#include "noise.h"
#include "../../utils.h"

#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>

namespace HoundTTS {

void GenerateNoise(std::shared_ptr<PCMQueue> queue,
                   std::shared_ptr<Session>  session,
                   const std::string&        noiseType,
                   uint32_t                  seed,
                   float                     volume,
                   double                    duration) {
    static const int   kSampleRate  = 16000;
    static const int   kChunkFrames = kSampleRate;   // 1 second per chunk
    static const float k2Pi         = 2.0f * 3.14159265f;

    // Validate duration: NaN or Inf would cause undefined behaviour in the
    // float→int conversion below, so coerce to a safe fallback.
    if (!std::isfinite(duration)) {
        Logger::Instance().Error("HoundTTS/Noise",
            "Non-finite duration (" + std::to_string(duration) + "); coercing to 5s");
        duration = 5.0;
    }

    // Hard ceiling: no noise session may exceed 2 hours (7200 s).
    static const double kMaxDuration = 7200.0;

    // <=0 means "continuous" — cap at kMaxDuration so it always terminates.
    if (duration <= 0.0) {
        duration = kMaxDuration;
    } else if (duration > kMaxDuration) {
        duration = kMaxDuration;
    }

    const int totalSamples = std::max(1, static_cast<int>(std::ceil(duration * kSampleRate)));
    int samplesGenerated = 0;

    std::mt19937  rng(seed);
    std::uniform_real_distribution<float> uniNoise(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uniFreq(300.0f, 3400.0f);  // speech intelligibility band
    std::uniform_real_distribution<float> uniPhase(0.0f, k2Pi);

    struct Osc { float freq; float phase; float life; float maxLife; };
    static const int kNumOsc = 8;  // dense spectral coverage for effective masking
    std::vector<Osc> oscs(kNumOsc);
    for (auto& o : oscs) {
        o.freq    = uniFreq(rng);
        o.phase   = uniPhase(rng);
        o.maxLife = kSampleRate * std::uniform_real_distribution<float>(0.05f, 0.20f)(rng);
        o.life    = o.maxLife;
    }

    float vol = std::max(0.0f, std::min(1.0f, volume));

    // Noise floor dominates — oscillators add spectral density.
    // Weights intentionally exceed 1.0 with overdrive; hard clip below handles overflow
    // and the resulting flat-top waveform is rich in harmonics (maximises masking power).
    static const float kNoiseW = 0.65f;
    static const float kOscW   = (1.0f - kNoiseW) / kNumOsc;  // ~0.044 each

    // Pink noise state — Paul Kellett's 7-coefficient Voss-McCartney approximation.
    // Produces 1/f spectrum (+3 dB/octave perceptual loudness vs white, matches
    // Fletcher-Munson equal-loudness curves far better for wideband jamming noise).
    float pk[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // 20 Hz single-pole high-pass filter state (applied to all noise types).
    // Removes DC and infrasonic content — no energy below human hearing is wasted.
    // Coefficient α = fs / (fs + 2π·fc)  where fc=20 Hz, fs=16000 Hz
    static const float kHPCoeff = static_cast<float>(kSampleRate) / (static_cast<float>(kSampleRate) + k2Pi * 20.0f); // ≈ 0.99221
    float hpPrev  = 0.0f; // previous raw sample (filter input memory)
    float hpOut   = 0.0f; // previous filter output memory

    // Jam-mode syllabic AM envelope state (initialised here, used only by "jam")
    float amPhase = 0.0f;
    float amFreq  = 5.0f;
    float amLife  = 0.0f;

    // Helper: returns true while the session is alive (or session is null — finite duration only)
    auto isAlive = [&session]() { return !session || session->alive.load(); };

    // totalSamples is always >= 1 (clamped + kMaxDuration above), so the loop
    // is always bounded and the "continuous mode" branch is unnecessary.
    while (isAlive()) {
        int remaining = totalSamples - samplesGenerated;
        if (remaining <= 0) break;
        int chunkSize = std::min(kChunkFrames, remaining);

        std::vector<int16_t> chunk(chunkSize);

        for (int i = 0; i < chunkSize; ++i) {
            if ((i % 1000 == 0) && !isAlive()) {
                chunk.resize(static_cast<size_t>(i));
                break;
            }

            float sample = 0.0f;

            if (noiseType == "white" || noiseType == "pink") {
                // Paul Kellett's 7-stage IIR approximation of 1/f (pink) noise.
                // "white" is aliased here — pink sounds significantly fuller to
                // human ears and wastes no energy on perceptually inaudible content.
                // Each stage covers a different octave; sum = -3 dB/octave rolloff.
                float white = uniNoise(rng);
                pk[0] =  0.99886f * pk[0] + white * 0.0555179f;
                pk[1] =  0.99332f * pk[1] + white * 0.0750759f;
                pk[2] =  0.96900f * pk[2] + white * 0.1538520f;
                pk[3] =  0.86650f * pk[3] + white * 0.3104856f;
                pk[4] =  0.55000f * pk[4] + white * 0.5329522f;
                pk[5] = -0.75862f * pk[5] - white * 0.0168980f;
                float pink = (pk[0] + pk[1] + pk[2] + pk[3] + pk[4] + pk[5]
                              + pk[6] + white * 0.5362f) * 0.25f;  // boosted gain — fills headroom
                pk[6] = white * 0.115926f;
                sample = pink * 1.8f;  // overdrive — hard clip adds harmonics

            } else if (noiseType == "chirp") {
                // Noise floor + swept sine oscillators — oscillators dominant for siren effect
                // Weights tuned so tonal elements are clearly audible above noise floor
                static const float kChirpNoiseW = 0.25f;    // Noise at 25%
                static const float kChirpOscW   = 0.09375f; // Each osc at ~9.4% (75% total)
                sample = uniNoise(rng) * kChirpNoiseW;
                for (auto& o : oscs) {
                    sample += std::sin(o.phase) * kChirpOscW;
                    o.phase += k2Pi * o.freq / kSampleRate;
                    if (o.phase > k2Pi) o.phase -= k2Pi;
                    o.life -= 1.0f;
                    if (o.life <= 0.0f) {
                        o.freq    = uniFreq(rng);
                        o.phase   = uniPhase(rng);
                        o.maxLife = kSampleRate * std::uniform_real_distribution<float>(0.15f, 0.40f)(rng); // 150-400 ms for siren effect
                        o.life    = o.maxLife;
                    }
                }
                sample *= 1.1f;  // mild overdrive — oscillators stay audible

            } else if (noiseType == "jam") {
                // "jam" — designed to resist both Speex denoising AND Speex AGC.
                //
                // Anti-denoise (spectral):
                //   Ultra-short oscillator lifetimes (5-35 ms) change the spectrum
                //   faster than Speex's 10 ms analysis window can converge.
                //   Random micro-bursts add non-stationary transients.
                //
                // Anti-AGC (amplitude):
                //   Sharp duty-cycle AM pulses instead of smooth sine.  Brief
                //   "dip" phases (15-40 ms at ~25% power) force AGC gain upward
                //   via its slow increment path; "on" phases (30-100 ms at ~90%)
                //   then arrive before AGC can decrement, creating a pumping
                //   effect that keeps perceived loudness high.

                // --- Duty-cycle AM envelope (anti-AGC) ---
                if (amLife <= 0.0f) {
                    if (amPhase > 0.5f) {
                        // Was ON → switch to DIP (force AGC gain up)
                        amPhase = std::uniform_real_distribution<float>(0.20f, 0.35f)(rng);
                        amLife  = kSampleRate * std::uniform_real_distribution<float>(0.015f, 0.040f)(rng);
                    } else {
                        // Was DIP → switch to ON (hit at boosted AGC gain)
                        amPhase = std::uniform_real_distribution<float>(0.85f, 1.0f)(rng);
                        amLife  = kSampleRate * std::uniform_real_distribution<float>(0.030f, 0.100f)(rng);
                    }
                }
                amLife -= 1.0f;
                float amEnv = amPhase;

                // --- Base noise (pink, for perceptual fullness) ---
                float white = uniNoise(rng);
                pk[0] =  0.99886f * pk[0] + white * 0.0555179f;
                pk[1] =  0.99332f * pk[1] + white * 0.0750759f;
                pk[2] =  0.96900f * pk[2] + white * 0.1538520f;
                pk[3] =  0.86650f * pk[3] + white * 0.3104856f;
                pk[4] =  0.55000f * pk[4] + white * 0.5329522f;
                pk[5] = -0.75862f * pk[5] - white * 0.0168980f;
                float pink = (pk[0] + pk[1] + pk[2] + pk[3] + pk[4] + pk[5]
                              + pk[6] + white * 0.5362f) * 0.20f;
                pk[6] = white * 0.115926f;

                // --- Rapid-sweep oscillators (5-35 ms lifetimes) ---
                float oscSum = 0.0f;
                for (auto& o : oscs) {
                    oscSum += std::sin(o.phase) * kOscW;
                    o.phase += k2Pi * o.freq / kSampleRate;
                    if (o.phase > k2Pi) o.phase -= k2Pi;
                    o.life -= 1.0f;
                    if (o.life <= 0.0f) {
                        o.freq    = uniFreq(rng);
                        o.phase   = uniPhase(rng);
                        // 5-35 ms: half a Speex frame at the low end
                        o.maxLife = kSampleRate * std::uniform_real_distribution<float>(0.005f, 0.035f)(rng);
                        o.life    = o.maxLife;
                    }
                }

                // --- Random micro-bursts (non-stationary transients) ---
                float burst = 0.0f;
                if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.010f) {
                    // ~160 impulses/sec on average — sparse but disruptive
                    burst = uniNoise(rng) * 0.6f;
                }

                sample = (pink + oscSum + burst) * amEnv * 2.2f;

            } else {
                // "harsh" — noise floor + square-wave oscillators.
                // Square waves are already harmonic-rich; weights tuned so buzzing
                // is clearly audible. Moderate overdrive preserves square-wave character.
                static const float kHarshNoiseW = 0.20f;  // Noise at 20%
                static const float kHarshOscW   = 0.10f;   // Each osc at 10% (80% total)
                sample = uniNoise(rng) * kHarshNoiseW;
                for (auto& o : oscs) {
                    float sq = std::sin(o.phase) >= 0.0f ? 1.0f : -1.0f;
                    sample += sq * kHarshOscW;
                    o.phase += k2Pi * o.freq / kSampleRate;
                    if (o.phase > k2Pi) o.phase -= k2Pi;
                    o.life -= 1.0f;
                    if (o.life <= 0.0f) {
                        o.freq    = uniFreq(rng);
                        o.phase   = uniPhase(rng);
                        o.maxLife = kSampleRate * std::uniform_real_distribution<float>(0.03f, 0.12f)(rng);
                        o.life    = o.maxLife;
                    }
                }
                sample *= 1.2f;  // moderate overdrive — square waves stay buzzy
            }

            // 20 Hz single-pole high-pass: y[n] = α·(y[n-1] + x[n] - x[n-1])
            // Removes DC offset and sub-20 Hz infrasonic content from all types.
            float hpFiltered = kHPCoeff * (hpOut + sample - hpPrev);
            hpPrev = sample;
            hpOut  = hpFiltered;

            // Hard clip then scale to full INT16 range
            sample   = std::max(-1.0f, std::min(1.0f, hpFiltered));
            chunk[i] = static_cast<int16_t>(sample * 32767.0f * vol);
        }

        if (!chunk.empty()) {
            samplesGenerated += static_cast<int>(chunk.size());
            queue->Push(std::move(chunk));

            // Pace at ~real-time to prevent unbounded audio buffering downstream.
            // The Opus encoder and UDP timer consume at real-time (40 ms/frame);
            // without this sleep the entire session would be generated in seconds
            // and queued in memory.  Sleep slightly less than chunk duration to
            // maintain a small lead (~1-2 s) for jitter absorption.
            int paceMs = std::max(0, chunkSize * 1000 / kSampleRate - 200);
            for (int waited = 0; waited < paceMs && isAlive(); waited += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    queue->MarkDone();
}

} // namespace HoundTTS
