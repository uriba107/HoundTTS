#pragma once

#ifndef HOUNDTTS_TTS_PIPELINE_H
#define HOUNDTTS_TTS_PIPELINE_H

#include "backend.h"
#include "backends/pcm_queue.h"
#include "session.h"
#include <memory>
#include <string>

namespace HoundTTS {

// Shared TTS pipeline: translates (optional) then synthesises audio into a PCMQueue.
// Backend-agnostic — any ITTSBackend can consume the resulting PCM.
class TTSPipeline {
public:
    // Run in the caller's thread (or a detached thread).
    // Reads config, optionally translates, dispatches to the appropriate TTS provider,
    // and pushes 16kHz mono int16_t PCM chunks into queue.
    // Always calls queue.MarkDone() before returning, even on error.
    static void Produce(const TTSRequest& req, std::shared_ptr<PCMQueue> queue);

    // Generate noise PCM into queue.
    // noiseType: "white" | "chirp" | "harsh" | "jam" — seed: RNG seed — volume: 0.0-1.0
    // duration: seconds (<=0 = continuous until session->alive becomes false)
    // Always calls queue->MarkDone() before returning.
    static void ProduceNoise(std::shared_ptr<PCMQueue> queue,
                             std::shared_ptr<Session> session,
                             const std::string& noiseType,
                             uint32_t seed,
                             float volume,
                             double duration = 0.0);

    // Generate a fixed-frequency sine-wave tone into queue.
    // session: checked for alive flag so KillSession can interrupt the tone.
    // duration: seconds (<=0 defaults to 2.0).
    // freqHz: tone frequency — volume: 0.0-1.0
    // Always calls queue->MarkDone() before returning.
    static void ProduceTone(std::shared_ptr<PCMQueue> queue,
                            std::shared_ptr<Session> session,
                            double duration = 2.0,
                            float  freqHz   = 440.0f,
                            float  volume   = 1.0f);
};

} // namespace HoundTTS

#endif // HOUNDTTS_TTS_PIPELINE_H
