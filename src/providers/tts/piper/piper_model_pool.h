#pragma once

#ifndef HOUNDTTS_PIPER_MODEL_POOL_H
#define HOUNDTTS_PIPER_MODEL_POOL_H

#include "piper/piper.h"

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace HoundTTS {

// Thread-safe pool of piper_synthesizer* handles, keyed by model path.
//
// Concurrency model:
//   - Each concurrent TTS request acquires its own piper_synthesizer* from the pool.
//   - If no idle instance exists for a model, a new one is created (ONNX session loaded).
//   - After synthesis, the handle is returned to the pool for reuse (model stays loaded).
//   - piper_synthesize_start (espeak-ng phonemization) is serialized via espeakMutex_
//     because espeak-ng uses global process state. This step is fast (~1-5 ms).
//   - piper_synthesize_next (ONNX inference) runs concurrently across instances,
//     but the total number of ACTIVE synthesizers is capped by maxActive_ to prevent
//     ORT/memory exhaustion under burst load. Excess Acquire() calls block until
//     a slot frees up in Release().
class PiperModelPool {
public:
    static PiperModelPool& Instance();

    // Acquire a synthesizer for the given model and espeak-ng data path.
    // Creates a new instance if no idle one is available.
    // Returns nullptr if piper.dll is unavailable or the model fails to load.
    piper_synthesizer* Acquire(const std::string& modelPath,
                               const std::string& espeakDataPath);

    // Return a synthesizer to the pool for reuse.
    // Must be called with the same modelPath used in Acquire().
    void Release(const std::string& modelPath, piper_synthesizer* synth);

    // Serialize a piper_synthesize_start call under the espeak-ng global mutex.
    // Returns the result of piper_synthesize_start.
    int StartSynthesize(piper_synthesizer* synth, const char* text,
                        const piper_synthesize_options* options);

    // Free all pooled synthesizers. Called on shutdown.
    void Clear();

private:
    PiperModelPool() = default;
    ~PiperModelPool() = default;
    PiperModelPool(const PiperModelPool&) = delete;
    PiperModelPool& operator=(const PiperModelPool&) = delete;

    std::mutex poolMutex_;
    std::mutex espeakMutex_;
    std::map<std::string, std::vector<piper_synthesizer*>> pool_;

    // Bounded active-synthesizer semaphore (guarded by poolMutex_).
    // maxActive_ is lazily initialized from config on first Acquire().
    std::condition_variable activeCv_;
    int  activeCount_ = 0;
    int  maxActive_   = 0;   // 0 = uninitialized, set on first Acquire()
};

} // namespace HoundTTS

#endif // HOUNDTTS_PIPER_MODEL_POOL_H
