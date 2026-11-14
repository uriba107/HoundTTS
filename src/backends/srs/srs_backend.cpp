#include "srs_backend.h"
#include "srs_types.h"
#include "srs_client.h"
#include "codecs/opus_encoder.h"
#include "audio_queue.h"
#include "utils.h"

#include <thread>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>

namespace HoundTTS {

static const char* kTag = "HoundTTS/SRSBackend";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

// Logging from a catch handler must not throw: building the message allocates,
// and an exception escaping a detached thread calls std::terminate.
static void LogFail(const char* msg, const std::exception* e = nullptr) noexcept {
    try {
        LogE(e ? std::string(msg) + ": " + e->what() : std::string(msg));
    } catch (...) {}
}

bool SRSBackend::Transmit(std::shared_ptr<PCMQueue> pcm,
                           const TransmitParams& params) {
    if (!pcm) {
        LogE("Transmit called with a null PCM queue; aborting");
        return false;
    }

    std::vector<FreqMod> freqs = ParseFreqMods(params.freqs, params.modulations,
                                               params.encrypt, params.encKey);

    std::string host      = params.host.empty() ? "127.0.0.1" : params.host;
    int         port      = params.port;
    int         coalition = params.coalition;
    std::string name      = params.name;
    uint32_t    unitId    = static_cast<uint32_t>(params.coalition);
    auto        session   = params.session;  // may be nullptr

    // Select EAM password for the requested coalition
    std::string eamPassword;
    if (coalition == 2)
        eamPassword = params.srsBluePassword;
    else if (coalition == 1)
        eamPassword = params.srsRedPassword;

    // Create and attach SRS-specific position data to the session
    std::shared_ptr<SRSPositionData> posData;
    if (session) {
        posData = std::make_shared<SRSPositionData>();
        posData->lat.store(params.lat);
        posData->lon.store(params.lon);
        posData->alt.store(params.alt);
        { std::lock_guard<std::mutex> lk(session->backendMutex); session->backendData = posData; }
    }

    SpawnDetached([pcm, host, port, freqs, coalition, name, unitId,
                 session, posData, eamPassword]() {
        // Opus-encode PCM as it arrives from the pipeline
        auto opusQueue = std::make_shared<AudioQueue>();
        // Set by cleanup() so the encoder thread stops consuming PCM when the
        // stream aborts early (e.g. connect/handshake failure). Without it the
        // encoder keeps encoding into a queue nobody drains until the producer
        // finishes, which can grow unbounded.
        auto stopEncoder = std::make_shared<std::atomic<bool>>(false);

        // Shared teardown: clears SRS position-sync callback, drains the opus
        // queue, detaches backend data, flags the session dead, and removes it
        // from the manager. Called from every exit path (success or failure)
        // so there is exactly one cleanup sequence.
        auto cleanup = [&]() {
            // Called from catch handlers too: nothing may escape the handler,
            // or std::terminate takes DCS down.
            try {
                stopEncoder->store(true);
                opusQueue->MarkDone();
                if (posData) { std::lock_guard<std::mutex> lk(posData->syncMutex); posData->sendPositionSync = nullptr; }
                if (session) {
                    session->alive.store(false);
                    { std::lock_guard<std::mutex> lk(session->backendMutex); session->backendData.reset(); }
                    HoundTTS::SessionManager::Instance().Remove(session->id);
                }
            } catch (const std::exception& e) {
                LogFail("SRS cleanup exception", &e);
            } catch (...) {
                LogFail("SRS cleanup unknown exception");
            }
        };

        // An exception escaping this thread would call std::terminate and kill
        // DCS, skipping cleanup() (leaving the session registered). Catch it,
        // log, and still run cleanup.
        try {
            SpawnDetached([pcm, opusQueue, stopEncoder]() {
                try {
                    OpusFrameEncoder encoder;
                    if (!encoder.Init()) {
                        LogE("OpusFrameEncoder::Init() failed");
                    } else {
                        bool done = false;
                        while (!done && !stopEncoder->load()) {
                            std::vector<int16_t> chunk = pcm->Pop(500, &done);
                            if (!chunk.empty())
                                encoder.EncodeChunk(chunk.data(), static_cast<int>(chunk.size()), *opusQueue);
                        }
                        // Don't flush into a queue whose consumer was already
                        // marked done by an early-abort cleanup().
                        if (!stopEncoder->load())
                            encoder.Flush(*opusQueue);
                    }
                } catch (const std::exception& e) {
                    LogFail("Opus encode exception", &e);
                } catch (...) {
                    LogFail("Opus encode unknown exception");
                }
                // Always signal completion to the streamer; best-effort since
                // this runs on a detached thread.
                try { opusQueue->MarkDone(); } catch (...) {}
            });

            // Connect + handshake + stream
            SRSClient client;
            if (!client.Connect(host, port)) {
                LogE("SRS connect failed host=" + host + " port=" + std::to_string(port));
                cleanup();
                return;
            }
            if (!client.Handshake(coalition, name, freqs, eamPassword)) {
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
        } catch (const std::exception& e) {
            LogFail("SRS worker exception", &e);
            cleanup();
        } catch (...) {
            LogFail("SRS worker unknown exception");
            cleanup();
        }

    });

    return true;
}

} // namespace HoundTTS
