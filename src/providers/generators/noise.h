#pragma once

#ifndef HOUNDTTS_GENERATORS_NOISE_H
#define HOUNDTTS_GENERATORS_NOISE_H

#include "../../backends/pcm_queue.h"
#include "../../session.h"
#include <memory>
#include <string>
#include <cstdint>

namespace HoundTTS {

// Generates noise PCM into queue.
// noiseType : "white" | "chirp" | "harsh" | "jam"
// seed      : RNG seed (use different values per jammer for variety)
// volume    : 0.0–1.0
// duration  : seconds to run (<=0 = continuous until session->alive becomes false)
// Calls queue->MarkDone() before returning.
void GenerateNoise(std::shared_ptr<PCMQueue> queue,
                   std::shared_ptr<Session>  session,
                   const std::string&        noiseType,
                   uint32_t                  seed,
                   float                     volume,
                   double                    duration = 0.0);

} // namespace HoundTTS

#endif // HOUNDTTS_GENERATORS_NOISE_H
