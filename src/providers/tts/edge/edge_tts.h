#pragma once

#ifndef HOUNDTTS_EDGE_TTS_H
#define HOUNDTTS_EDGE_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Microsoft Edge's online Read Aloud TTS service.
// Uses a reverse-engineered WebSocket API — no API key required.
// Requests 24kHz MP3, decodes to 16kHz 16-bit mono PCM → pushes to PCMQueue.
// Calls queue.MarkDone() when done (success or failure).
//
// WARNING: This uses an unofficial Microsoft endpoint. It may stop working
// if Microsoft changes the API, revokes the trusted client token, or
// modifies the DRM scheme. No SLA or guarantees.
class EdgeTTS {
public:
    // voice:   Azure Neural voice name e.g. "en-US-AriaNeural" (optional, uses default if empty)
    // culture: BCP-47 locale e.g. "en-US" (used to pick default voice if voice is empty)
    // gender:  "male" | "female" (used to pick default voice if voice is empty)
    // speed:   rate multiplier (1.0 = normal; mapped to SSML prosody rate)
    // volume:  0.0..1.0 (applied as PCM sample scalar)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& voice,
        const std::string& culture,
        const std::string& gender,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_EDGE_TTS_H
