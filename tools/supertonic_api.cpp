// supertonic_api.cpp — C API wrapper around Supertonic's helper.h classes.
// Compiled into supertonic.dll. No C++ types cross the DLL boundary.
//
// Ort::Env is lazily initialized (not in DllMain) to avoid loader-lock.

#include "supertonic/supertonic.h"
#include "helper.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>

// ---------------------------------------------------------------------------
// Lazy Ort::Env — created on first supertonic_create(), destroyed at unload.
// ---------------------------------------------------------------------------
static std::mutex g_env_mutex;
static std::unique_ptr<Ort::Env> g_env;

static Ort::Env& GetEnv() {
    std::lock_guard<std::mutex> lock(g_env_mutex);
    if (!g_env) {
        g_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "supertonic");
    }
    return *g_env;
}

// ---------------------------------------------------------------------------
// Context — owns all per-instance state.
// ---------------------------------------------------------------------------
struct supertonic_context {
    // ONNX models (lifetime tied to context)
    OnnxModels models;
    std::unique_ptr<UnicodeProcessor> text_processor;
    std::unique_ptr<TextToSpeech> tts;
    Ort::MemoryInfo memory_info{nullptr};

    // Cached voice style (loaded via supertonic_load_style)
    bool has_style = false;
    Style style{{}, {}, {}, {}};

    // Last synthesis result buffer (owned here, freed on next call or free)
    std::vector<float> result_buf;

    int num_threads = 0;
};

// ---------------------------------------------------------------------------
// Serializes global tensor-buffer operations (clearTensorBuffers) so
// concurrent supertonic_synthesize calls from different threads do not
// race on the shared state in helper.cpp.
// ---------------------------------------------------------------------------
static std::mutex g_tensor_mutex;

// ---------------------------------------------------------------------------
// C API
// ---------------------------------------------------------------------------

extern "C" {

supertonic_context* supertonic_create(const char* onnx_dir, int num_threads) {
    if (!onnx_dir) return nullptr;

    try {
        auto ctx = std::make_unique<supertonic_context>();
        ctx->num_threads = num_threads;

        Ort::Env& env = GetEnv();

        Ort::SessionOptions opts;
        if (num_threads > 0) {
            opts.SetIntraOpNumThreads(num_threads);
        }
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        std::string dir(onnx_dir);

        // Load config, models, text processor
        Config cfgs = loadCfgs(dir);
        ctx->models = loadOnnxAll(env, dir, opts);
        ctx->text_processor = loadTextProcessor(dir);

        ctx->tts = std::make_unique<TextToSpeech>(
            cfgs,
            ctx->text_processor.get(),
            ctx->models.dp.get(),
            ctx->models.text_enc.get(),
            ctx->models.vector_est.get(),
            ctx->models.vocoder.get()
        );

        ctx->memory_info = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator,
            OrtMemType::OrtMemTypeDefault
        );

        return ctx.release();
    } catch (...) {
        return nullptr;
    }
}

void supertonic_free(supertonic_context* ctx) {
    delete ctx;
}

int supertonic_load_style(supertonic_context* ctx, const char* style_json_path) {
    if (!ctx || !style_json_path) return SUPERTONIC_ERR;

    try {
        std::vector<std::string> paths = { std::string(style_json_path) };
        ctx->style = loadVoiceStyle(paths, false);
        ctx->has_style = true;
        return SUPERTONIC_OK;
    } catch (...) {
        ctx->has_style = false;
        return SUPERTONIC_ERR;
    }
}

int supertonic_synthesize(supertonic_context* ctx,
                          const char* text, const char* lang,
                          int total_steps, float speed,
                          supertonic_audio_chunk* out) {
    if (!ctx || !text || !lang || !out || !ctx->has_style) return SUPERTONIC_ERR;

    std::lock_guard<std::mutex> tensorLock(g_tensor_mutex);
    try {
        // Clear previous tensor buffers (global state in helper.cpp)
        clearTensorBuffers();

        auto result = ctx->tts->call(
            ctx->memory_info,
            std::string(text),
            std::string(lang),
            ctx->style,
            total_steps,
            speed
        );

        int sample_rate = ctx->tts->getSampleRate();

        // Trim to actual duration (guard against empty/negative duration)
        int wav_len = static_cast<int>(result.wav.size());
        if (!result.duration.empty()) {
            int trimmed = static_cast<int>(sample_rate * result.duration[0]);
            wav_len = std::clamp(trimmed, 0, static_cast<int>(result.wav.size()));
        }

        // Store result in context-owned buffer
        ctx->result_buf.assign(result.wav.begin(), result.wav.begin() + wav_len);

        out->samples     = ctx->result_buf.data();
        out->num_samples = ctx->result_buf.size();
        out->sample_rate = sample_rate;

        clearTensorBuffers();
        return SUPERTONIC_OK;
    } catch (...) {
        std::memset(out, 0, sizeof(*out));
        clearTensorBuffers();
        return SUPERTONIC_ERR;
    }
}

void supertonic_free_result(supertonic_audio_chunk* chunk) {
    if (chunk) {
        chunk->samples     = nullptr;
        chunk->num_samples = 0;
        chunk->sample_rate = 0;
    }
    // Actual memory is owned by ctx->result_buf — freed on next call or ctx delete.
}

} // extern "C"
