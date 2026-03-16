#pragma once

#ifndef HOUNDTTS_OPENAI_CHAT_H
#define HOUNDTTS_OPENAI_CHAT_H

#include <string>

namespace HoundTTS {

// Sends a chat completion request to an OpenAI-compatible endpoint.
// Used for translation; designed to be extensible for other chat tasks.
class OpenAIChat {
public:
    // Translate text to the given language via the chat completions API.
    // endpoint: Base URL, e.g. "https://api.openai.com" (same as TTS endpoint)
    //           /v1/chat/completions is always appended.
    // apiKey:   Bearer token (may be empty for local deployments)
    // model:    Chat model name, e.g. "gpt-4o-mini"
    // language: Target language name, e.g. "Hebrew", "French"
    // text:     The text to translate
    // Returns:  Translated text, or empty string on failure (error is logged).
    static std::string Translate(
        const std::string& text,
        const std::string& language,
        const std::string& endpoint,
        const std::string& apiKey,
        const std::string& model);
};

} // namespace HoundTTS

#endif // HOUNDTTS_OPENAI_CHAT_H
