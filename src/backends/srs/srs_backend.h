#pragma once

#ifndef HOUNDTTS_SRS_BACKEND_H
#define HOUNDTTS_SRS_BACKEND_H

#include "backend.h"

namespace HoundTTS {

// ITTSBackend implementation that transmits over the SRS protocol.
// Receives 16kHz mono PCM from the shared TTSPipeline, Opus-encodes it,
// then streams over the SRS TCP/UDP protocol.
class SRSBackend : public ITTSBackend {
public:
    SRSBackend() = default;
    ~SRSBackend() override = default;

    // Non-blocking: spawns a detached thread and returns immediately.
    bool Transmit(std::shared_ptr<PCMQueue> pcm,
                  const TransmitParams& params) override;
};

} // namespace HoundTTS

#endif // HOUNDTTS_SRS_BACKEND_H
