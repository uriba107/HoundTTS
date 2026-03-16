#pragma once

#ifndef HOUNDTTS_AWS_TTS_H
#define HOUNDTTS_AWS_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Amazon Polly SynthesizeSpeech REST API.
// Requests raw PCM (16kHz mono 16-bit) → pushes raw PCM chunks to PCMQueue.
// Calls queue.MarkDone() when done (success or failure).
class AwsTTS {
public:
    // accessKey:  AWS access key ID
    // secretKey:  AWS secret access key
    // region:     AWS region e.g. "us-east-1"
    // voice:      Polly VoiceId e.g. "Joanna" (optional, picks default if empty)
    // engine:     "standard" | "neural" | "long-form" (optional, defaults to "standard")
    // culture:    BCP-47 locale e.g. "en-US" (used to pick default voice if voice is empty)
    // gender:     "male" | "female" (used to pick default voice if voice is empty)
    // speed:      rate multiplier (1.0 = normal; mapped to SSML prosody rate)
    // volume:     0.0..1.0 (applied as PCM sample scalar)
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& accessKey,
        const std::string& secretKey,
        const std::string& region,
        const std::string& voice,
        const std::string& engine,
        const std::string& culture,
        const std::string& gender,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_AWS_TTS_H
