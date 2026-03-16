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

    std::string host     = params.host.empty() ? "127.0.0.1" : params.host;
    int         port     = params.port;
    int         coalition = params.coalition;
    std::string name     = params.name;
    uint32_t    unitId   = static_cast<uint32_t>(params.coalition);

    std::thread([pcm, host, port, freqs, coalition, name, unitId]() {
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

        // Connect + handshake + stream
        SRSClient client;
        if (!client.Connect(host, port)) {
            LogE("SRS connect failed host=" + host + " port=" + std::to_string(port));
            opusQueue->MarkDone();
            return;
        }
        if (!client.Handshake(coalition, name, freqs)) {
            LogE("SRS handshake failed");
            client.Disconnect();
            opusQueue->MarkDone();
            return;
        }
        LogI("Streaming to SRS name=" + name);
        client.StreamFromQueue(*opusQueue, freqs, unitId);
        client.Disconnect();
        LogI("Stream done name=" + name);

    }).detach();

    return true;
}

} // namespace HoundTTS
