#pragma once

#ifndef HOUNDTTS_SRS_BACKEND_H
#define HOUNDTTS_SRS_BACKEND_H

#include "backend.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

namespace HoundTTS {

// ---------------------------------------------------------------------------
// SRSPositionData — SRS-specific extension stored in session->backendData.
// Only meaningful for SRS transmissions; other backends use their own struct.
// ---------------------------------------------------------------------------
struct SRSPositionData {
    // Position — updated by l_updateSession; read by SRS streaming thread
    std::atomic<double> lat{91.0};    // >90  = unset
    std::atomic<double> lon{181.0};   // >180 = unset
    std::atomic<double> alt{-500.0};  // <-499 = unset

    // Last-sent position values and timestamp — for change-detection and heartbeat.
    // Only touched while holding syncMutex.
    double lastSentLat{91.0};
    double lastSentLon{181.0};
    double lastSentAlt{-500.0};
    std::chrono::steady_clock::time_point lastSentAt{};  // zero = never

    // Callback to send a TCP position UPDATE on the active SRS connection.
    // Set by SRSClient when the stream starts; called by l_updateSession.
    // Protected by syncMutex — lock before reading, writing, or invoking.
    std::mutex syncMutex;
    std::function<void()> sendPositionSync;

    SRSPositionData() = default;
    SRSPositionData(const SRSPositionData&) = delete;
    SRSPositionData& operator=(const SRSPositionData&) = delete;
};

// ---------------------------------------------------------------------------
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
