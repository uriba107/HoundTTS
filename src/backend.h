#pragma once

#ifndef HOUNDTTS_BACKEND_H
#define HOUNDTTS_BACKEND_H

#include <string>
#include <memory>
#include <cstdint>
#include "provider.h"
#include "backends/pcm_queue.h"
#include "session.h"

namespace HoundTTS {

// TTS transmission request — all fields are UTF-8 strings unless noted.
// Consumed by TTSPipeline::Produce(); backends never see this directly.
struct TTSRequest {
    // Routing
    std::string writedir;       // Saved Games\DCS\ — DLL locates credentials INI from here
    std::string transmitter;    // "srs" | "discord"
    TtsProvider  provider = TtsProvider::Sapi;

    // SRS / transmission
    int         srsPort = 5002;
    std::string srsHost;        // SRS server hostname/IP (default: localhost)
    bool        encrypt = false;
    int         encKey  = 0;    // Encryption key (0-255)
    std::string srsBluePassword; // EAM password for blue coalition (2)
    std::string srsRedPassword;  // EAM password for red coalition (1)

    // Content
    std::string message;
    std::string freqs;          // Comma-separated frequencies in MHz
    std::string modulations;    // Comma-separated AM/FM
    int         coalition = 0;  // 0=spectator, 1=red, 2=blue
    std::string name;           // Transmitter name shown in SRS

    // Audio
    double      volume = 1.0;
    double      speed  = -999.0; // sentinel: not set → provider applies its own default

    // Voice selection (provider-dependent)
    std::string gender;         // male / female / neuter
    std::string culture;        // e.g. en-US, en-GB
    std::string voice;          // Piper model name OR ElevenLabs voice ID
    std::string speaker;        // Piper speaker name or integer ID (multi-speaker models only)
    std::string awsPollyEngine; // "neural" | "standard" | "long-form" (overrides INI default)

    // Position
    double      lat = 91.0;    // >90 means unset
    double      lon = 181.0;   // >180 means unset
    double      alt = -500.0;  // <-499 means unset

    bool        isSSML = false;

    // Optional translation step (runs before TTS in the background thread).
    // If translateProvider is non-empty, message is translated first.
    TranslateProvider translateProvider = TranslateProvider::None;
    std::string translateLanguage;       // ISO 639-1 target code e.g. "de"
    std::string translateSourceLanguage; // ISO 639-1 source code, default "en"
};

// Transmission parameters passed to a backend — pure routing/network info,
// no TTS logic. Extracted from TTSRequest by lua_tts.cpp before calling Transmit().
struct TransmitParams {
    std::string host;
    int         port       = 5002;
    std::string freqs;          // Comma-separated frequencies in MHz
    std::string modulations;    // Comma-separated AM/FM
    bool        encrypt    = false;
    uint8_t     encKey     = 0;
    int         coalition  = 0;
    std::string name;           // Transmitter name shown in SRS
    std::string srsBluePassword; // EAM password for blue coalition (2)
    std::string srsRedPassword;  // EAM password for red coalition (1)
    double      lat        = 91.0;
    double      lon        = 181.0;
    double      alt        = -500.0;

    // Optional session for position updates and kill signal.
    // nullptr means fire-and-forget (legacy TTS with no update support).
    std::shared_ptr<Session> session;
};

// Abstract backend interface.
// Receives already-synthesised 16kHz mono PCM and transmission parameters.
// Returns true if the transmission was dispatched (non-blocking implementations
// may return before the stream completes).
class ITTSBackend {
public:
    virtual ~ITTSBackend() = default;

    virtual bool Transmit(std::shared_ptr<PCMQueue> pcm,
                          const TransmitParams& params) = 0;
};

} // namespace HoundTTS

#endif // HOUNDTTS_BACKEND_H
