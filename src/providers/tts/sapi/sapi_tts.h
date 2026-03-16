#pragma once

#ifndef HOUNDTTS_SAPI_TTS_H
#define HOUNDTTS_SAPI_TTS_H

#include <string>
#include <vector>
#include <cstdint>

namespace HoundTTS {

// Synthesizes text to 16kHz mono 16-bit PCM using Windows SAPI (ISpVoice).
// Returns the full PCM buffer in one call (synthesize-all).
class SapiTTS {
public:
    // Synthesize text to PCM.
    // voice:   specific voice name (overrides gender/culture if non-empty)
    // gender:  "male" / "female" / "neuter" (ignored if voice is set)
    // culture: BCP-47 locale string e.g. "en-US" (ignored if voice is set)
    // speed:   SAPI rate -10..+10 (default 0 = normal)
    // volume:  0.0..1.0 (mapped to SAPI 0..100)
    // Returns empty vector on failure.
    static std::vector<int16_t> Synthesize(
        const std::string& text,
        const std::string& voice,
        const std::string& gender,
        const std::string& culture,
        double speed,
        double volume);
};

} // namespace HoundTTS

#endif // HOUNDTTS_SAPI_TTS_H
