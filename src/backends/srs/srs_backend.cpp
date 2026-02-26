#include "srs_backend.h"
#include "srs_types.h"
#include "srs_client.h"
#include "providers/sapi/sapi_tts.h"
#include "providers/piper/piper_tts.h"
#include "providers/azure/azure_tts.h"
#include "providers/google/google_tts.h"
#include "providers/elevenlabs/elevenlabs_tts.h"
#include "providers/polly/polly_tts.h"
#include "providers/kitten/kitten_tts.h"
#include "opus_encoder.h"
#include "audio_queue.h"
#include "config_reader.h"
#include "utils.h"

#include <thread>
#include <string>
#include <memory>
#include <cmath>
#include <vector>
#include <cstdint>

namespace HoundTTS {

static const char* kTag = "HoundTTS/SRSBackend";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

bool SRSBackend::TransmitTTS(const TTSRequest& req) {
    std::vector<FreqMod> freqs = ParseFreqMods(req.freqs, req.modulations,
                                               req.encrypt,
                                               static_cast<uint8_t>(req.encKey));

    std::string host      = req.srsHost.empty() ? "127.0.0.1" : req.srsHost;
    int         port      = req.srsPort;
    std::string message   = req.message;
    std::string provider  = req.provider;
    std::string voice     = req.voice;
    std::string speaker   = req.speaker;
    std::string gender    = req.gender;
    std::string culture   = req.culture;
    // Apply provider-appropriate default if speed was not set (sentinel -999)
    double      speed     = req.speed;
    if (speed <= -999.0) {
        // SAPI: 0 = normal rate (-10..+10 scale)
        // All others: 1.0 = normal multiplier
        speed = (provider == "sapi" || provider == "win" || provider.empty()) ? 0.0 : 1.0;
    }
    double      volume    = req.volume;
    uint32_t    unitId    = static_cast<uint32_t>(req.coalition);
    int         coalition = req.coalition;
    std::string name      = req.name;

    // Capture config values needed by provider threads
    auto& cfg = ConfigReader::Instance();
    std::string piperExe       = cfg.GetPiperExe();
    std::string piperVoicePath = cfg.GetPiperVoicePath();
    std::string azureKey       = cfg.GetAzureKey();
    std::string azureRegion    = cfg.GetAzureRegion();
    std::string googleCreds    = cfg.GetGoogleCredsFile();
    std::string elevenLabsKey  = cfg.GetElevenLabsKey();
    std::string elevenLabsModel = cfg.GetElevenLabsModelId();
    std::string pollyAccessKey  = cfg.GetPollyAccessKey();
    std::string pollySecretKey  = cfg.GetPollySecretKey();
    std::string pollyRegion     = cfg.GetPollyRegion();
    std::string pollyEngine     = req.pollyEngine.empty() ? cfg.GetPollyEngine() : req.pollyEngine;
    std::string kittenEndpoint  = cfg.GetKittenEndpoint();

    std::thread([host, port, freqs, message, provider, voice, speaker, gender, culture,
                 speed, volume, unitId, coalition, name,
                 piperExe, piperVoicePath,
                 azureKey, azureRegion,
                 googleCreds,
                 elevenLabsKey, elevenLabsModel,
                 pollyAccessKey, pollySecretKey, pollyRegion, pollyEngine,
                 kittenEndpoint]() {

        auto queue = std::make_shared<AudioQueue>();

        if (provider == "piper") {
            // Resolve model path: piperVoicePath + voice (+ culture prefix if needed)
            // If voice already starts with a locale prefix (e.g. en_US-, nl_NL-, en-US-)
            // use it as-is. Otherwise prepend the culture (normalized to underscore form).
            auto hasLocalePrefix = [](const std::string& v) {
                // Match xx_XX- or xx-XX- at position 0 (5-char locale + dash)
                if (v.size() < 6) return false;
                return (std::isalpha((unsigned char)v[0]) &&
                        std::isalpha((unsigned char)v[1]) &&
                        (v[2] == '_' || v[2] == '-') &&
                        std::isupper((unsigned char)v[3]) &&
                        std::isupper((unsigned char)v[4]) &&
                        v[5] == '-');
            };

            std::string modelName;
            if (!culture.empty() && !hasLocalePrefix(voice)) {
                std::string cultureNorm = culture;
                for (char& c : cultureNorm) if (c == '-') c = '_';
                modelName = cultureNorm + "-" + voice;
            } else {
                modelName = voice;
            }

            std::string piperModel = piperVoicePath;
            if (!piperModel.empty() && piperModel.back() != '\\' && piperModel.back() != '/')
                piperModel += '\\';
            piperModel += modelName;
            if (piperModel.size() < 5 || piperModel.substr(piperModel.size()-5) != ".onnx")
                piperModel += ".onnx";

            std::thread([message, piperModel, piperExe, speaker, speed, volume, queue]() {
                PiperTTS::SynthesizeToQueue(message, piperModel, piperExe, speaker, speed, volume, *queue);
            }).detach();

        } else if (provider == "azure") {
            // Chunked HTTP stream — start SRS client concurrently with audio arrival
            std::thread([message, azureKey, azureRegion, voice, culture, gender, speed, volume, queue]() {
                AzureTTS::SynthesizeToQueue(message, azureKey, azureRegion,
                                            voice, culture, gender, speed, volume, *queue);
            }).detach();

        } else if (provider == "google" || provider == "gcloud") {
            // Single-shot REST response — must complete before SRS client starts
            GoogleTTS::SynthesizeToQueue(message, googleCreds,
                                         voice, culture, gender, speed, volume, *queue);

        } else if (provider == "elevenlabs") {
            // WebSocket stream — start SRS client concurrently with audio arrival
            std::thread([message, elevenLabsKey, elevenLabsModel, voice, speed, volume, queue]() {
                ElevenLabsTTS::SynthesizeToQueue(message, elevenLabsKey,
                                                 elevenLabsModel, voice, speed, volume, *queue);
            }).detach();

        } else if (provider == "polly" || provider == "aws") {
            // Single-shot REST response — must complete before SRS client starts
            PollyTTS::SynthesizeToQueue(message, pollyAccessKey, pollySecretKey,
                                        pollyRegion, voice, pollyEngine,
                                        culture, gender, speed, volume, *queue);

        } else if (provider == "kittentts" || provider == "kitten_tts" || provider == "kitten") {
            // Single-shot HTTP POST — must complete before SRS client starts
            // Default speed 1.1 matches the KittenTTS UI default (server default is too slow)
            double kittenSpeed = (speed <= 0.0) ? 1.1 : speed;
            KittenTTS::SynthesizeToQueue(message, kittenEndpoint,
                                         voice, culture, kittenSpeed, volume, *queue);

        } else if (message == "__test_tone__") {
            // Test tone: 440Hz sine at 16kHz, duration = speed (seconds)
            const int   sampleRate = 16000;
            const int   durationS  = (speed > 0.0) ? static_cast<int>(std::ceil(speed)) : 2;
            const float freq440    = 440.0f;
            const int   numSamples = sampleRate * durationS;
            std::vector<int16_t> pcm(numSamples);
            float vol = static_cast<float>(std::max(0.0, std::min(1.0, volume)));
            for (int i = 0; i < numSamples; ++i) {
                float t   = static_cast<float>(i) / sampleRate;
                float val = std::sin(2.0f * 3.14159265f * freq440 * t);
                pcm[i] = static_cast<int16_t>(val * 16000.0f * vol);
            }
            OpusFrameEncoder encoder;
            if (!encoder.Init()) return;
            encoder.EncodeChunk(pcm.data(), static_cast<int>(pcm.size()), *queue);
            encoder.Flush(*queue);
            queue->MarkDone();

        } else if (provider == "sapi" || provider == "win" || provider.empty()) {
            // SAPI (Windows TTS)
            std::vector<int16_t> pcm = SapiTTS::Synthesize(
                message, voice, gender, culture, speed, volume);
            if (pcm.empty()) { queue->MarkDone(); return; }

            OpusFrameEncoder encoder;
            if (!encoder.Init()) { queue->MarkDone(); return; }
            encoder.EncodeChunk(pcm.data(), static_cast<int>(pcm.size()), *queue);
            encoder.Flush(*queue);
            queue->MarkDone();

        } else {
            // Unknown provider — fall back to SAPI (uses system TTS voices)
            std::vector<int16_t> pcm = SapiTTS::Synthesize(
                message, voice, gender, culture, speed, volume);
            if (pcm.empty()) { queue->MarkDone(); return; }

            OpusFrameEncoder encoder;
            if (!encoder.Init()) { queue->MarkDone(); return; }
            encoder.EncodeChunk(pcm.data(), static_cast<int>(pcm.size()), *queue);
            encoder.Flush(*queue);
            queue->MarkDone();
        }

        // Connect + handshake + stream
        SRSClient client;
        if (!client.Connect(host, port)) {
            LogE("SRS connect failed host=" + host + " port=" + std::to_string(port));
            queue->MarkDone(); return;
        }
        if (!client.Handshake(coalition, name, freqs)) {
            LogE("SRS handshake failed");
            client.Disconnect();
            queue->MarkDone();
            return;
        }
        LogI("Streaming provider=" + provider + " name=" + name);
        client.StreamFromQueue(*queue, freqs, unitId);
        client.Disconnect();
        LogI("Stream done provider=" + provider + " name=" + name);

    }).detach();

    return true;
}

} // namespace HoundTTS
