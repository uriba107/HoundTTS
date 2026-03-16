#pragma once

#ifndef HOUNDTTS_PIPER_TTS_H
#define HOUNDTTS_PIPER_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via piper.exe subprocess.
// Streams raw PCM stdout chunks → pushes chunks to PCMQueue (with resampling if needed).
// Calls queue.MarkDone() when piper.exe exits.
class PiperTTS {
public:
    // modelPath: path to .onnx model file
    // piperExe:  path to piper.exe; if empty, looks in same dir as modelPath
    // volume:    0.0..1.0 (applied as PCM sample scalar)
    // Returns false if piper.exe could not be launched.
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& modelPath,
        const std::string& piperExe,
        const std::string& speaker,
        double speed,
        double volume,
        PCMQueue& queue);

private:
    // Read sample_rate from <modelPath>.json sidecar. Returns 0 on failure.
    static int ReadSampleRate(const std::string& modelPath);

    // Resolve piper.exe path: explicit piperExe, or same dir as modelPath.
    static std::string ResolvePiperExe(const std::string& modelPath,
                                        const std::string& piperExe);
};

} // namespace HoundTTS

#endif // HOUNDTTS_PIPER_TTS_H
