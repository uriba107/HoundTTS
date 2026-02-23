#include "audio_queue.h"
#include <opus/opus.h>
#include "srs_client.h"
#include "utils.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#include <windows.h>

#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

namespace HoundTTS {

static const char* kTag = "HoundTTS/SRSClient";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// SRS protocol constants
// ---------------------------------------------------------------------------
static const char* kSRSVersion = "2.1.0.2";

// UDP voice packet fixed-segment sizes (from skyeye/pkg/simpleradio/voice/packet.go)
static const int kHeaderSize    = 6;   // PacketLength(2) + AudioSegLen(2) + FreqSegLen(2)
static const int kFreqEntrySize = 10;  // float64(8) + modulation(1) + encryption(1)
static const int kFixedSize     = 57;  // UnitID(4) + PacketID(8) + Hops(1) + RelayGUID(22) + OriginGUID(22)

// ---------------------------------------------------------------------------
// Timer callback state (passed via dwUser as pointer)
// ---------------------------------------------------------------------------
struct TimerState {
    AudioQueue*                  queue;
    const std::vector<FreqMod>*  freqs;
    uint32_t                     unitId;
    const std::string*           guid;
    SOCKET                       udpSock;
    sockaddr_in                  udpAddr;
    std::atomic<uint64_t>        packetId{0};
    std::atomic<bool>            finished{false};
    // Pre-encoded silence frame (sent when queue is momentarily empty)
    std::vector<uint8_t>         silenceFrame;
};

static void CALLBACK TimerProc(UINT, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR) {
    auto* s = reinterpret_cast<TimerState*>(dwUser);
    if (s->finished.load()) return;

    bool done = false;
    auto frame = s->queue->Pop(0, &done);

    if (frame.empty()) {
        if (done) {
            s->finished.store(true);
            return;
        }
        // Queue momentarily empty but producer still running — send silence
        frame = s->silenceFrame;
    }

    auto packet = SRSClient::BuildVoicePacket(
        frame, *s->freqs, s->unitId,
        s->packetId.fetch_add(1), *s->guid);

    sendto(s->udpSock,
           reinterpret_cast<const char*>(packet.data()),
           static_cast<int>(packet.size()),
           0,
           reinterpret_cast<const sockaddr*>(&s->udpAddr),
           sizeof(s->udpAddr));
}

// ---------------------------------------------------------------------------
// Helpers: little-endian write
// ---------------------------------------------------------------------------
static void WriteLE16(uint8_t* buf, uint16_t v) {
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
}
static void WriteLE32(uint8_t* buf, uint32_t v) {
    buf[0] =  v        & 0xFF;
    buf[1] = (v >>  8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
}
static void WriteLE64(uint8_t* buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) buf[i] = (v >> (8 * i)) & 0xFF;
}
static void WriteDouble(uint8_t* buf, double v) {
    // IEEE 754 double — copy raw bytes (little-endian on x86)
    std::memcpy(buf, &v, 8);
}

// ---------------------------------------------------------------------------
// SRSClient
// ---------------------------------------------------------------------------
SRSClient::SRSClient() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    m_guid = GenerateGUID();
}

SRSClient::~SRSClient() {
    Disconnect();
    WSACleanup();
}

bool SRSClient::Connect(const std::string& host, int port) {
    m_host = host;
    m_port = port;

    // --- TCP ---
    m_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_tcp == INVALID_SOCKET) {
        LogE("TCP socket() failed WSA=" + std::to_string(WSAGetLastError()));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(m_tcp, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        LogE("TCP connect() failed host=" + host + " port=" + std::to_string(port) +
             " WSA=" + std::to_string(WSAGetLastError()));
        return false;
    }

    // --- UDP ---
    m_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udp == INVALID_SOCKET) {
        LogE("UDP socket() failed WSA=" + std::to_string(WSAGetLastError()));
        return false;
    }

    LogI("Connected to " + host + ":" + std::to_string(port));
    return true;
}

