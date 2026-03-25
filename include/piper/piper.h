#ifndef PIPER_H_
#define PIPER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIPER_OK 0
#define PIPER_DONE 1
#define PIPER_ERR_GENERIC -1

/**
 * \brief Text-to-speech synthesizer.
 */
typedef struct piper_synthesizer piper_synthesizer;

/**
 * \brief Chunk of synthesized audio samples.
 */
typedef struct piper_audio_chunk {
  const float *samples;
  size_t num_samples;
  int sample_rate;
  bool is_last;
  const uint32_t *phonemes;
  size_t num_phonemes;
  const int *phoneme_ids;
  size_t num_phoneme_ids;
  const int *alignments;
  size_t num_alignments;
} piper_audio_chunk;

/**
 * \brief Options for synthesis.
 */
typedef struct piper_synthesize_options {
  int speaker_id;
  float length_scale;
  float noise_scale;
  float noise_w_scale;
} piper_synthesize_options;

piper_synthesizer *piper_create(const char *model_path, const char *config_path,
                                const char *espeak_data_path);

void piper_free(piper_synthesizer *synth);

piper_synthesize_options
piper_default_synthesize_options(piper_synthesizer *synth);

int piper_synthesize_start(piper_synthesizer *synth, const char *text,
                           const piper_synthesize_options *options);

int piper_synthesize_next(piper_synthesizer *synth, piper_audio_chunk *chunk);

#ifdef __cplusplus
}
#endif

#endif // PIPER_H_
