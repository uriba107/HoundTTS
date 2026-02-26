#include "config_reader.h"
#include "utils.h"

#include <fstream>
#include <sstream>
#include <algorithm>
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

    std::string path = writedir;
    if (!path.empty() && path.back() != '\\' && path.back() != '/')
        path += '\\';
    path += "Config\\HoundTTS-credentials.ini";

    std::ifstream f(Utils::Utf8ToWide(path).c_str());
    if (!f.is_open()) {
        loaded_ = true;
        return;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    ParseIni(ss.str());
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
            if (key == "exe")        piperExe_       = val;
            if (key == "voice_path") piperVoicePath_ = val;
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
        } else if (section == "Polly") {
            if (key == "access_key") pollyAccessKey_ = val;
            if (key == "secret_key") pollySecretKey_ = val;
            if (key == "region")     pollyRegion_    = val;
            if (key == "engine")     pollyEngine_    = val;
        } else if (section == "Discord") {
            if (key == "bot_token") discordToken_ = val;
        } else if (section == "General") {
            if (key == "log_level") logLevel_ = val;
        }
    }

    // Apply defaults for blank values
    if (elevenLabsModelId_.empty()) elevenLabsModelId_ = "eleven_turbo_v2";
    if (pollyEngine_.empty())        pollyEngine_       = "neural";

    // Bundled piper defaults (relative to writedir)
    if (piperExe_.empty()) {
        piperExe_ = writedir_;
        if (!piperExe_.empty() && piperExe_.back() != '\\') piperExe_ += '\\';
        piperExe_ += "Mods\\Services\\HoundTTS\\bin\\piper\\piper.exe";
    }
    if (piperVoicePath_.empty()) {
        piperVoicePath_ = writedir_;
        if (!piperVoicePath_.empty() && piperVoicePath_.back() != '\\') piperVoicePath_ += '\\';
        piperVoicePath_ += "Mods\\Services\\HoundTTS\\voices\\";
    }
}

std::string ConfigReader::GetWritedir() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writedir_;
}
std::string ConfigReader::GetPiperExe() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return piperExe_;
}
std::string ConfigReader::GetPiperVoicePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return piperVoicePath_;
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
std::string ConfigReader::GetPollyAccessKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pollyAccessKey_;
}
std::string ConfigReader::GetPollySecretKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pollySecretKey_;
}
std::string ConfigReader::GetPollyRegion() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pollyRegion_;
}
std::string ConfigReader::GetPollyEngine() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pollyEngine_;
}
std::string ConfigReader::GetDiscordToken() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return discordToken_;
}
std::string ConfigReader::GetLogLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logLevel_.empty() ? "error" : logLevel_;
}

} // namespace HoundTTS