bool SRSClient::Handshake(int coalition, const std::string& name,
                           const std::vector<FreqMod>& freqs) {
    std::string clientInfo = BuildClientInfoJson(coalition, name, freqs);

    // Message type 2 = MessageSync
    std::ostringstream sync;
    sync << R"({"Version":")" << kSRSVersion << R"(","MsgType":2,"Client":)"
         << clientInfo << "}";
    if (!SendTCP(sync.str())) {
        LogE("Handshake MsgType=2 send failed");
        return false;
    }

    // Message type 7 = MessageExternalAWACSModePassword (empty password)
    std::ostringstream awacs;
    awacs << R"({"Version":")" << kSRSVersion
          << R"(","MsgType":7,"ExternalAWACSModePassword":"","Client":)"
          << clientInfo << "}";
    if (!SendTCP(awacs.str())) {
        LogE("Handshake MsgType=7 send failed");
        return false;
    }

    LogI("Handshake OK coalition=" + std::to_string(coalition) + " name=" + name);
    return true;
}

void SRSClient::SendPing() {
    // UDP ping = raw GUID bytes (22 ASCII chars)
    std::vector<uint8_t> ping(m_guid.begin(), m_guid.end());

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<u_short>(m_port));
    inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);

    sendto(m_udp,
           reinterpret_cast<const char*>(ping.data()),
           static_cast<int>(ping.size()),
           0,
           reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr));
}

void SRSClient::StreamFromQueue(AudioQueue& queue,
                                 const std::vector<FreqMod>& freqs,
                                 uint32_t unitId) {
    // Build a silence Opus frame (used when queue is momentarily empty)
    // A single Opus frame of silence is just encoding 640 zero samples.
    // We pre-encode it here using a temporary encoder.
    std::vector<uint8_t> silenceFrame;
    {
        int err = 0;
        OpusEncoder* enc = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
        if (enc) {
            std::vector<int16_t> zeros(640, 0);
            std::vector<uint8_t> buf(4000);
            int len = opus_encode(enc, zeros.data(), 640, buf.data(), 4000);
            if (len > 0) { buf.resize(len); silenceFrame = std::move(buf); }
            opus_encoder_destroy(enc);
        }
    }
    if (silenceFrame.empty()) {
        // Fallback: DTX-style empty packet
        silenceFrame = {0xF8, 0xFF, 0xFE};
    }

    // Set up UDP destination
    sockaddr_in udpAddr{};
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port   = htons(static_cast<u_short>(m_port));
    inet_pton(AF_INET, m_host.c_str(), &udpAddr.sin_addr);

    // Send initial UDP ping so the server knows we exist
    sendto(m_udp,
           reinterpret_cast<const char*>(m_guid.data()),
           static_cast<int>(m_guid.size()),
           0,
           reinterpret_cast<const sockaddr*>(&udpAddr),
           sizeof(udpAddr));

    // Wait for startup buffer (5 frames = ~200ms) before starting timer
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (queue.Size() < 5 && !queue.IsDone()) {
        if (std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Set up timer state
    TimerState state;
    state.queue        = &queue;
    state.freqs        = &freqs;
    state.unitId       = unitId;
    state.guid         = &m_guid;
    state.udpSock      = m_udp;
    state.udpAddr      = udpAddr;
    state.silenceFrame = silenceFrame;
    state.finished.store(false);

    timeBeginPeriod(1);
    MMRESULT timerId = timeSetEvent(
        40, 0,
        TimerProc,
        reinterpret_cast<DWORD_PTR>(&state),
        TIME_PERIODIC);

    // Wait until timer callback signals done, sending TCP pings every 15s
    auto lastPing = std::chrono::steady_clock::now();
    while (!state.finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        if (now - lastPing >= std::chrono::seconds(15)) {
            std::string ping = std::string(R"({"Version":")") + kSRSVersion +
                               R"(","MsgType":1,"Client":{"ClientGuid":")" +
                               m_guid + R"(","Name":"","Coalition":0,)" +
                               R"("RadioInfo":{"radios":[],"unit":"","unitId":0,)" +
                               R"("iff":{"control":0,"mode1":0,"mode2":0,"mode3":0,"mode4":false,"mic":0,"status":0},)" +
                               R"("ambient":{"vol":0,"abType":""}}}})" ;
            SendTCP(ping);
            lastPing = now;
        }
    }

    if (timerId) timeKillEvent(timerId);
    timeEndPeriod(1);
}

void SRSClient::Disconnect() {
    if (m_tcp != INVALID_SOCKET) {
        closesocket(m_tcp);
        m_tcp = INVALID_SOCKET;
    }
    if (m_udp != INVALID_SOCKET) {
        closesocket(m_udp);
        m_udp = INVALID_SOCKET;
    }
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> SRSClient::BuildVoicePacket(
    const std::vector<uint8_t>& opusFrame,
    const std::vector<FreqMod>& freqs,
    uint32_t unitId,
    uint64_t packetId,
    const std::string& guid)
{
    uint16_t audioLen = static_cast<uint16_t>(opusFrame.size());
    uint16_t freqLen  = static_cast<uint16_t>(freqs.size() * kFreqEntrySize);
    uint16_t totalLen = static_cast<uint16_t>(
        kHeaderSize + audioLen + freqLen + kFixedSize);

    std::vector<uint8_t> pkt(totalLen, 0);
    uint8_t* p = pkt.data();

    // Header
    WriteLE16(p,     totalLen);  p += 2;
    WriteLE16(p,     audioLen);  p += 2;
    WriteLE16(p,     freqLen);   p += 2;

    // Audio bytes
    std::memcpy(p, opusFrame.data(), audioLen);
    p += audioLen;

    // Frequencies
    for (const auto& fm : freqs) {
        WriteDouble(p, fm.freqHz);    p += 8;
        *p++ = static_cast<uint8_t>(fm.modulation);
        *p++ = fm.encrypt ? fm.encKey : 0;
    }

    // Fixed segment
    WriteLE32(p, unitId);   p += 4;
    WriteLE64(p, packetId); p += 8;
    *p++ = 0; // Hops = 0

    // RelayGUID (22 bytes, zero-padded if shorter)
    std::memset(p, 0, 22);
    size_t gLen = std::min(guid.size(), (size_t)22);
    std::memcpy(p, guid.data(), gLen);
    p += 22;

    // OriginGUID (same as RelayGUID for non-relay)
    std::memset(p, 0, 22);
    std::memcpy(p, guid.data(), gLen);
    // p += 22; // last field, no need to advance

    return pkt;
}

void SRSClient::SendUDP(const std::vector<uint8_t>& data) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<u_short>(m_port));
    inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    sendto(m_udp,
           reinterpret_cast<const char*>(data.data()),
           static_cast<int>(data.size()),
           0,
           reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr));
}

