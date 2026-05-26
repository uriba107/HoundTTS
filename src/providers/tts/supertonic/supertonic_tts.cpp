#include "supertonic_tts.h"
#include "supertonic/supertonic.h"
#include "utils.h"

#include <windows.h>

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <atomic>
#include <condition_variable>
#include <shared_mutex>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Supertonic";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// DLL loader — singleton, lazy-loads supertonic.dll via LoadLibrary
// ---------------------------------------------------------------------------

class SupertonicNative {
public:
    static SupertonicNative& Instance() {
        static SupertonicNative instance;
        return instance;
    }

    bool Load(const std::string& dllDir) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loaded_) return available_;
        loaded_ = false;
        available_ = false;

        std::string dllPath = dllDir;
        if (!dllPath.empty() && dllPath.back() != '\\' && dllPath.back() != '/')
            dllPath += '\\';
        dllPath += "supertonic.dll";

        LogI("Loading supertonic.dll from: " + dllPath);

        // Use AddDllDirectory + LoadLibraryExW (scoped, not process-wide)
        // so onnxruntime.dll is found by supertonic.dll's loader without
        // mutating the global DLL search path.
        std::wstring wDllDir = Utils::Utf8ToWide(dllDir);
        DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(wDllDir.c_str());

        hDll_ = LoadLibraryExW(Utils::Utf8ToWide(dllPath).c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        DWORD loadErr = GetLastError();

        if (cookie) RemoveDllDirectory(cookie);

        if (!hDll_) {
            LogE("LoadLibrary failed for supertonic.dll (GLE=" + std::to_string(loadErr) + ")");
            return false;
        }

        fn_create_      = (PFN_create)     GetProcAddress(hDll_, "supertonic_create");
        fn_free_        = (PFN_free)       GetProcAddress(hDll_, "supertonic_free");
        fn_load_style_  = (PFN_load_style) GetProcAddress(hDll_, "supertonic_load_style");
        fn_synthesize_  = (PFN_synthesize) GetProcAddress(hDll_, "supertonic_synthesize");
        fn_free_result_ = (PFN_free_result)GetProcAddress(hDll_, "supertonic_free_result");

        if (!fn_create_ || !fn_free_ || !fn_load_style_ || !fn_synthesize_ || !fn_free_result_) {
            LogE("supertonic.dll loaded but missing required exports — disabling");
            FreeLibrary(hDll_);
            hDll_ = nullptr;
            return false;
        }

        loaded_ = true;
        available_ = true;
        LogI("supertonic.dll loaded successfully");
        return true;
    }

    bool Available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_;
    }

    supertonic_context* Create(const char* onnx_dir, int num_threads) {
        return fn_create_(onnx_dir, num_threads);
    }
    void Free(supertonic_context* ctx) { fn_free_(ctx); }
    int LoadStyle(supertonic_context* ctx, const char* path) { return fn_load_style_(ctx, path); }
    int Synthesize(supertonic_context* ctx, const char* text, const char* lang,
                   int steps, float speed, supertonic_audio_chunk* out) {
        return fn_synthesize_(ctx, text, lang, steps, speed, out);
    }
    void FreeResult(supertonic_audio_chunk* chunk) { fn_free_result_(chunk); }

private:
    SupertonicNative() = default;
    ~SupertonicNative() = default;
    SupertonicNative(const SupertonicNative&) = delete;
    SupertonicNative& operator=(const SupertonicNative&) = delete;

    mutable std::mutex mutex_;
    bool    loaded_    = false;
    bool    available_ = false;
    HMODULE hDll_      = nullptr;

    typedef supertonic_context* (*PFN_create)(const char*, int);
    typedef void                (*PFN_free)(supertonic_context*);
    typedef int                 (*PFN_load_style)(supertonic_context*, const char*);
    typedef int                 (*PFN_synthesize)(supertonic_context*, const char*, const char*,
                                                  int, float, supertonic_audio_chunk*);
    typedef void                (*PFN_free_result)(supertonic_audio_chunk*);

    PFN_create      fn_create_      = nullptr;
    PFN_free        fn_free_        = nullptr;
    PFN_load_style  fn_load_style_  = nullptr;
    PFN_synthesize  fn_synthesize_  = nullptr;
    PFN_free_result fn_free_result_ = nullptr;
};

// ---------------------------------------------------------------------------
// Model pool — holds a single supertonic_context*, guards concurrency
// ---------------------------------------------------------------------------

class SupertonicPool {
public:
    static SupertonicPool& Instance() {
        static SupertonicPool instance;
        return instance;
    }

    // Ensure context is created. Thread-safe, idempotent.
    // Caches the initial modelPath/threads; subsequent calls with different
    // values fail fast to avoid silently ignoring parameter changes.
    bool EnsureContext(const std::string& modelPath, int threads) {
        std::lock_guard<std::mutex> lock(ctxMutex_);
        if (ctx_) {
            if (modelPath != initialModelPath_ || threads != initialThreads_) {
                LogE("EnsureContext called with different modelPath/threads after context already created");
                return false;
            }
            return true;
        }

        LogI("Creating supertonic context: modelPath=" + modelPath + " threads=" + std::to_string(threads));
        initialModelPath_ = modelPath;
        initialThreads_ = threads;
        ctx_ = SupertonicNative::Instance().Create(modelPath.c_str(), threads);
        if (!ctx_) {
            LogE("supertonic_create failed");
            return false;
        }
        return true;
    }

