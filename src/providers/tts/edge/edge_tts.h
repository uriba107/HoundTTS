#pragma once

#ifndef HOUNDTTS_EDGE_TTS_H
#define HOUNDTTS_EDGE_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Microsoft Edge's online Read Aloud TTS service.
// Uses a reverse-engineered WebSocket API — no API key required.
// Requests 24kHz MP3, decodes to 16kHz 16-bit mono PCM → pushes to PCMQueue.
// Calls queue.MarkDone() on every normal return path (success or failure);
// the dispatching caller delivers MarkDone if this function throws.
//
// WARNING: This uses an unofficial Microsoft endpoint. It may stop working
// if Microsoft changes the API, revokes the trusted client token, or
// modifies the DRM scheme. No SLA or guarantees.
class EdgeTTS {
public:
    // text:    plain UTF-8 text; XML escaping and control-char stripping happen
    //          inside SynthesizeToQueue (do not pre-escape).
    // voice:   Azure Neural voice name e.g. "en-US-AriaNeural" (optional, uses default if empty)
    // culture: BCP-47 locale e.g. "en-US" (used to pick default voice if voice is empty)
    // gender:  "male" | "female" (used to pick default voice if voice is empty)
    // speed:   rate multiplier (1.0 = normal; mapped to SSML prosody rate)
    // volume:  0.0..1.0 (applied as PCM sample scalar)
    // Returns true when at least one audio chunk was decoded and pushed.
    // On a WebSocket connect failure (assumed 403/network) the attempt is
    // retried once with a +5s clock-skew nudge. The retry happens before any
    // audio is read or pushed, so the utterance is never enqueued twice.
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& voice,
        const std::string& culture,
        const std::string& gender,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_EDGE_TTS_H
