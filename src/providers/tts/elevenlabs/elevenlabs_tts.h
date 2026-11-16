#pragma once

#ifndef HOUNDTTS_ELEVENLABS_TTS_H
#define HOUNDTTS_ELEVENLABS_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via ElevenLabs WebSocket streaming API.
// Requests pcm_16000 (raw 16kHz mono PCM) → pushes raw PCM chunks to PCMQueue.
// Calls queue.MarkDone() on every normal return path (success or failure);
// TTSPipeline's dispatch catch-all delivers MarkDone if this throws.
class ElevenLabsTTS {
public:
    // apiKey:  ElevenLabs API key — secret. Used only for the xi-api-key header
    //          and the BOS frame; never logged, never returned to Lua.
    // modelId: ElevenLabs model e.g. "eleven_turbo_v2"
    // voiceId: ElevenLabs voice ID (per-call, from Lua provider_params.voice)
    // speed:   speaking rate (0.7–1.2 supported by ElevenLabs)
    // volume:  0.0..1.0 (applied as PCM sample scalar)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& apiKey,
        const std::string& modelId,
        const std::string& voiceId,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_ELEVENLABS_TTS_H
