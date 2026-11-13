#pragma once

#ifndef HOUNDTTS_GOOGLE_TTS_H
#define HOUNDTTS_GOOGLE_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Google Cloud Text-to-Speech REST API.
// Uses a service-account JSON file for OAuth2 authentication.
// Requests LINEAR16 (16kHz mono PCM) → pushes raw PCM chunks to PCMQueue.
// Calls queue.MarkDone() on every normal return path (success or failure);
// TTSPipeline's dispatch catch-all delivers MarkDone if this throws.
class GoogleTTS {
public:
    // credsFile: path to Google service-account JSON (from [Google] credentials_file).
    //            Secret: file contents (private key) are never logged or returned
    //            to Lua; only the path may appear in an error log. Missing,
    //            unreadable, or non-service-account JSON fails the call.
    // voice:     full voice name e.g. "en-US-Wavenet-D" (optional; uses languageCode default if empty)
    // culture:   BCP-47 locale e.g. "en-US"
    // gender:    "male" | "female" | "neutral"
    // speed:     speaking rate multiplier (0.25–4.0)
    // volume:    0.0..1.0 (applied as PCM sample scalar)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& credsFile,
        const std::string& voice,
        const std::string& culture,
        const std::string& gender,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_GOOGLE_TTS_H
