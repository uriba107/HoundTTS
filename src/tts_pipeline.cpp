#include "tts_pipeline.h"
#include "providers/tts/sapi/sapi_tts.h"
#include "providers/tts/piper/piper_tts.h"
#include "providers/tts/azure/azure_tts.h"
#include "providers/tts/google/google_tts.h"
#include "providers/tts/elevenlabs/elevenlabs_tts.h"
#include "providers/tts/aws/aws_tts.h"
#include "providers/tts/openai/openai_tts.h"
#include "providers/translate/openai/openai_chat.h"
#include "providers/translate/google/google_translate.h"
#include "providers/translate/libretranslate/libretranslate.h"
#include "providers/translate/aws/aws_translate.h"
#include "providers/translate/azure/azure_translate.h"
#include "backends/codecs/opus_encoder.h"
#include "config_reader.h"
#include "utils.h"

#include <thread>
#include <string>
#include <memory>
#include <cmath>
#include <vector>
#include <cstdint>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Pipeline";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

void TTSPipeline::Produce(const TTSRequest& req, std::shared_ptr<PCMQueue> queue) {
    TtsProvider  provider  = req.provider;
    std::string  message   = req.message;
    std::string  voice     = req.voice;
    std::string  speaker   = req.speaker;
    std::string  gender    = req.gender;
    std::string  culture   = req.culture;

    // Apply provider-appropriate default if speed was not set (sentinel -999)
    double speed = req.speed;
    if (speed <= -999.0) {
        // SAPI: 0 = normal rate (-10..+10 scale)
        // All others: 1.0 = normal multiplier
        speed = (provider == TtsProvider::Sapi) ? 0.0 : 1.0;
    }
    double volume = req.volume;

    // Capture config values
    auto& cfg = ConfigReader::Instance();
    std::string piperExe        = cfg.GetPiperExe();
    std::string piperVoicePath  = cfg.GetPiperVoicePath();
    std::string azureKey        = cfg.GetAzureKey();
    std::string azureRegion     = cfg.GetAzureRegion();
    std::string googleCreds     = cfg.GetGoogleCredsFile();
    std::string elevenLabsKey   = cfg.GetElevenLabsKey();
    std::string elevenLabsModel = cfg.GetElevenLabsModelId();
    std::string awsAccessKey    = cfg.GetAwsAccessKey();
    std::string awsSecretKey    = cfg.GetAwsSecretKey();
    std::string awsRegion       = cfg.GetAwsRegion();
    std::string awsPollyEngine  = req.awsPollyEngine.empty() ? cfg.GetAwsPollyEngine() : req.awsPollyEngine;
    std::string kittenEndpoint  = cfg.GetKittenEndpoint();
    std::string openaiKey       = cfg.GetOpenAIKey();
    std::string openaiEndpoint  = cfg.GetOpenAIEndpoint();
    std::string openaiModel     = cfg.GetOpenAIModel();

    // Translation credential resolution
    TranslateProvider xlProvider = req.translateProvider;
    std::string xlLang     = req.translateLanguage;
    std::string xlSrcLang  = req.translateSourceLanguage;
    std::string xlEndpoint;
    std::string xlApiKey;
    std::string xlChatModel;
    std::string xlCredsFile;
    if (xlProvider != TranslateProvider::None) {
        if (xlProvider == TranslateProvider::OpenAI) {
            xlEndpoint  = cfg.GetOpenAIEndpoint();
            xlApiKey    = cfg.GetOpenAIKey();
            xlChatModel = cfg.GetOpenAIChatModel();
        } else if (xlProvider == TranslateProvider::Google) {
            xlCredsFile = cfg.GetGoogleCredsFile();
        } else if (xlProvider == TranslateProvider::LibreTranslate) {
            xlEndpoint = cfg.GetLibreTranslateEndpoint();
            xlApiKey   = cfg.GetLibreTranslateApiKey();
        } else if (xlProvider == TranslateProvider::AWS) {
            xlApiKey    = cfg.GetAwsAccessKey();
            xlChatModel = cfg.GetAwsSecretKey();
            xlEndpoint  = cfg.GetAwsRegion();
        } else if (xlProvider == TranslateProvider::Azure) {
            xlApiKey   = cfg.GetAzureKey();
            xlEndpoint = cfg.GetAzureRegion();
        }
    }

    // --- Optional translate step ---
    if (xlProvider != TranslateProvider::None && !xlLang.empty()) {
        LogI("Translate before TTS: provider=" + std::string(TranslateProviderName(xlProvider)) + " lang=" + xlLang);
        std::string translated;
        if (xlProvider == TranslateProvider::OpenAI) {
            translated = OpenAIChat::Translate(message, xlLang, xlEndpoint, xlApiKey, xlChatModel);
        } else if (xlProvider == TranslateProvider::Google) {
            translated = GoogleTranslate::Translate(message, xlLang, xlCredsFile);
        } else if (xlProvider == TranslateProvider::LibreTranslate) {
            translated = LibreTranslate::Translate(message, xlLang, xlEndpoint, xlApiKey, xlSrcLang);
        } else if (xlProvider == TranslateProvider::AWS) {
            translated = AwsTranslate::Translate(message, xlLang, xlApiKey, xlChatModel, xlEndpoint);
        } else if (xlProvider == TranslateProvider::Azure) {
            translated = AzureTranslate::Translate(message, xlLang, xlApiKey, xlEndpoint);
        } else {
            LogE("Unknown translate provider: " + std::string(TranslateProviderName(xlProvider)) + " — skipping translation");
        }
        if (!translated.empty()) {
            LogI("Translation result: " + translated);
            message = translated;
        } else {
            LogE("Translation failed — using original message");
        }
    }

    // --- TTS synthesis dispatch ---

    if (provider == TtsProvider::Piper) {
        // Resolve model path: piperVoicePath + voice (+ culture prefix if needed)
        auto hasLocalePrefix = [](const std::string& v) {
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

        // Streaming: detach synthesis thread so consumer can start immediately
        std::thread([message, piperModel, piperExe, speaker, speed, volume, queue]() {
            PiperTTS::SynthesizeToQueue(message, piperModel, piperExe, speaker, speed, volume, *queue);
        }).detach();

    } else if (provider == TtsProvider::Azure) {
        std::thread([message, azureKey, azureRegion, voice, culture, gender, speed, volume, queue]() {
            AzureTTS::SynthesizeToQueue(message, azureKey, azureRegion,
                                        voice, culture, gender, speed, volume, *queue);
        }).detach();

    } else if (provider == TtsProvider::Google) {
        // Single-shot REST — runs inline; queue->MarkDone() called inside
        GoogleTTS::SynthesizeToQueue(message, googleCreds,
                                     voice, culture, gender, speed, volume, *queue);

    } else if (provider == TtsProvider::ElevenLabs) {
        std::thread([message, elevenLabsKey, elevenLabsModel, voice, speed, volume, queue]() {
            ElevenLabsTTS::SynthesizeToQueue(message, elevenLabsKey,
                                             elevenLabsModel, voice, speed, volume, *queue);
        }).detach();

    } else if (provider == TtsProvider::AWS) {
        // Single-shot REST — runs inline
        AwsTTS::SynthesizeToQueue(message, awsAccessKey, awsSecretKey,
                                    awsRegion, voice, awsPollyEngine,
                                    culture, gender, speed, volume, *queue);

    } else if (provider == TtsProvider::KittenTTS) {
        // DEPRECATED: reroutes through OpenAI-compatible endpoint
        LogE("[DEPRECATED] Kitten TTS provider will be removed in the next release. "
             "Use provider=\"openai\" with your Kitten TTS Server URL as [OpenAI] endpoint "
             "and model=kitten-tts. See README for migration instructions.");
        double kittenSpeed = (speed <= 0.0) ? 1.1 : speed;
        std::thread([message, kittenEndpoint, voice, kittenSpeed, volume, queue]() {
            OpenAITTS::SynthesizeToQueue(message, kittenEndpoint, "",
                                         "kitten-tts", voice, kittenSpeed, volume, *queue);
        }).detach();

    } else if (provider == TtsProvider::OpenAI) {
        std::thread([message, openaiEndpoint, openaiKey, openaiModel, voice, speed, volume, queue]() {
            OpenAITTS::SynthesizeToQueue(message, openaiEndpoint, openaiKey,
                                         openaiModel, voice, speed, volume, *queue);
        }).detach();

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
        queue->Push(std::move(pcm));
        queue->MarkDone();

    } else {
        // Unknown provider or Sapi — synthesize-all via SAPI
        std::vector<int16_t> pcm = SapiTTS::Synthesize(
            message, voice, gender, culture, speed, volume);
        if (!pcm.empty())
            queue->Push(std::move(pcm));
        queue->MarkDone();
    }
}

} // namespace HoundTTS
