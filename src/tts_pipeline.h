#pragma once

#ifndef HOUNDTTS_TTS_PIPELINE_H
#define HOUNDTTS_TTS_PIPELINE_H

#include "backend.h"
#include "backends/pcm_queue.h"
#include <memory>

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
};

} // namespace HoundTTS

#endif // HOUNDTTS_TTS_PIPELINE_H
