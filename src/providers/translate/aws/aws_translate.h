#pragma once

#ifndef HOUNDTTS_AWS_TRANSLATE_H
#define HOUNDTTS_AWS_TRANSLATE_H

#include <string>

namespace HoundTTS {

// Translates text via Amazon Translate (TranslateText API).
// Reuses the same AWS credentials as Amazon Polly ([AWS] section in credentials INI).
class AwsTranslate {
public:
    // Translate text to the given language.
    // accessKey: AWS access key ID
    // secretKey: AWS secret access key
    // region:    AWS region e.g. "us-east-1"
    // language:  target language name e.g. "German" or ISO 639-1 code e.g. "de"
    // text:      text to translate
    // Returns:   translated text, or empty string on failure (error is logged).
    static std::string Translate(
        const std::string& text,
        const std::string& language,
        const std::string& accessKey,
        const std::string& secretKey,
        const std::string& region);
};

} // namespace HoundTTS

#endif // HOUNDTTS_AWS_TRANSLATE_H