bool SRSClient::SendTCP(const std::string& json) {
    std::string msg = json + "\n";
    int sent = send(m_tcp, msg.c_str(), static_cast<int>(msg.size()), 0);
    return sent == static_cast<int>(msg.size());
}

std::string SRSClient::BuildClientInfoJson(int coalition, const std::string& name,
                                            const std::vector<FreqMod>& freqs) const {
    // Build radios array — field names must match SRS C# [JsonProperty] names (lowercase)
    std::ostringstream radios;
    radios << "[";
    for (size_t i = 0; i < freqs.size(); ++i) {
        if (i > 0) radios << ",";
        radios << std::fixed << std::setprecision(1);
        radios << R"({"freq":)"       << freqs[i].freqHz
               << R"(,"modulation":)" << freqs[i].modulation
               << R"(,"enc":)"        << (freqs[i].encrypt ? "true" : "false")
               << R"(,"encKey":)"     << static_cast<int>(freqs[i].encKey)
               << R"(,"secFreq":0.0)"
               << R"(,"retransmit":false)"
               << "}";
    }
    radios << "]";

    std::ostringstream ci;
    ci << "{"
       << R"("ClientGuid":")" << m_guid << "\","
       << R"("Name":")"       << name   << "\","
       << R"("Seat":0,)"
       << R"("Coalition":)"   << coalition << ","
       << R"("AllowRecord":true,)"
       << R"("RadioInfo":{"radios":)" << radios.str()
       << R"(,"unit":"HoundTTS")"
       << R"(,"unitId":100000002)"
       << R"(,"iff":{"control":2,"mode1":-1,"mode2":-1,"mode3":-1,"mode4":false,"mic":-1,"status":0})"
       << R"(,"ambient":{"vol":1.0,"abType":""}})"
       << "}";
    return ci.str();
}

} // namespace HoundTTS
