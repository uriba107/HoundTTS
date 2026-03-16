#pragma once

#ifndef HOUNDTTS_GOOGLE_TRANSLATE_H
#define HOUNDTTS_GOOGLE_TRANSLATE_H

#include <string>

namespace HoundTTS {

// Translates text via Google Cloud Translation API v2.
// Reuses the same service-account JSON as Google Cloud TTS.
// Requires the Cloud Translation API to be enabled on the GCP project.
class GoogleTranslate {
public:
    // Translate text to the given language.
    // credsFile: path to Google service-account JSON (same as [Google] credentials_file)
    // language:  target language name, e.g. "German", "French", "Russian"
    //            (mapped internally to ISO 639-1 code, or pass a code directly)
    // text:      text to translate
    // Returns:   translated text, or empty string on failure (error is logged).
    static std::string Translate(
        const std::string& text,
        const std::string& language,
        const std::string& credsFile);
};

} // namespace HoundTTS

#endif // HOUNDTTS_GOOGLE_TRANSLATE_H
