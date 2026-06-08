#pragma once

#ifndef HOUNDTTS_SPEECH_TIME_H
#define HOUNDTTS_SPEECH_TIME_H

#include <cmath>
#include <algorithm>
#include "provider.h"

namespace HoundTTS {

// Estimate speech duration in seconds.
// length:   character count (or string length)
// speed:    rate value (interpretation depends on provider)
inline double GetSpeechTime(int length, double speed = 1.0,
                            TtsProvider provider = TtsProvider::Sapi) {
    double speedFactor = 1.0;

    switch (provider) {
    case TtsProvider::Sapi:
        // SAPI rate: -10..+10 where 0 = normal.
        // Matches STTS.getSpeechTime and HOUND convention.
        {
            const double maxRateRatio = 3.0;
            if (speed != 0.0) {
                speedFactor = std::abs(speed) * (maxRateRatio - 1.0) / 10.0 + 1.0;
            }
            if (speed < 0.0) {
                speedFactor = 1.0 / speedFactor;
            }
        }
        break;
    case TtsProvider::Piper:
        // Piper: length_scale = 1/speed, so speed=2 → 2x faster.
        // Clamp to sane range to avoid division issues.
        speedFactor = std::max(0.1, std::min(10.0, speed));
        break;
    case TtsProvider::ElevenLabs:
        // ElevenLabs: direct multiplier, clamped 0.7–1.2
        speedFactor = std::max(0.7, std::min(1.2, speed));
        break;
    case TtsProvider::Google:
        // Google: speakingRate, clamped 0.25–4.0
        speedFactor = std::max(0.25, std::min(4.0, speed));
        break;
    case TtsProvider::AWS:
        // AWS Polly: SSML prosody rate, clamped 0.2–2.0
        speedFactor = std::max(0.2, std::min(2.0, speed));
        break;
    case TtsProvider::KittenTTS:
        // KittenTTS: direct speed multiplier (0.1–4.0)
        speedFactor = std::max(0.1, std::min(4.0, speed));
        break;
    case TtsProvider::OpenAI:
        // OpenAI: direct speed multiplier (0.25–4.0)
        speedFactor = std::max(0.25, std::min(4.0, speed));
        break;
    case TtsProvider::Azure:
    case TtsProvider::Edge:
        // Azure & Edge use the same neural voices and SSML prosody rate
        // mapping (speed 1.0 = +0%, 2.0 = +100%). Clamp to 0.5–3.0.
        speedFactor = std::max(0.5, std::min(3.0, speed));
        break;
    case TtsProvider::Supertonic:
        // Supertonic: direct speed multiplier, clamped 0.5–2.0
        speedFactor = std::max(0.5, std::min(2.0, speed));
        break;
    default:
        // Unknown: direct multiplier, clamped to sane range
        speedFactor = std::max(0.1, std::min(4.0, speed));
        break;
    }

    double wpm = std::ceil(100.0 * speedFactor); // Word per min based on speed factor
    double cps = std::floor((wpm * 5.0) / 60.0); // Character per sec based on WPM count - assuming english 5 chars per word avarage.
    if (cps <= 0.0) cps = 1.0;

    return std::ceil(static_cast<double>(length) / cps);
}

} // namespace HoundTTS

#endif // HOUNDTTS_SPEECH_TIME_H
