#include "config_reader.h"
#include "utils.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <cctype>

namespace HoundTTS {

ConfigReader& ConfigReader::Instance() {
    static ConfigReader instance;
    return instance;
}

std::string ConfigReader::Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void ConfigReader::Load(const std::string& writedir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_ && writedir_ == writedir) return;

    writedir_ = writedir;

    // Reset all config members so stale values from a previous Load() don't carry over
    piperPath_.clear();
    piperVoicePath_.clear();
    piperThreads_ = 4;
    piperMaxConcurrent_ = 4;
    googleCredsFile_.clear();
    azureKey_.clear();
    azureRegion_.clear();
    elevenLabsKey_.clear();
    elevenLabsModelId_.clear();
    kittenEndpoint_.clear();
    openaiKey_.clear();
    openaiEndpoint_.clear();
    openaiModel_.clear();
    openaiChatModel_.clear();
    libreTranslateEndpoint_.clear();
    libreTranslateApiKey_.clear();
    awsAccessKey_.clear();
    awsSecretKey_.clear();
    awsRegion_.clear();
    awsPollyEngine_.clear();
    discordToken_.clear();
    logLevel_.clear();
    cacheEnabled_ = true;
    cacheMaxMb_   = 100;
    cacheTtlMin_  = 5;

    std::string path = writedir;
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
        path += '\\';
    path += "Config\\HoundTTS-credentials.ini";

    std::ifstream f(Utils::Utf8ToWide(path).c_str());
    if (f.is_open()) {
        std::ostringstream ss;
        ss << f.rdbuf();
        ParseIni(ss.str());
    }

    // Apply defaults (always, even if config file doesn't exist)
    if (elevenLabsModelId_.empty()) elevenLabsModelId_ = "eleven_turbo_v2";
    if (awsPollyEngine_.empty())     awsPollyEngine_    = "neural";
    if (openaiEndpoint_.empty())     openaiEndpoint_    = "https://api.openai.com";
    if (openaiModel_.empty())        openaiModel_       = "tts-1";
    if (openaiChatModel_.empty())    openaiChatModel_   = "gpt-4o-mini";
    if (libreTranslateEndpoint_.empty()) libreTranslateEndpoint_ = "http://localhost:5000";

    if (piperPath_.empty()) {
        piperPath_ = writedir_;
        if (!piperPath_.empty() && piperPath_.back() != '\\' && piperPath_.back() != '/') piperPath_ += '\\';
        piperPath_ += "Mods\\Services\\HoundTTS\\bin\\piper";
    }
    if (piperVoicePath_.empty()) {
        piperVoicePath_ = writedir_;
        if (!piperVoicePath_.empty() && piperVoicePath_.back() != '\\' && piperVoicePath_.back() != '/') piperVoicePath_ += '\\';
        piperVoicePath_ += "Mods\\Services\\HoundTTS\\voices\\";
    }

    loaded_ = true;
}

