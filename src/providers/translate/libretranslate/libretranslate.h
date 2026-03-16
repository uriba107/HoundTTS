#pragma once

#ifndef HOUNDTTS_LIBRETRANSLATE_H
#define HOUNDTTS_LIBRETRANSLATE_H

#include <string>

namespace HoundTTS {

// Translates text via a LibreTranslate REST API endpoint.
// Works with any self-hosted or public LibreTranslate instance.
// POST /translate  →  {"translatedText":"..."}
class LibreTranslate {
public:
    // Translate text to the given language.
    // endpoint:  Base URL of the LibreTranslate server,
    //            e.g. "http://localhost:5000" or "https://libretranslate.com"
    // apiKey:    Optional API key (leave empty for instances that don't require one)
    // language:        Target language name (e.g. "German") or ISO 639-1 code (e.g. "de")
    // source_language: Source language ISO 639-1 code (e.g. "en"). Defaults to "en" if empty.
    // text:            Text to translate
    // Returns:         Translated text, or empty string on failure (error is logged).
    static std::string Translate(
        const std::string& text,
        const std::string& language,
        const std::string& endpoint,
        const std::string& apiKey,
        const std::string& source_language = "en");
};

} // namespace HoundTTS

#endif // HOUNDTTS_LIBRETRANSLATE_H
