#include "tone.h"
#include "../../utils.h"

#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <thread>
#include <chrono>

namespace HoundTTS {

void GenerateTone(std::shared_ptr<PCMQueue> queue,
                  std::shared_ptr<Session>  session,
                  double durationS,
                  float  freqHz,
                  float  volume) {
    static const int kSampleRate  = 16000;
    static const int kChunkFrames = kSampleRate;   // 1 second per chunk
    static const float k2Pi = 2.0f * 3.14159265f;

    // Validate duration: NaN or Inf → safe fallback
    if (!std::isfinite(durationS)) {
        Logger::Instance().Error("HoundTTS/Tone",
            "Non-finite duration (" + std::to_string(durationS) + "); coercing to 2s");
        durationS = 2.0;
    }

    // Hard ceiling: 7200 s (2 hours), same as noise generator
    static const double kMaxDuration = 7200.0;
    if (durationS <= 0.0) durationS = 2.0;
    if (durationS > kMaxDuration) durationS = kMaxDuration;

    const int totalSamples = std::max(1, static_cast<int>(std::ceil(durationS * kSampleRate)));
    int samplesGenerated = 0;

    // Guard against NaN/Inf: std::min/std::max of NaN are undefined-ish
    // (can propagate NaN), and sin()/int cast on NaN yields undefined output.
    if (!std::isfinite(freqHz)) freqHz = 440.0f;
    if (!std::isfinite(volume)) volume = 1.0f;

    const float nyquist = kSampleRate * 0.5f;
    freqHz = std::max(20.0f, std::min(nyquist - 1.0f, freqHz));
    float vol = std::max(0.0f, std::min(1.0f, volume));

    auto isAlive = [&session]() { return !session || session->alive.load(); };

    while (isAlive() && samplesGenerated < totalSamples) {
        int chunkSize = kChunkFrames;
        int remaining = totalSamples - samplesGenerated;
        if (remaining < chunkSize) chunkSize = remaining;

        std::vector<int16_t> chunk(chunkSize);
        for (int i = 0; i < chunkSize; ++i) {
            float t   = static_cast<float>(samplesGenerated + i) / kSampleRate;
            float val = std::sin(k2Pi * freqHz * t);
            chunk[i] = static_cast<int16_t>(val * 32767.0f * vol);
        }

        samplesGenerated += chunkSize;
        queue->Push(std::move(chunk));

        // Pace at ~real-time to prevent unbounded audio buffering downstream.
        int paceMs = std::max(0, chunkSize * 1000 / kSampleRate - 200);
        for (int waited = 0; waited < paceMs && isAlive(); waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    queue->MarkDone();
}

} // namespace HoundTTS
