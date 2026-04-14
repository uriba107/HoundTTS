#pragma once

#ifndef HOUNDTTS_PIPER_TTS_H
#define HOUNDTTS_PIPER_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Default fallback voice for Piper TTS
static constexpr const char* PIPER_DEFAULT_VOICE = "en_US-lessac-low";

// Synthesizes text via piper.dll (preferred) or piper.exe subprocess (deprecated fallback).
// Pushes 16kHz mono int16 PCM chunks into queue. Calls queue.MarkDone() on completion.
class PiperTTS {
public:
    // modelPath: path to .onnx model file
    // piperPath: directory containing piper.dll + onnxruntime.dll + espeak-ng-data/
    //            If piper.dll is found, uses in-process synthesis with model pool caching.
    //            Falls back to piper.exe in the same directory if piper.dll is unavailable.
    // volume:    0.0..1.0 (applied as PCM sample scalar)
    // Returns false on unrecoverable error.
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& modelPath,
        const std::string& piperPath,
        const std::string& speaker,
        double speed,
        double volume,
        PCMQueue& queue);

    // Initialize Piper subsystem (voice registry, etc).
    // Safe to call multiple times; lazy initialization on first call.
    static void EnsureInitialized(const std::string& voicesPath);

private:
    // Native DLL path: in-process synthesis via piper.dll model pool.
    static bool SynthesizeViaNative(
        const std::string& text,
        const std::string& modelPath,
        const std::string& espeakDataPath,
        const std::string& speaker,
        double speed,
        double volume,
        PCMQueue& queue);

    // Deprecated subprocess path: spawns piper.exe via CreateProcessW.
    static bool SynthesizeViaSubprocess(
        const std::string& text,
        const std::string& modelPath,
        const std::string& piperPath,
        const std::string& speaker,
        double speed,
        double volume,
        PCMQueue& queue);

    // Read sample_rate from <modelPath>.json sidecar. Returns 0 on failure.
    static int ReadSampleRate(const std::string& modelPath);

    // Resolve piper.exe path from piperPath dir.
    static std::string ResolvePiperExe(const std::string& piperPath);
};

} // namespace HoundTTS

#endif // HOUNDTTS_PIPER_TTS_H
