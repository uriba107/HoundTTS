#pragma once

#ifndef HOUNDTTS_KITTEN_TTS_H
#define HOUNDTTS_KITTEN_TTS_H

#include "backends/pcm_queue.h"

#include <string>

namespace HoundTTS {

// Synthesizes text via Kitten TTS Server HTTP REST API.
// POST /tts with JSON body → decodes WAV/Ogg-Opus response → pushes raw PCM chunks to PCMQueue.
// Calls queue.MarkDone() when done (success or failure).
// Server must be running externally; endpoint configured via [KittenTTS] endpoint in INI.
class KittenTTS {
public:
    // endpoint:    Full base URL of the Kitten TTS server, e.g. "http://192.168.10.30:8005"
    // voice:       Required voice ID string, e.g. "Bella" or "Hugo"
    //              Available: Bella, Jasper, Luna, Bruno, Rosie, Hugo, Kiki, Leo
    // language:    Optional language override e.g. "en" (server default used if empty)
    // speed:       Speaking rate multiplier (1.0 = normal); omitted from request if <= 0
    // volume:      0.0..1.0 — applied to PCM when server returns WAV;
    //              not applied for direct Opus output (would require decode/re-encode)
    // queue:       PCMQueue to push raw PCM chunks into
    static bool SynthesizeToQueue(
        const std::string& text,
        const std::string& endpoint,
        const std::string& voice,
        const std::string& language,
        double speed,
        double volume,
        PCMQueue& queue);
};

} // namespace HoundTTS

#endif // HOUNDTTS_KITTEN_TTS_H
