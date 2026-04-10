#pragma once

#ifndef HOUNDTTS_GENERATORS_TONE_H
#define HOUNDTTS_GENERATORS_TONE_H

#include "../../backends/pcm_queue.h"
#include "../../session.h"
#include <memory>

namespace HoundTTS {

// Generates a fixed-frequency sine-wave tone and pushes it into queue.
// durationS : length in seconds (>0; defaults to 2 if <=0)
// volume    : 0.0–1.0
// Calls queue->MarkDone() before returning.
void GenerateTone(std::shared_ptr<PCMQueue> queue,
                  std::shared_ptr<Session>  session,
                  double durationS = 2.0,
                  float  freqHz    = 440.0f,
                  float  volume    = 1.0f);

} // namespace HoundTTS

#endif // HOUNDTTS_GENERATORS_TONE_H
