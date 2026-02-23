#pragma once

#ifndef HOUNDTTS_GOOGLE_TTS_H
#define HOUNDTTS_GOOGLE_TTS_H

#include "audio_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Google Cloud Text-to-Speech REST API.
// Requests LINEAR16 (raw 16kHz mono PCM) → encodes to Opus → pushes to AudioQueue.
// Calls queue.MarkDone() when done (success or failure).
class GoogleTTS {
public:
    // credsFile: path to Google service-account JSON file
    // voice:     Google voice name e.g. "en-GB-Neural2-A" (optional)
    // culture:   BCP-47 locale e.g. "en-US"
    // gender:    "male" | "female" | "neutral"
    // speed:     speaking rate (1.0 = normal, 0.25–4.0)
    // volume:    0.0..1.0 (applied as PCM sample scalar)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& credsFile,
        const std::string& voice,
        const std::string& culture,
        const std::string& gender,
        double speed,
        double volume,
        AudioQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_GOOGLE_TTS_H
