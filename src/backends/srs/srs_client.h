#pragma once

#ifndef HOUNDTTS_SRS_CLIENT_H
#define HOUNDTTS_SRS_CLIENT_H

#include "audio_queue.h"
#include "srs_types.h"
#include "../../session.h"
#include "srs_backend.h"

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
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

    // Two-step handshake: send SYNC (MsgType=2), read server response,
    // conditionally send EAM password if server has EAM enabled.
    // eamPassword is the password for the desired coalition (empty = don't send).
    bool Handshake(int coalition, const std::string& name,
                   const std::vector<FreqMod>& freqs,
                   const std::string& eamPassword = "");

    // Send a UDP ping (22-byte GUID) to keep the connection alive.
    void SendPing();

    // Stream Opus frames from queue over UDP using a 40ms multimedia timer.
    // Blocks until the queue is drained and marked done.
    // Sends a silence frame if the queue is momentarily empty but not done.
    // session  : if non-null, checks session->alive to stop early (kill signal).
    // posData  : if non-null, registers sendPositionSync callback for position updates.
    void StreamFromQueue(AudioQueue& queue,
                         const std::vector<FreqMod>& freqs,
                         uint32_t unitId,
                         std::shared_ptr<HoundTTS::Session> session = nullptr,
                         std::shared_ptr<SRSPositionData> posData   = nullptr);

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
    // lat/lon/alt > sentinel values (91/181/-500) are included as LatLngPosition.
    std::string BuildClientInfoJson(int coalition, const std::string& name,
                                    const std::vector<FreqMod>& freqs,
                                    double lat = 91.0,
                                    double lon = 181.0,
                                    double alt = -500.0) const;

    // Build a minimal metadata-only JSON blob for MsgType=UPDATE position pushes.
    // Mirrors the official SRS ClientCoalitionUpdate wire format: no RadioInfo.
    std::string BuildClientMetadataJson(int coalition, const std::string& name,
                                        double lat, double lon, double alt) const;

private:
    // Read one newline-delimited JSON response from TCP (with 5s timeout).
    // Returns empty string on error or timeout.
    std::string ReadLine();

    SOCKET      m_tcp       = INVALID_SOCKET;
    SOCKET      m_udp       = INVALID_SOCKET;
    std::string m_host;
    int         m_port      = 5002;
    std::string m_guid;
    int         m_coalition = 0;
    std::string m_name;
    std::mutex  m_tcpMutex; // serializes all TCP send() calls
};

} // namespace HoundTTS

#endif // HOUNDTTS_SRS_CLIENT_H
