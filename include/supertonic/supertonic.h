#ifndef SUPERTONIC_H_
#define SUPERTONIC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#  ifdef SUPERTONIC_BUILDING_DLL
#    define SUPERTONIC_API __declspec(dllexport)
#  else
#    define SUPERTONIC_API __declspec(dllimport)
#  endif
#else
#  define SUPERTONIC_API
#endif

#define SUPERTONIC_OK   0
#define SUPERTONIC_ERR -1

/**
 * \brief Opaque TTS context (holds ORT env, sessions, text processor, cached style).
 */
typedef struct supertonic_context supertonic_context;

/**
 * \brief Audio output from synthesis.
 *
 * samples points to float32 PCM in [-1, 1].
 * Valid until the next supertonic_synthesize call on the same context,
 * or until supertonic_free_result / supertonic_free is called.
 */
typedef struct supertonic_audio_chunk {
    const float* samples;
    size_t       num_samples;
    int          sample_rate;
} supertonic_audio_chunk;

/**
 * \brief Create a Supertonic TTS context.
 *
 * \param onnx_dir   Directory containing the ONNX models
 *                   (duration_predictor.onnx, text_encoder.onnx,
 *                    vector_estimator.onnx, vocoder.onnx,
 *                    tts.json, unicode_indexer.json).
 * \param num_threads  ORT intra-op thread count (0 = ORT default).
 *
 * \return Context pointer, or NULL on failure.
 *
 * \note Ort::Env is lazily initialized on first call to avoid
 *       DllMain loader-lock issues.
 */
SUPERTONIC_API supertonic_context* supertonic_create(const char* onnx_dir, int num_threads);

/**
 * \brief Free a Supertonic TTS context and all associated resources.
 */
SUPERTONIC_API void supertonic_free(supertonic_context* ctx);

/**
 * \brief Load (or replace) the voice style used for subsequent synthesis.
 *
 * \param ctx             Context.
 * \param style_json_path Path to a voice style JSON file (e.g. M1.json).
 *
 * \return SUPERTONIC_OK on success, SUPERTONIC_ERR on failure.
 */
SUPERTONIC_API int supertonic_load_style(supertonic_context* ctx, const char* style_json_path);

/**
 * \brief Synthesize text to audio.
 *
 * supertonic_load_style must be called at least once before this.
 *
 * \param ctx         Context.
 * \param text        UTF-8 text to synthesize.
 * \param lang        Language code (e.g. "en", "ko", "ja").
 * \param total_steps Denoising steps (e.g. 8).
 * \param speed       Speech speed multiplier (e.g. 1.05).
 * \param out         [out] Filled with audio data on success.
 *
 * \return SUPERTONIC_OK on success, SUPERTONIC_ERR on failure.
 */
SUPERTONIC_API int supertonic_synthesize(supertonic_context* ctx,
                          const char* text, const char* lang,
                          int total_steps, float speed,
                          supertonic_audio_chunk* out);

/**
 * \brief Clear an audio chunk returned by supertonic_synthesize.
 *
 * Does NOT free the underlying sample memory. The sample buffer is owned by
 * ctx->result_buf and its lifetime is tied to the supertonic_context; it is
 * released when the context is freed via supertonic_free, or overwritten on
 * the next supertonic_synthesize call on the same context.
 *
 * This function only zeroes the supertonic_audio_chunk fields
 * (sets chunk->samples = NULL, chunk->num_samples = 0, chunk->sample_rate = 0).
 * Safe to call with a NULL or zeroed chunk.
 */
SUPERTONIC_API void supertonic_free_result(supertonic_audio_chunk* chunk);

#ifdef __cplusplus
}
#endif

#endif /* SUPERTONIC_H_ */
