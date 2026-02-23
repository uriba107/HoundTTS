#pragma once

#ifndef HOUNDTTS_AZURE_TTS_H
#define HOUNDTTS_AZURE_TTS_H

#include "audio_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Azure Cognitive Services Speech REST API.
// Requests audio/wav (16kHz mono PCM) → encodes to Opus → pushes to AudioQueue.
// Calls queue.MarkDone() when done (success or failure).
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
        AudioQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_AZURE_TTS_H
