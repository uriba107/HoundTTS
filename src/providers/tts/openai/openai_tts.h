#pragma once

#ifndef HOUNDTTS_OPENAI_TTS_H
#define HOUNDTTS_OPENAI_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via the OpenAI (or compatible) TTS REST API.
// Decodes WAV/Ogg-Opus response → pushes raw PCM chunks to PCMQueue.
// Calls queue.MarkDone() when done (success or failure).
class OpenAITTS {
public:
    // endpoint: Base URL, e.g. "https://api.openai.com" or "http://localhost:8080"
    //           /v1/audio/speech is always appended.
    // apiKey:   Bearer token (may be empty for local deployments)
    // model:    TTS model name, e.g. "tts-1" or "tts-1-hd"
    // voice:    Voice name, e.g. "alloy", "echo", "shimmer"
    // speed:    Speaking rate (0.25–4.0)
    // volume:   0.0..1.0 (applied as PCM sample scalar after decode)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& endpoint,
        const std::string& apiKey,
        const std::string& model,
        const std::string& voice,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_OPENAI_TTS_H