void ConfigReader::ParseIni(const std::string& content) {
    std::string section;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos)
                section = Trim(line.substr(1, end - 1));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        // Strip inline comments
        size_t semi = val.find(';');
        if (semi != std::string::npos)
            val = Trim(val.substr(0, semi));

        // Strip surrounding quotes (single or double) — paths with spaces don't need them
        if (val.size() >= 2 &&
            ((val.front() == '"'  && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\'')))
            val = val.substr(1, val.size() - 2);

        if (section == "Piper") {
            if (key == "path" || key == "exe" || key == "dll_path") {
                // Normalize: strip trailing piper.exe or piper.dll to get directory
                auto endsWithCI = [](const std::string& s, const std::string& suffix) {
                    if (s.size() < suffix.size()) return false;
                    std::string tail = s.substr(s.size() - suffix.size());
                    for (auto& c : tail) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                    return tail == suffix;
                };
                if (endsWithCI(val, "\\piper.exe") || endsWithCI(val, "/piper.exe") ||
                    endsWithCI(val, "\\piper.dll") || endsWithCI(val, "/piper.dll")) {
                    val = val.substr(0, val.find_last_of("\\/"));
                }
                piperPath_ = val;
            }
            if (key == "voice_path") piperVoicePath_ = val;
            if (key == "threads") {
                int v = 0;
                auto [p, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
                if (ec == std::errc{} && v > 0) piperThreads_ = v;
            }
            if (key == "max_concurrent") {
                int v = 0;
                auto [p, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
                if (ec == std::errc{} && v > 0) piperMaxConcurrent_ = v;
            }
        } else if (section == "Google") {
            if (key == "credentials_file") googleCredsFile_ = val;
        } else if (section == "Azure") {
            if (key == "key")    azureKey_    = val;
            if (key == "region") azureRegion_ = val;
        } else if (section == "ElevenLabs") {
            if (key == "api_key")  elevenLabsKey_     = val;
            if (key == "model_id") elevenLabsModelId_ = val;
        } else if (section == "KittenTTS") {
            if (key == "endpoint") kittenEndpoint_ = val;
        } else if (section == "OpenAI") {
            if (key == "api_key")  openaiKey_      = val;
            if (key == "endpoint") openaiEndpoint_ = val;
            if (key == "model")    openaiModel_    = val;
            if (key == "chat_model") openaiChatModel_ = val;
        } else if (section == "AWS") {
            if (key == "access_key") awsAccessKey_   = val;
            if (key == "secret_key") awsSecretKey_   = val;
            if (key == "region")     awsRegion_      = val;
            if (key == "engine")     awsPollyEngine_ = val;
        } else if (section == "LibreTranslate") {
            if (key == "endpoint") libreTranslateEndpoint_ = val;
            if (key == "api_key")  libreTranslateApiKey_  = val;
        } else if (section == "Discord") {
            if (key == "bot_token") discordToken_ = val;
        } else if (section == "General") {
            if (key == "log_level") logLevel_ = val;
            if (key == "cache_enabled") {
                std::string lower = val;
                for (auto& c : lower) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                cacheEnabled_ = (lower != "false" && lower != "0" && lower != "no");
            }
            if (key == "cache_max_mb") {
                int v = 0;
                auto [p, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
                if (ec == std::errc{} && v > 0) cacheMaxMb_ = v;
            }
            if (key == "cache_ttl_minutes") {
                int v = -1;
                auto [p, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
                if (ec == std::errc{} && v >= 0) cacheTtlMin_ = v;
            }
        }
    }
    // Defaults are applied in Load() after ParseIni() returns
}

std::string ConfigReader::GetWritedir() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writedir_;
}
std::string ConfigReader::GetPiperPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return piperPath_;
}
std::string ConfigReader::GetPiperVoicePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return piperVoicePath_;
}
int ConfigReader::GetPiperThreads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return piperThreads_;
}
int ConfigReader::GetPiperMaxConcurrent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return piperMaxConcurrent_;
}
std::string ConfigReader::GetGoogleCredsFile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return googleCredsFile_;
}
std::string ConfigReader::GetAzureKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return azureKey_;
}
std::string ConfigReader::GetAzureRegion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return azureRegion_;
}
std::string ConfigReader::GetElevenLabsKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return elevenLabsKey_;
}
std::string ConfigReader::GetElevenLabsModelId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return elevenLabsModelId_;
}
std::string ConfigReader::GetKittenEndpoint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return kittenEndpoint_;
}
std::string ConfigReader::GetOpenAIKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return openaiKey_;
}
std::string ConfigReader::GetOpenAIEndpoint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return openaiEndpoint_;
}
std::string ConfigReader::GetOpenAIModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return openaiModel_;
}
std::string ConfigReader::GetOpenAIChatModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return openaiChatModel_;
}
std::string ConfigReader::GetAwsAccessKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return awsAccessKey_;
}
std::string ConfigReader::GetAwsSecretKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return awsSecretKey_;
}
std::string ConfigReader::GetAwsRegion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return awsRegion_;
}
std::string ConfigReader::GetAwsPollyEngine() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return awsPollyEngine_;
}
std::string ConfigReader::GetDiscordToken() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return discordToken_;
}
std::string ConfigReader::GetLibreTranslateEndpoint() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return libreTranslateEndpoint_;
}
std::string ConfigReader::GetLibreTranslateApiKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return libreTranslateApiKey_;
}
std::string ConfigReader::GetLogLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logLevel_.empty() ? "error" : logLevel_;
}
bool ConfigReader::GetCacheEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheEnabled_;
}
int ConfigReader::GetCacheMaxMb() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheMaxMb_;
}
int ConfigReader::GetCacheTtlMinutes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheTtlMin_;
}

} // namespace HoundTTS
