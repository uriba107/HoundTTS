#pragma once

#ifndef HOUNDTTS_PIPER_VOICE_REGISTRY_H
#define HOUNDTTS_PIPER_VOICE_REGISTRY_H

#include <string>
#include <vector>
#include <set>
#include <mutex>

namespace HoundTTS {

// PiperVoiceRegistry — singleton registry of available Piper voice models.
// 
// Scans the voices folder on Initialize() and caches available voice basenames.
// Thread-safe: all public methods take a mutex (queries included).
// The critical sections are short read-only lookups against a std::set and
// a few std::string members, so contention is negligible.
//
// Usage:
//   PiperVoiceRegistry::Instance().Initialize(voicesPath, defaultVoice);
//   if (!PiperVoiceRegistry::Instance().IsVoiceAvailable("en_US-lessac-low")) {
//       LogE("Voice not found!");
//   }
class PiperVoiceRegistry {
public:
    static PiperVoiceRegistry& Instance() {
        static PiperVoiceRegistry instance;
        return instance;
    }

    // Initialize the registry by scanning the voices folder.
    // Safe to call multiple times; only scans once.
    // defaultVoice: full name of the default fallback voice (e.g., "en_US-lessac-low")
    // Logs discovery results at INFO level; logs ERROR if default voice not found.
    void Initialize(const std::string& voicesPath, const std::string& defaultVoice);

    // Ensure registry is initialized. Idempotent: safe to call multiple times.
    // Returns true if initialization was successful, false on error.
    bool EnsureInitialized(const std::string& voicesPath, const std::string& defaultVoice);

    // Check if a voice is available (case-insensitive).
    // voiceName should be the basename without .onnx extension.
    bool IsVoiceAvailable(const std::string& voiceName) const;

    // Check if the default fallback voice is available.
    bool IsDefaultVoiceAvailable() const;

    // Get list of all discovered voices (for logging/debugging).
    std::vector<std::string> GetAvailableVoices() const;

    // Get the voices folder path (for logging/debugging).
    std::string GetVoiceFolderPath() const;

private:
    PiperVoiceRegistry() = default;
    PiperVoiceRegistry(const PiperVoiceRegistry&) = delete;
    PiperVoiceRegistry& operator=(const PiperVoiceRegistry&) = delete;

    mutable std::mutex         mutex_;
    std::set<std::string>      voices_;           // case-insensitive voice basenames
    std::string                voiceFolderPath_;  // path to voices folder
    std::string                defaultVoice_;     // default fallback voice name
    bool                       defaultFound_ = false;     // whether default voice was found
    bool                       initialized_ = false;      // whether Initialize() has been called
};

} // namespace HoundTTS

#endif // HOUNDTTS_PIPER_VOICE_REGISTRY_H
