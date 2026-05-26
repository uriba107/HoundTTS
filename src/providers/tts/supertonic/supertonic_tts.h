#pragma once

#ifndef HOUNDTTS_SUPERTONIC_TTS_H
#define HOUNDTTS_SUPERTONIC_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via supertonic.dll (LoadLibrary, no link-time dependency).
// Pushes 16kHz mono int16 PCM chunks into queue. Calls queue.MarkDone() on completion.
class SupertonicTTS {
public:
    // dllDir:       directory containing supertonic.dll + onnxruntime.dll
    // modelPath:    directory containing ONNX models (tts.json, *.onnx, unicode_indexer.json)
    // stylePath:    path to voice style JSON file (e.g. M1.json)
    // lang:         language code (e.g. "en")
    // totalSteps:   denoising steps (e.g. 8)
    // speed:        speech speed multiplier (e.g. 1.05)
    // volume:       0.0..1.0 (applied as PCM sample scalar)
    // maxConcurrent: max concurrent synthesis cap
    // threads:      ORT intra-op threads
    // Returns false on unrecoverable error.
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& dllDir,
        const std::string& modelPath,
        const std::string& stylePath,
        const std::string& lang,
        int    totalSteps,
        float  speed,
        double volume,
        int    maxConcurrent,
        int    threads,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_SUPERTONIC_TTS_H
