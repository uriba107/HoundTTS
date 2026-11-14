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

    // Every exit path except a successful hand-off must give the reserved slot
    // back — including an exception from an allocation below — otherwise
    // Acquire() starves permanently.
    struct SlotGuard {
        PiperModelPool& pool;
        bool reserved = false;
        bool disarmed = false;
        // Only release a slot that was actually reserved: a throw from wait()
        // or the map lookup before ++activeCount_ must not underflow the count.
        ~SlotGuard() noexcept {
            if (reserved && !disarmed) pool.ReleaseActiveSlot();
        }
    } slotGuard{*this};

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
        slotGuard.reserved = true;

        auto it = pool_.find(modelPath);
        if (it != pool_.end() && !it->second.empty()) {
            piper_synthesizer* synth = it->second.back();
            it->second.pop_back();
            slotGuard.disarmed = true;
            return synth;
        }
    }

    // No idle instance — create a new one.
    // piper_create internally calls espeak_Initialize which is safe to call
    // multiple times; guard under espeakMutex_ for safety during init.
    piper_synthesizer* synth = nullptr;
    try {
        std::lock_guard<std::mutex> lock(espeakMutex_);
        LogI("Loading model: " + modelPath);
        synth = native.Create(modelPath.c_str(), nullptr, espeakDataPath.c_str());
    } catch (const std::exception& e) {
        LogE(std::string("piper_create threw for model: ") + modelPath + ": " + e.what());
        throw;
    } catch (...) {
        LogE("piper_create threw for model: " + modelPath + ": unknown exception");
        throw;
    }

    if (!synth) {
        LogE("piper_create failed for model: " + modelPath);
        return nullptr;
    }

    LogI("Model loaded: " + modelPath);
    slotGuard.disarmed = true;
    return synth;
}

void PiperModelPool::ReleaseActiveSlot() noexcept {
    // Called from a noexcept destructor: a throwing lock must not terminate.
    try {
        {
            std::lock_guard<std::mutex> lock(poolMutex_);
            --activeCount_;
        }
        // Notify outside the lock to avoid spurious wake-and-reblock.
        activeCv_.notify_one();
    } catch (...) {}
}

void PiperModelPool::Release(const std::string& modelPath, piper_synthesizer* synth) {
    if (!synth) return;

    // The active slot must be released on every path — including a throwing
    // idle-queue insertion (allocation), which would otherwise permanently
    // reduce the pool's concurrency and starve Acquire().
    struct SlotGuard {
        std::mutex& mu;
        int& count;
        std::condition_variable& cv;
        bool released = false;
        ~SlotGuard() noexcept {
            try { Release(); } catch (...) {}
        }
        void Release() {
            if (released) return;
            {
                std::lock_guard<std::mutex> lock(mu);
                --count;
            }
            // Notify outside the lock to avoid spurious wake-and-reblock.
            cv.notify_one();
            released = true;
        }
    } slotGuard{poolMutex_, activeCount_, activeCv_};

    bool stored = false;
    try {
        std::lock_guard<std::mutex> lock(poolMutex_);
        auto& idle = pool_[modelPath];
        if (static_cast<int>(idle.size()) < kMaxIdlePerModel) {
            idle.push_back(synth);
            stored = true;
        }
    } catch (...) {
        // Never leak the handle we failed to store; the guard releases the slot
        // and the caller (PoolGuard) logs and swallows the exception.
        PiperNative::Instance().Free(synth);
        throw;
    }

    if (!stored) {
        LogI("Pool cap reached for model, destroying excess instance: " + modelPath);
        PiperNative::Instance().Free(synth);
    }
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
