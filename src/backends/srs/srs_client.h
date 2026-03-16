#pragma once

#ifndef HOUNDTTS_SRS_CLIENT_H
#define HOUNDTTS_SRS_CLIENT_H

#include "../audio_queue.h"
#include "srs_types.h"

#include <string>
#include <vector>
#include <cstdint>
#include <winsock2.h>

namespace HoundTTS {

// Native C++ SRS protocol client.
// Handles TCP control channel (JSON) and UDP voice channel (binary packets).
class SRSClient {
public:
    SRSClient();
    ~SRSClient();

    // Connect TCP + UDP sockets to host:port.
    bool Connect(const std::string& host, int port);

    // Send MessageSync + MessageExternalAWACSModePassword over TCP.
    bool Handshake(int coalition, const std::string& name,
                   const std::vector<FreqMod>& freqs);

    // Send a UDP ping (22-byte GUID) to keep the connection alive.
    void SendPing();

    // Stream Opus frames from queue over UDP using a 40ms multimedia timer.
    // Blocks until the queue is drained and marked done.
    // Sends a silence frame if the queue is momentarily empty but not done.
    void StreamFromQueue(AudioQueue& queue,
                         const std::vector<FreqMod>& freqs,
                         uint32_t unitId);

    void Disconnect();

    // Build a binary UDP voice packet (public so TimerProc can call it).
    static std::vector<uint8_t> BuildVoicePacket(
        const std::vector<uint8_t>& opusFrame,
        const std::vector<FreqMod>& freqs,
        uint32_t unitId,
        uint64_t packetId,
        const std::string& guid);

    // Send raw bytes over UDP.
    void SendUDP(const std::vector<uint8_t>& data);

    // Send a JSON string + newline over TCP.
    bool SendTCP(const std::string& json);

    // Build the ClientInfo JSON blob used in sync/handshake messages.
    std::string BuildClientInfoJson(int coalition, const std::string& name,
                                    const std::vector<FreqMod>& freqs) const;

private:
    SOCKET      m_tcp  = INVALID_SOCKET;
    SOCKET      m_udp  = INVALID_SOCKET;
    std::string m_host;
    int         m_port = 5002;
    std::string m_guid;
};

} // namespace HoundTTS

#endif // HOUNDTTS_SRS_CLIENT_H
