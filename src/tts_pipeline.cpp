#include "tts_pipeline.h"
#include "session.h"
#include "providers/generators/tone.h"
#include "providers/generators/noise.h"
#include "providers/tts/sapi/sapi_tts.h"
#include "providers/tts/piper/piper_tts.h"
#include "providers/tts/piper/piper_voice_registry.h"
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
#include "backends/pcm_cache.h"
#include "utils.h"

#include <thread>
#include <string>
#include <memory>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cctype>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Pipeline";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }
// Logger has no Warn level — route warnings through Error so they are never
// suppressed at LEVEL_ERROR. The "[WARNING]" prefix keeps them distinguishable
// from true errors in the log.
static void LogW(const std::string& msg) { Logger::Instance().Error(kTag, "[WARNING] " + msg); }

void TTSPipeline::Produce(const TTSRequest& req, std::shared_ptr<PCMQueue> queue) {
    // --- PCM cache: check for hit before any work ---
    const bool cacheable = (req.message != "__test_tone__");
    uint64_t cacheKey = 0;
    if (cacheable) {
        cacheKey = ComputeTTSRequestKey(req);
        auto cached = PCMCache::Instance().Get(cacheKey);
        if (cached) {
            LogI("PCM cache hit");
            // Replay cached PCM into queue (single chunk, detached thread)
            std::thread([cached, queue]() {
                queue->Push(std::vector<int16_t>(cached->begin(), cached->end()));
                queue->MarkDone();
            }).detach();
            return;
        }
    }

    // --- Cache miss: wrap queue with CachingPCMQueue to capture output,
    // then PaddedPCMQueue to inject 200ms silence before/after speech.
    // Chain: Provider → PaddedPCMQueue → CachingPCMQueue → PCMQueue.
    // effectiveQueue is kept for error/abort paths (bypass padding).
    // On cache hit the padded PCM replays directly — no double-padding.
    std::shared_ptr<PCMQueue> effectiveQueue = queue;
    std::shared_ptr<PCMQueue> dispatchQueue  = queue;
    std::shared_ptr<CachingPCMQueue> cachingQueue;
    if (cacheable) {
        cachingQueue = std::make_shared<CachingPCMQueue>(queue, cacheKey);
        effectiveQueue = cachingQueue;
        dispatchQueue  = std::make_shared<PaddedPCMQueue>(effectiveQueue);
    }

    // Helper: committed only when a caching wrapper is in use. Providers
    // always call PCMQueue::MarkDone() on success AND failure paths, so we
    // must use the provider return value to decide whether to commit the
    // accumulated buffer to PCMCache (avoids caching truncated audio).
    auto finalizeCache = [cachingQueue](bool ok) {
        if (cachingQueue) cachingQueue->Finalize(ok);
    };

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
    std::string piperPath       = cfg.GetPiperPath();
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
        // Ensure Piper subsystem is initialized (lazy init, voice registry scan, etc)
        PiperTTS::EnsureInitialized(piperVoicePath);

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

        // Normalise basename: registry stores names without ".onnx" suffix and
        // compares case-insensitively, so strip the extension (case-insensitive)
        // from what we look up. piperModel still carries the full file path +
        // ".onnx" below for actual synthesis.
        auto hasOnnxSuffix = [](const std::string& s) {
            if (s.size() < 5) return false;
            std::string tail = s.substr(s.size() - 5);
            for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return tail == ".onnx";
        };
        std::string basename = hasOnnxSuffix(modelName)
                                   ? modelName.substr(0, modelName.size() - 5)
                                   : modelName;

        std::string piperModel = piperVoicePath;
        if (!piperModel.empty() && piperModel.back() != '\\' && piperModel.back() != '/')
            piperModel += '\\';
        piperModel += modelName;
        if (!hasOnnxSuffix(piperModel))
            piperModel += ".onnx";

        // --- Piper voice validation with fallback ---
        auto& voiceRegistry = PiperVoiceRegistry::Instance();
        
        // Check if requested voice is available
        if (!voiceRegistry.IsVoiceAvailable(basename)) {
            // Requested voice not found, try fallback
            LogW("Piper voice not found: " + modelName + ". Falling back to default (" + PIPER_DEFAULT_VOICE + ")");
            
            // Check if default voice is available
            if (!voiceRegistry.IsDefaultVoiceAvailable()) {
                // Default voice also not available, abort synthesis
                LogE("Default Piper voice not available. Aborting synthesis.");
                if (cachingQueue) cachingQueue->MarkFailed();
                effectiveQueue->MarkDone();
                finalizeCache(false);
                return;
            }
            
            // Use default voice path
            piperModel = piperVoicePath;
            if (!piperModel.empty() && piperModel.back() != '\\' && piperModel.back() != '/')
                piperModel += '\\';
            piperModel += PIPER_DEFAULT_VOICE;
            if (!hasOnnxSuffix(piperModel))
                piperModel += ".onnx";
        }

        // Streaming: detach synthesis thread so consumer can start immediately
        std::thread([message, piperModel, piperPath, speaker, speed, volume, dispatchQueue, finalizeCache]() {
            bool ok = PiperTTS::SynthesizeToQueue(message, piperModel, piperPath, speaker, speed, volume, *dispatchQueue);
            finalizeCache(ok);
        }).detach();

    } else if (provider == TtsProvider::Azure) {
        std::thread([message, azureKey, azureRegion, voice, culture, gender, speed, volume, dispatchQueue, finalizeCache]() {
            bool ok = AzureTTS::SynthesizeToQueue(message, azureKey, azureRegion,
                                                  voice, culture, gender, speed, volume, *dispatchQueue);
            finalizeCache(ok);
        }).detach();

    } else if (provider == TtsProvider::Google) {
        // Single-shot REST — runs inline; queue->MarkDone() called inside
        bool ok = GoogleTTS::SynthesizeToQueue(message, googleCreds,
                                               voice, culture, gender, speed, volume, *dispatchQueue);
        finalizeCache(ok);

    } else if (provider == TtsProvider::ElevenLabs) {
        std::thread([message, elevenLabsKey, elevenLabsModel, voice, speed, volume, dispatchQueue, finalizeCache]() {
            bool ok = ElevenLabsTTS::SynthesizeToQueue(message, elevenLabsKey,
                                                       elevenLabsModel, voice, speed, volume, *dispatchQueue);
            finalizeCache(ok);
        }).detach();

    } else if (provider == TtsProvider::AWS) {
        // Single-shot REST — runs inline
        bool ok = AwsTTS::SynthesizeToQueue(message, awsAccessKey, awsSecretKey,
                                            awsRegion, voice, awsPollyEngine,
                                            culture, gender, speed, volume, *dispatchQueue);
        finalizeCache(ok);

    } else if (provider == TtsProvider::KittenTTS) {
        // DEPRECATED: reroutes through OpenAI-compatible endpoint
        LogE("[DEPRECATED] Kitten TTS provider will be removed in the next release. "
             "Use provider=\"openai\" with your Kitten TTS Server URL as [OpenAI] endpoint "
             "and model=kitten-tts. See README for migration instructions.");
        double kittenSpeed = (speed <= 0.0) ? 1.1 : speed;
        std::thread([message, kittenEndpoint, voice, kittenSpeed, volume, dispatchQueue, finalizeCache]() {
            bool ok = OpenAITTS::SynthesizeToQueue(message, kittenEndpoint, "",
                                                   "kitten-tts", voice, kittenSpeed, volume, *dispatchQueue);
            finalizeCache(ok);
        }).detach();

    } else if (provider == TtsProvider::OpenAI) {
        std::thread([message, openaiEndpoint, openaiKey, openaiModel, voice, speed, volume, dispatchQueue, finalizeCache]() {
            bool ok = OpenAITTS::SynthesizeToQueue(message, openaiEndpoint, openaiKey,
                                                   openaiModel, voice, speed, volume, *dispatchQueue);
            finalizeCache(ok);
        }).detach();

    } else if (message == "__test_tone__") {
        GenerateTone(queue, nullptr, 2.0, 440.0f, static_cast<float>(volume));

    } else {
        // Unknown provider or Sapi — synthesize-all via SAPI
        std::vector<int16_t> pcm = SapiTTS::Synthesize(
            message, voice, gender, culture, speed, volume);
        bool ok = !pcm.empty();
        if (ok)
            dispatchQueue->Push(std::move(pcm));
        dispatchQueue->MarkDone();
        finalizeCache(ok);
    }
}

void TTSPipeline::ProduceNoise(std::shared_ptr<PCMQueue> queue,
                                std::shared_ptr<Session> session,
                                const std::string& noiseType,
                                uint32_t seed,
                                float volume,
                                double duration) {
    GenerateNoise(queue, session, noiseType, seed, volume, duration);
    LogI("ProduceNoise done");
}

void TTSPipeline::ProduceTone(std::shared_ptr<PCMQueue> queue,
                               std::shared_ptr<Session> session,
                               double duration,
                               float  freqHz,
                               float  volume) {
    GenerateTone(queue, session, duration, freqHz, volume);
    LogI("ProduceTone done");
}

} // namespace HoundTTS
