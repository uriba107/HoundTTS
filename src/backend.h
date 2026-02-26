#pragma once

#ifndef HOUNDTTS_BACKEND_H
#define HOUNDTTS_BACKEND_H

#include <string>

namespace HoundTTS {

// TTS transmission request — all fields are UTF-8 strings unless noted.
struct TTSRequest {
    // Routing
    std::string writedir;       // Saved Games\DCS\ — DLL locates credentials INI from here
    std::string transmitter;    // "srs" | "discord"
    std::string provider;       // "piper" | "azure" | "google" | "elevenlabs" | "sapi" | "kittentts" | "kitten_tts" | "kitten"

    // SRS / transmission
    int         srsPort = 5002;
    std::string srsHost;        // SRS server hostname/IP (default: localhost)
    bool        encrypt = false;
    int         encKey  = 0;    // Encryption key (0-255)

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
    std::string pollyEngine;    // "neural" | "standard" | "long-form" (overrides INI default)

    // Position
    double      lat = 91.0;    // >90 means unset
    double      lon = 181.0;   // >180 means unset
    double      alt = -500.0;  // <-499 means unset

    bool        isSSML = false;
};

// Abstract backend interface.
class ITTSBackend {
public:
    virtual ~ITTSBackend() = default;

    // Transmit a TTS message. Returns true if the request was dispatched successfully.
    virtual bool TransmitTTS(const TTSRequest& req) = 0;
};

} // namespace HoundTTS

#endif // HOUNDTTS_BACKEND_H