    // Atomic style-load + synthesize + copy-out. Always holds exclusive lock
    // over the entire sequence so that ctx->result_buf is never overwritten
    // by another thread while we are still reading from it.
    // Returns SUPERTONIC_OK on success and fills out_pcm / out_sample_rate.
    int SynthesizeCopy(const std::string& stylePath,
                                const char* text, const char* lang,
                                int steps, float speed,
                                std::vector<float>& out_pcm,
                                int& out_sample_rate) {
        std::unique_lock<std::shared_mutex> writeLock(styleMutex_);
        if (stylePath != cachedStylePath_) {
            std::lock_guard<std::mutex> ctxLock(ctxMutex_);
            int rc = SupertonicNative::Instance().LoadStyle(
                ctx_, stylePath.c_str());
            if (rc != SUPERTONIC_OK) {
                LogE("supertonic_load_style failed: " + stylePath);
                return rc;
            }
            cachedStylePath_ = stylePath;
        }
        supertonic_audio_chunk chunk{};
        int rc = SupertonicNative::Instance().Synthesize(
            ctx_, text, lang, steps, speed, &chunk);
        if (rc == SUPERTONIC_OK && chunk.samples && chunk.num_samples > 0) {
            out_pcm.assign(chunk.samples, chunk.samples + chunk.num_samples);
            out_sample_rate = chunk.sample_rate;
        }
        SupertonicNative::Instance().FreeResult(&chunk);
        return rc;
    }

    // Acquire a concurrency slot (blocks if at max). Returns true when slot acquired.
    void AcquireSlot(int maxConcurrent) {
        int limit = std::max(1, maxConcurrent);
        std::unique_lock<std::mutex> lock(slotMutex_);
        slotCv_.wait(lock, [&] { return activeCount_ < limit; });
        ++activeCount_;
    }

    void ReleaseSlot() {
        {
            std::lock_guard<std::mutex> lock(slotMutex_);
            --activeCount_;
        }
        slotCv_.notify_one();
    }

    void FreeResult(supertonic_audio_chunk* chunk) {
        SupertonicNative::Instance().FreeResult(chunk);
    }

private:
    SupertonicPool() = default;
    ~SupertonicPool() {
        if (ctx_) SupertonicNative::Instance().Free(ctx_);
    }
    SupertonicPool(const SupertonicPool&) = delete;
    SupertonicPool& operator=(const SupertonicPool&) = delete;

    std::mutex ctxMutex_;
    std::shared_mutex styleMutex_;
    supertonic_context* ctx_ = nullptr;
    std::string cachedStylePath_;
    std::string initialModelPath_;
    int initialThreads_ = 0;

    std::mutex slotMutex_;
    std::condition_variable slotCv_;
    int activeCount_ = 0;
};

// ---------------------------------------------------------------------------
// PCM helpers
// ---------------------------------------------------------------------------

static std::vector<int16_t> Resample(const int16_t* in, int inCount,
                                      int inRate, int outRate) {
    if (inRate == outRate) return std::vector<int16_t>(in, in + inCount);
    int outCount = std::max(1, static_cast<int>(static_cast<int64_t>(inCount) * outRate / inRate));
    std::vector<int16_t> out(outCount);
    for (int i = 0; i < outCount; ++i) {
        double srcPos = static_cast<double>(i) * inRate / outRate;
        int lo = static_cast<int>(srcPos);
        double frac = srcPos - lo;
        int hi = std::min(lo + 1, inCount - 1);
        out[i] = static_cast<int16_t>(in[lo] * (1.0 - frac) + in[hi] * frac);
    }
    return out;
}

static void ApplyVolume(std::vector<int16_t>& chunk, double volume) {
    if (volume >= 1.0) return;
    double vol = std::max(0.0, std::min(1.0, volume));
    for (auto& s : chunk)
        s = static_cast<int16_t>(s * vol);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

bool SupertonicTTS::SynthesizeToQueue(
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
    PCMQueue& queue)
{
    LogI("text_len=" + std::to_string(text.size()));
    LogI("model=" + modelPath + " style=" + stylePath + " lang=" + lang);

    // Load DLL (no-op if already loaded)
    if (!dllDir.empty())
        SupertonicNative::Instance().Load(dllDir);

    if (!SupertonicNative::Instance().Available()) {
        LogE("supertonic.dll not available");
        queue.MarkDone();
        return false;
    }

    auto& pool = SupertonicPool::Instance();

    // Ensure context is created
    if (!pool.EnsureContext(modelPath, threads)) {
        queue.MarkDone();
        return false;
    }

    // Acquire concurrency slot (blocks if at max)
    pool.AcquireSlot(maxConcurrent);

    // Atomic style-load + synthesize + copy-out. The float PCM is copied
    // under the style mutex, so ctx->result_buf is protected against
    // concurrent overwrites by another slot.
    std::vector<float> floatPcm;
    int sampleRate = 0;
    int rc = pool.SynthesizeCopy(
        stylePath, text.c_str(), lang.c_str(), totalSteps, speed,
        floatPcm, sampleRate);

    if (rc != SUPERTONIC_OK || floatPcm.empty()) {
        pool.ReleaseSlot();
        LogE("supertonic_synthesize failed");
        queue.MarkDone();
        return false;
    }

    if (sampleRate <= 0) sampleRate = 44100;

    // Convert float32 → int16 (local copy, no race)
    std::vector<int16_t> pcm(floatPcm.size());
    for (size_t i = 0; i < floatPcm.size(); ++i) {
        float s = floatPcm[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        pcm[i] = static_cast<int16_t>(s * static_cast<float>(
            std::numeric_limits<int16_t>::max()));
    }

    pool.ReleaseSlot();

    // Resample to 16kHz
    if (sampleRate != 16000)
        pcm = Resample(pcm.data(), static_cast<int>(pcm.size()), sampleRate, 16000);

    ApplyVolume(pcm, volume);

    LogI("synthesis complete, output samples: " + std::to_string(pcm.size()));
    queue.Push(std::move(pcm));
    queue.MarkDone();
    return true;
}

} // namespace HoundTTS
