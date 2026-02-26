#pragma once

#ifndef HOUNDTTS_SPEECH_TIME_H
#define HOUNDTTS_SPEECH_TIME_H

#include <cmath>
#include <cstring>
#include <algorithm>

namespace HoundTTS {

// Estimate speech duration in seconds.
// length:   character count (or string length)
// speed:    rate value (interpretation depends on provider)
// provider: "sapi", "google", "azure", "polly", "elevenlabs", "piper", "kittentts"
inline double GetSpeechTime(int length, double speed = 1.0, const char* provider = "sapi") {
    double speedFactor = 1.0;

    if (std::strcmp(provider, "sapi") == 0) {
        // SAPI rate: -10..+10 where 0 = normal.
        // Matches STTS.getSpeechTime and HOUND convention.
        const double maxRateRatio = 3.0;
        if (speed != 0.0) {
            speedFactor = std::abs(speed) * (maxRateRatio - 1.0) / 10.0 + 1.0;
        }
        if (speed < 0.0) {
            speedFactor = 1.0 / speedFactor;
        }
    } else if (std::strcmp(provider, "elevenlabs") == 0) {
        // ElevenLabs: direct multiplier, clamped to 0.7–1.2
        speedFactor = std::max(0.7, std::min(1.2, speed));
    } else if (std::strcmp(provider, "piper") == 0) {
        // Piper: speed is ignored by the engine
        speedFactor = 1.0;
    } else if (std::strcmp(provider, "kittentts") == 0 || std::strcmp(provider, "kitten") == 0) {
        // KittenTTS: direct speed multiplier (0.1–4.0)
        speedFactor = std::max(0.1, std::min(4.0, speed));
    } else {
        // google, azure, polly: direct multiplier
        speedFactor = speed;
    }

    double wpm = std::ceil(100.0 * speedFactor);
    double cps = std::floor((wpm * 5.0) / 60.0);
    if (cps <= 0.0) cps = 1.0;

    return std::ceil(static_cast<double>(length) / cps);
}

} // namespace HoundTTS

#endif // HOUNDTTS_SPEECH_TIME_H
