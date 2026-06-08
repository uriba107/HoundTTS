#pragma once

#ifndef HOUNDTTS_EDGE_DRM_H
#define HOUNDTTS_EDGE_DRM_H

#include <string>
#include <atomic>

namespace HoundTTS {

// Constants for the Edge TTS reverse-engineered API.
// These mirror the values used by Microsoft Edge's Read Aloud feature.
static constexpr const char* EDGE_TRUSTED_CLIENT_TOKEN = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
static constexpr const char* EDGE_CHROMIUM_FULL_VERSION = "143.0.3650.75";
static constexpr const char* EDGE_CHROMIUM_MAJOR_VERSION = "143";

// DRM token generator for Microsoft Edge TTS API.
// Handles Sec-MS-GEC token computation with clock skew correction.
class EdgeDRM {
public:
    // Generate the Sec-MS-GEC token value.
    // SHA256( floor(windows_filetime / 3000000000) * 3000000000  +  trusted_client_token )
    // Returns uppercased hex digest.
    static std::string GenerateSecMsGec();

    // Generate the Sec-MS-GEC-Version header value.
    static std::string SecMsGecVersion();

    // Generate a random MUID cookie value (32-char uppercase hex).
    static std::string GenerateMuid();

    // Adjust clock skew (seconds) when server returns 403 with a Date header.
    // Thread-safe (atomic).
    static void AdjustClockSkew(double seconds);

    // Get current Unix timestamp with clock skew correction applied.
    static double GetCorrectedTimestamp();

    // Build the full WebSocket URL with all required query parameters.
    // connectionId must be a 32-char uppercase hex string (no dashes, URL-safe).
    static std::string BuildWebSocketUrl(const std::string& connectionId);

    // Build the required WebSocket headers (User-Agent, Origin, Cookie, etc).
    static std::string BuildUserAgent();
    static std::string BuildOrigin();
    static std::string BuildCookieHeader();

private:
    static std::atomic<double> clockSkewSeconds_;
};

} // namespace HoundTTS

#endif // HOUNDTTS_EDGE_DRM_H
