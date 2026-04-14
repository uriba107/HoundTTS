#include "piper_model_pool.h"
#include "piper_native.h"
#include "config_reader.h"
#include "utils.h"

namespace HoundTTS {

static const int kMaxIdlePerModel = 4;
static const char* kTag = "HoundTTS/PiperModelPool";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

PiperModelPool& PiperModelPool::Instance() {
    static PiperModelPool instance;
    return instance;
}

piper_synthesizer* PiperModelPool::Acquire(const std::string& modelPath,
                                            const std::string& espeakDataPath) {
    auto& native = PiperNative::Instance();
    if (!native.Available()) return nullptr;

    // Bounded concurrency: block until an active slot is available, then
    // try to take an idle instance from the pool or fall through to create one.
    {
        std::unique_lock<std::mutex> lock(poolMutex_);
        if (maxActive_ <= 0) {
            int cfg = ConfigReader::Instance().GetPiperMaxConcurrent();
            maxActive_ = (cfg > 0) ? cfg : 4;
            LogI("Max concurrent synthesizers: " + std::to_string(maxActive_));
        }
        activeCv_.wait(lock, [this]{ return activeCount_ < maxActive_; });
        ++activeCount_;

        auto it = pool_.find(modelPath);
        if (it != pool_.end() && !it->second.empty()) {
            piper_synthesizer* synth = it->second.back();
            it->second.pop_back();
            return synth;
        }
    }

    // No idle instance — create a new one.
    // piper_create internally calls espeak_Initialize which is safe to call
    // multiple times; guard under espeakMutex_ for safety during init.
    piper_synthesizer* synth = nullptr;
    {
        std::lock_guard<std::mutex> lock(espeakMutex_);
        LogI("Loading model: " + modelPath);
        synth = native.Create(modelPath.c_str(), nullptr, espeakDataPath.c_str());
    }

    if (!synth) {
        LogE("piper_create failed for model: " + modelPath);
        // Release the active slot we reserved above.
        {
            std::lock_guard<std::mutex> lock(poolMutex_);
            --activeCount_;
        }
        activeCv_.notify_one();
        return nullptr;
    }

    LogI("Model loaded: " + modelPath);
    return synth;
}

void PiperModelPool::Release(const std::string& modelPath, piper_synthesizer* synth) {
    if (!synth) return;
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        auto& idle = pool_[modelPath];
        if (static_cast<int>(idle.size()) < kMaxIdlePerModel) {
            idle.push_back(synth);
        } else {
            LogI("Pool cap reached for model, destroying excess instance: " + modelPath);
            PiperNative::Instance().Free(synth);
        }
        --activeCount_;
    }
    // Notify one waiter outside the lock to avoid spurious wake-and-reblock.
    activeCv_.notify_one();
}

int PiperModelPool::StartSynthesize(piper_synthesizer* synth, const char* text,
                                     const piper_synthesize_options* options) {
    // Serialize espeak_SetVoiceByName + phonemization (global espeak-ng state).
    std::lock_guard<std::mutex> lock(espeakMutex_);
    return PiperNative::Instance().SynthesizeStart(synth, text, options);
}

void PiperModelPool::Clear() {
    auto& native = PiperNative::Instance();
    std::lock_guard<std::mutex> lock(poolMutex_);
    for (auto& kv : pool_) {
        for (auto* synth : kv.second) {
            native.Free(synth);
        }
    }
    pool_.clear();
    LogI("All pooled models freed");
}

} // namespace HoundTTS
