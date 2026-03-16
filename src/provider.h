#pragma once

#ifndef HOUNDTTS_PROVIDER_H
#define HOUNDTTS_PROVIDER_H

#include <string>

namespace HoundTTS {

// -------------------------------------------------------------------------
// TTS provider enum — single source of truth for all provider names/aliases.
// -------------------------------------------------------------------------
enum class TtsProvider {
    Sapi,
    Piper,
    Azure,
    Google,
    ElevenLabs,
    AWS,
    KittenTTS,   // deprecated — reroutes through OpenAI endpoint
    OpenAI,
    Unknown
};

inline TtsProvider ParseTtsProvider(const std::string& s) {
    if (s.empty() || s == "sapi" || s == "win")       return TtsProvider::Sapi;
    if (s == "piper")                                  return TtsProvider::Piper;
    if (s == "azure")                                  return TtsProvider::Azure;
    if (s == "google" || s == "gcloud")                return TtsProvider::Google;
    if (s == "elevenlabs")                             return TtsProvider::ElevenLabs;
    if (s == "aws" || s == "polly")                    return TtsProvider::AWS;
    if (s == "kittentts" || s == "kitten_tts" || s == "kitten") return TtsProvider::KittenTTS;
    if (s == "openai")                                 return TtsProvider::OpenAI;
    return TtsProvider::Unknown;
}

inline const char* TtsProviderName(TtsProvider p) {
    switch (p) {
        case TtsProvider::Sapi:       return "sapi";
        case TtsProvider::Piper:      return "piper";
        case TtsProvider::Azure:      return "azure";
        case TtsProvider::Google:     return "google";
        case TtsProvider::ElevenLabs: return "elevenlabs";
        case TtsProvider::AWS:        return "aws";
        case TtsProvider::KittenTTS:  return "kittentts";
        case TtsProvider::OpenAI:     return "openai";
        default:                      return "unknown";
    }
}

// -------------------------------------------------------------------------
// Translation provider enum
// -------------------------------------------------------------------------
enum class TranslateProvider {
    None,            // no translation
    OpenAI,
    Google,
    LibreTranslate,
    AWS,
    Azure,
    Unknown
};

inline TranslateProvider ParseTranslateProvider(const std::string& s) {
    if (s.empty())                                     return TranslateProvider::None;
    if (s == "openai")                                 return TranslateProvider::OpenAI;
    if (s == "google" || s == "gcloud")                return TranslateProvider::Google;
    if (s == "libretranslate" || s == "libre")         return TranslateProvider::LibreTranslate;
    if (s == "aws" || s == "polly")                    return TranslateProvider::AWS;
    if (s == "azure")                                  return TranslateProvider::Azure;
    return TranslateProvider::Unknown;
}

inline const char* TranslateProviderName(TranslateProvider p) {
    switch (p) {
        case TranslateProvider::None:           return "";
        case TranslateProvider::OpenAI:         return "openai";
        case TranslateProvider::Google:         return "google";
        case TranslateProvider::LibreTranslate: return "libretranslate";
        case TranslateProvider::AWS:            return "aws";
        case TranslateProvider::Azure:          return "azure";
        default:                                return "unknown";
    }
}

} // namespace HoundTTS

#endif // HOUNDTTS_PROVIDER_H
