#pragma once

#ifndef HOUNDTTS_ELEVENLABS_TTS_H
#define HOUNDTTS_ELEVENLABS_TTS_H

#include "audio_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via ElevenLabs WebSocket streaming API.
// Requests pcm_16000 (raw 16kHz mono PCM) → encodes to Opus → pushes to AudioQueue.
// Calls queue.MarkDone() when done (success or failure).
class ElevenLabsTTS {
public:
    // apiKey:  ElevenLabs API key
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
        AudioQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_ELEVENLABS_TTS_H
