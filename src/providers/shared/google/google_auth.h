#pragma once

#ifndef HOUNDTTS_GOOGLE_AUTH_H
#define HOUNDTTS_GOOGLE_AUTH_H

#include <string>

namespace HoundTTS {
namespace GoogleAuth {

// Read client_email and private_key from a Google service-account JSON file.
// Returns false and logs on failure.
bool ReadServiceAccount(const std::string& credsFile,
                        std::string& outEmail,
                        std::string& outKey);

// Mint a JWT and exchange it for a Google OAuth2 Bearer access token.
// scope: e.g. "https://www.googleapis.com/auth/cloud-platform"
// Returns access token string, or empty on failure.
std::string GetAccessToken(const std::string& clientEmail,
                           const std::string& privateKey,
                           const std::string& scope);

// Minimal JSON string field extractor: finds "field": "value"
std::string JsonString(const std::string& json, const std::string& field);

// Escape a string for use in a JSON string value
std::string JsonEscape(const std::string& s);

} // namespace GoogleAuth
} // namespace HoundTTS

#endif // HOUNDTTS_GOOGLE_AUTH_H
