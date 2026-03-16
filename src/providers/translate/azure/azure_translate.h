#pragma once

#ifndef HOUNDTTS_AZURE_TRANSLATE_H
#define HOUNDTTS_AZURE_TRANSLATE_H

#include <string>

namespace HoundTTS {

// Translates text via Azure AI Translator (Cognitive Services).
// Reuses the same Azure credentials as Azure TTS ([Azure] section in credentials INI).
class AzureTranslate {
public:
    // Translate text to the given language.
    // key:      Azure Cognitive Services subscription key
    // region:   Azure region e.g. "eastus"
    // language: target language name e.g. "German" or ISO 639-1 code e.g. "de"
    // text:     text to translate
    // Returns:  translated text, or empty string on failure (error is logged).
    static std::string Translate(
        const std::string& text,
        const std::string& language,
        const std::string& key,
        const std::string& region);
};

} // namespace HoundTTS

#endif // HOUNDTTS_AZURE_TRANSLATE_H
