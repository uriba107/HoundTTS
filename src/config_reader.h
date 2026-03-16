#pragma once

#ifndef HOUNDTTS_CONFIG_READER_H
#define HOUNDTTS_CONFIG_READER_H

#include <string>
#include <mutex>

namespace HoundTTS {

// Singleton INI reader for HoundTTS-credentials.ini
// Located at: <writedir>\Config\HoundTTS-credentials.ini
// Call Load() once with writedir; all Get*() are safe to call from any thread.
class ConfigReader {
public:
    static ConfigReader& Instance();

    // Load (or reload) credentials from <writedir>\Config\HoundTTS-credentials.ini
    // Thread-safe. No-op if already loaded from the same writedir.
    void Load(const std::string& writedir);

    // [Piper]
    std::string GetPiperExe() const;
    std::string GetPiperVoicePath() const;

    // [Google]
    std::string GetGoogleCredsFile() const;

    // [Azure]
    std::string GetAzureKey() const;
    std::string GetAzureRegion() const;

    // [ElevenLabs]
    std::string GetElevenLabsKey() const;
    std::string GetElevenLabsModelId() const;

    // [KittenTTS]
    std::string GetKittenEndpoint() const;

    // [OpenAI]
    std::string GetOpenAIKey() const;
    std::string GetOpenAIEndpoint() const;
    std::string GetOpenAIModel() const;
    std::string GetOpenAIChatModel() const;

    // [LibreTranslate]
    std::string GetLibreTranslateEndpoint() const;
    std::string GetLibreTranslateApiKey() const;

    // [AWS]
    std::string GetAwsAccessKey() const;
    std::string GetAwsSecretKey() const;
    std::string GetAwsRegion() const;
    std::string GetAwsPollyEngine() const;

    // [Discord]
    std::string GetDiscordToken() const;

    // [General]
    std::string GetLogLevel() const;  // "error" | "info", default "error"

    // Writedir used for bundled-default path resolution
    std::string GetWritedir() const;

private:
    ConfigReader() = default;
    ConfigReader(const ConfigReader&) = delete;
    ConfigReader& operator=(const ConfigReader&) = delete;

    mutable std::mutex mutex_;
    bool loaded_ = false;
    std::string writedir_;

    std::string piperExe_;
    std::string piperVoicePath_;
    std::string googleCredsFile_;
    std::string azureKey_;
    std::string azureRegion_;
    std::string elevenLabsKey_;
    std::string elevenLabsModelId_;
    std::string kittenEndpoint_;
    std::string openaiKey_;
    std::string openaiEndpoint_;
    std::string openaiModel_;
    std::string openaiChatModel_;
    std::string libreTranslateEndpoint_;
    std::string libreTranslateApiKey_;
    std::string awsAccessKey_;
    std::string awsSecretKey_;
    std::string awsRegion_;
    std::string awsPollyEngine_;
    std::string discordToken_;
    std::string logLevel_;

    void ParseIni(const std::string& content);
    static std::string Trim(const std::string& s);
};

} // namespace HoundTTS

#endif // HOUNDTTS_CONFIG_READER_H
