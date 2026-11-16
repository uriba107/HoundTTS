#pragma once

#ifndef HOUNDTTS_AZURE_TTS_H
#define HOUNDTTS_AZURE_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Azure Cognitive Services Speech REST API.
// Requests riff-16khz-16bit-mono-pcm; walks the RIFF chunks to the 'data'
// payload and pushes 16 kHz mono int16 PCM to PCMQueue (non-RIFF bodies are
// treated as raw PCM).
// Plain text is XML-escaped into <speak>; a caller-supplied SSML document
// (text starting with "<speak") is sent through unchanged.
// Calls queue.MarkDone() exactly once on every return path, including when the
// body throws (the detached-thread caller must never see an escaped exception).
class AzureTTS {
public:
    // key:    Azure subscription key
    // region: Azure region e.g. "eastus"
    // voice:  Azure voice name e.g. "en-US-AriaNeural" (optional, uses default if empty)
    // culture: BCP-47 locale e.g. "en-US" (used to pick default voice if voice is empty)
    // gender: "male" | "female" (used to pick default voice if voice is empty)
    // speed:  rate multiplier (1.0 = normal; mapped to SSML prosody rate)
    // volume: 0.0..1.0 (applied as PCM sample scalar)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& key,
        const std::string& region,
        const std::string& voice,
        const std::string& culture,
        const std::string& gender,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_AZURE_TTS_H
