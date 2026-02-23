#pragma once

#ifndef HOUNDTTS_SRS_BACKEND_H
#define HOUNDTTS_SRS_BACKEND_H

#include "backend.h"

namespace HoundTTS {

// ITTSBackend implementation that transmits directly over the SRS protocol.
// Phase 2: SAPI synthesize-all -> Opus encode -> SRS UDP stream.
// Phase 3 (future): Piper streaming path added here.
class SRSBackend : public ITTSBackend {
public:
    SRSBackend() = default;
    ~SRSBackend() override = default;

    // Non-blocking: spawns a detached thread and returns immediately.
    bool TransmitTTS(const TTSRequest& req) override;
};

} // namespace HoundTTS

#endif // HOUNDTTS_SRS_BACKEND_H
