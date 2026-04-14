#include "piper_voice_registry.h"
#include "utils.h"

#include <windows.h>
#include <algorithm>
#include <cctype>

namespace HoundTTS {

static const char* kTag = "HoundTTS/PiperVoiceRegistry";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

// Convert string to lowercase for case-insensitive comparison
static std::string ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

void PiperVoiceRegistry::Initialize(const std::string& voicesPath, const std::string& defaultVoice) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) return;  // Already initialized
    initialized_ = true;

    voiceFolderPath_ = voicesPath;
    defaultVoice_ = defaultVoice;
    defaultFound_ = false;

    if (voicesPath.empty()) {
        LogE("Voices path is empty. No voices will be available.");
        return;
    }

    // Build search pattern: <voicesPath>\*.onnx
    std::string searchPattern = voicesPath;
    if (!searchPattern.empty() && searchPattern.back() != '\\' && searchPattern.back() != '/') {
        searchPattern += '\\';
    }
    searchPattern += "*.onnx";

    // Convert to wide string for Windows API
    int wlen = MultiByteToWideChar(CP_UTF8, 0, searchPattern.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        LogE("Failed to convert voices path to wide string");
        return;
    }
    std::wstring wSearchPattern(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, searchPattern.c_str(), -1, &wSearchPattern[0], wlen);

    // Scan folder for .onnx files
    WIN32_FIND_DATAW findData;
    HANDLE findHandle = FindFirstFileW(wSearchPattern.c_str(), &findData);

    if (findHandle == INVALID_HANDLE_VALUE) {
        LogE("Voices folder not found or empty: " + voicesPath);
        return;
    }

    do {
        // Get filename and convert to UTF-8
        int fileNameLen = WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, nullptr, 0, nullptr, nullptr);
        if (fileNameLen <= 0) continue;

        std::string fileName(fileNameLen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, &fileName[0], fileNameLen, nullptr, nullptr);

        // Extract basename without .onnx extension (case-insensitive: FindFirstFile
        // returns names in their real on-disk casing, e.g. ".ONNX" on case-preserving
        // volumes).
        if (fileName.size() > 5 && ToLower(fileName.substr(fileName.size() - 5)) == ".onnx") {
            std::string basename = fileName.substr(0, fileName.size() - 5);
            std::string basenameKey = ToLower(basename);
            voices_.insert(basenameKey);

            // Track if this is the default voice (case-insensitive comparison)
            if (!defaultFound_ && basenameKey == ToLower(defaultVoice)) {
                defaultFound_ = true;
            }
        }
    } while (FindNextFileW(findHandle, &findData));

    FindClose(findHandle);

    // Log discovery results
    if (voices_.empty()) {
        LogE("No Piper voice files found in: " + voicesPath);
        if (!defaultVoice.empty()) {
            LogE("Default Piper voice (" + defaultVoice + ") not found. See README for voice installation.");
        }
    } else {
        std::string voiceList;
        for (const auto& v : voices_) {
            if (!voiceList.empty()) voiceList += ", ";
            voiceList += v;
        }
        LogI("Found " + std::to_string(voices_.size()) + " Piper voice(s): " + voiceList);

        if (!defaultVoice.empty() && !defaultFound_) {
            LogE("Default Piper voice (" + defaultVoice + ") not found. See README for voice installation.");
        } else if (!defaultVoice.empty() && defaultFound_) {
            LogI("Default Piper voice available: " + defaultVoice);
        }
    }
}

bool PiperVoiceRegistry::IsVoiceAvailable(const std::string& voiceName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return false;
    return voices_.count(ToLower(voiceName)) > 0;
}

bool PiperVoiceRegistry::IsDefaultVoiceAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && defaultFound_;
}

std::vector<std::string> PiperVoiceRegistry::GetAvailableVoices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result(voices_.begin(), voices_.end());
    return result;
}

std::string PiperVoiceRegistry::GetVoiceFolderPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return voiceFolderPath_;
}

bool PiperVoiceRegistry::EnsureInitialized(const std::string& voicesPath, const std::string& defaultVoice) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (initialized_) return defaultFound_;
    
    // Temporarily unlock while initializing (Initialize() may take time)
    lock.unlock();
    Initialize(voicesPath, defaultVoice);
    lock.lock();
    
    return defaultFound_;
}

} // namespace HoundTTS
