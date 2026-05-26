#pragma once

#ifndef HOUNDTTS_PROVIDER_H
#define HOUNDTTS_PROVIDER_H

#include <algorithm>
#include <cctype>
#include <string>

namespace HoundTTS {

inline std::string NormalizeProviderToken(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    std::string token = s.substr(start, end - start);
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return token;
}

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
    Supertonic,
    Unknown
};

inline TtsProvider ParseTtsProvider(const std::string& s) {
    const std::string token = NormalizeProviderToken(s);

    if (token.empty() || token == "sapi" || token == "win")       return TtsProvider::Sapi;
    if (token == "piper")                                            return TtsProvider::Piper;
    if (token == "azure")                                            return TtsProvider::Azure;
    if (token == "google" || token == "gcloud")                         return TtsProvider::Google;
    if (token == "elevenlabs")                                       return TtsProvider::ElevenLabs;
    if (token == "aws" || token == "polly" || token == "amazon")            return TtsProvider::AWS;
    if (token == "kittentts" || token == "kitten_tts" || token == "kitten") return TtsProvider::KittenTTS;
    if (token == "openai")                                                  return TtsProvider::OpenAI;
    if (token == "supertonic")                                               return TtsProvider::Supertonic;
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
        case TtsProvider::Supertonic: return "supertonic";
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
    const std::string token = NormalizeProviderToken(s);

    if (token.empty())                                           return TranslateProvider::None;
    if (token == "openai")                                       return TranslateProvider::OpenAI;
    if (token == "google" || token == "gcloud")                  return TranslateProvider::Google;
    if (token == "libretranslate" || token == "libre")           return TranslateProvider::LibreTranslate;
    if (token == "aws" || token == "polly" || token == "amazon") return TranslateProvider::AWS;
    if (token == "azure")                                        return TranslateProvider::Azure;
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
