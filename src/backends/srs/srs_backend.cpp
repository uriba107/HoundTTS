#include "srs_backend.h"
#include "srs_types.h"
#include "srs_client.h"
#include "../codecs/opus_encoder.h"
#include "../audio_queue.h"
#include "utils.h"

#include <thread>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace HoundTTS {

static const char* kTag = "HoundTTS/SRSBackend";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

bool SRSBackend::Transmit(std::shared_ptr<PCMQueue> pcm,
                           const TransmitParams& params) {
    std::vector<FreqMod> freqs = ParseFreqMods(params.freqs, params.modulations,
                                               params.encrypt, params.encKey);

    std::string host      = params.host.empty() ? "127.0.0.1" : params.host;
    int         port      = params.port;
    int         coalition = params.coalition;
    std::string name      = params.name;
    uint32_t    unitId    = static_cast<uint32_t>(params.coalition);
    auto        session   = params.session;  // may be nullptr

    // Create and attach SRS-specific position data to the session
    std::shared_ptr<SRSPositionData> posData;
    if (session) {
        posData = std::make_shared<SRSPositionData>();
        posData->lat.store(params.lat);
        posData->lon.store(params.lon);
        posData->alt.store(params.alt);
        { std::lock_guard<std::mutex> lk(session->backendMutex); session->backendData = posData; }
    }

    std::thread([pcm, host, port, freqs, coalition, name, unitId,
                 session, posData]() {
        // Opus-encode PCM as it arrives from the pipeline
        auto opusQueue = std::make_shared<AudioQueue>();

        std::thread([pcm, opusQueue]() {
            OpusFrameEncoder encoder;
            if (!encoder.Init()) {
                LogE("OpusFrameEncoder::Init() failed");
                opusQueue->MarkDone();
                return;
            }
            bool done = false;
            while (!done) {
                std::vector<int16_t> chunk = pcm->Pop(500, &done);
                if (!chunk.empty())
                    encoder.EncodeChunk(chunk.data(), static_cast<int>(chunk.size()), *opusQueue);
            }
            encoder.Flush(*opusQueue);
            opusQueue->MarkDone();
        }).detach();

        // Shared teardown: clears SRS position-sync callback, drains the opus
        // queue, detaches backend data, flags the session dead, and removes it
        // from the manager. Called from every exit path (success or failure)
        // so there is exactly one cleanup sequence.
        auto cleanup = [&]() {
            opusQueue->MarkDone();
            if (posData) { std::lock_guard<std::mutex> lk(posData->syncMutex); posData->sendPositionSync = nullptr; }
            if (session) {
                session->alive.store(false);
                { std::lock_guard<std::mutex> lk(session->backendMutex); session->backendData.reset(); }
                HoundTTS::SessionManager::Instance().Remove(session->id);
            }
        };

        // Connect + handshake + stream
        SRSClient client;
        if (!client.Connect(host, port)) {
            LogE("SRS connect failed host=" + host + " port=" + std::to_string(port));
            cleanup();
            return;
        }
        if (!client.Handshake(coalition, name, freqs)) {
            LogE("SRS handshake failed");
            client.Disconnect();
            cleanup();
            return;
        }
        LogI("Streaming to SRS name=" + name);
        client.StreamFromQueue(*opusQueue, freqs, unitId, session, posData);
        client.Disconnect();
        LogI("Stream done name=" + name);

        cleanup();

    }).detach();

    return true;
}

} // namespace HoundTTS
