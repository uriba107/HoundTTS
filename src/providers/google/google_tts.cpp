#include "google_tts.h"
#include "opus_encoder.h"
#include "utils.h"

#include "httplib.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <ctime>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Google";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// Minimal JSON field extractor (no external dep)
// Handles simple string fields: "key": "value"
// ---------------------------------------------------------------------------
static std::string JsonString(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + key.size());
    if (pos == std::string::npos) return "";
    ++pos;
    // Handle escaped characters minimally — collect until unescaped closing quote
    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char e = json[pos++];
            if      (e == 'n')  out += '\n';
            else if (e == 'r')  out += '\r';
            else if (e == 't')  out += '\t';
            else                out += e;
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Base64url encode (no padding) — for JWT header/payload
// ---------------------------------------------------------------------------
static const char kB64UrlChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string Base64UrlEncode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int b = (unsigned int)data[i] << 16;
        if (i + 1 < len) b |= (unsigned int)data[i+1] << 8;
        if (i + 2 < len) b |= (unsigned int)data[i+2];
        out += kB64UrlChars[(b >> 18) & 0x3F];
        out += kB64UrlChars[(b >> 12) & 0x3F];
        if (i + 1 < len) out += kB64UrlChars[(b >> 6) & 0x3F];
        if (i + 2 < len) out += kB64UrlChars[(b     ) & 0x3F];
    }
    return out; // no padding per JWT spec
}

static std::string Base64UrlEncodeStr(const std::string& s) {
    return Base64UrlEncode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// ---------------------------------------------------------------------------
// Standard base64 decode — for audioContent response
// ---------------------------------------------------------------------------
static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Decode(const std::string& in) {
    static int table[256];
    static bool init = false;
    if (!init) {
        std::fill(table, table + 256, -1);
        for (int i = 0; i < 64; i++) table[(unsigned char)kB64Chars[i]] = i;
        init = true;
    }
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (table[c] == -1) continue;
        val = (val << 6) + table[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back((char)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sign data with RSA private key (PEM) using SHA-256 → RS256 JWT signature
// ---------------------------------------------------------------------------
static std::string RS256Sign(const std::string& data, const std::string& pemKey) {
    BIO* bio = BIO_new_mem_buf(pemKey.data(), static_cast<int>(pemKey.size()));
    if (!bio) return "";

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        LogE("Failed to parse RSA private key from service-account JSON");
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::string sig;

    if (ctx &&
        EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
        EVP_DigestSignUpdate(ctx,
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size()) == 1)
    {
        size_t sigLen = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) == 1) {
            sig.resize(sigLen);
            EVP_DigestSignFinal(ctx,
                reinterpret_cast<unsigned char*>(&sig[0]), &sigLen);
            sig.resize(sigLen);
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return sig;
}

// ---------------------------------------------------------------------------
// Mint a Google OAuth2 JWT and exchange it for a Bearer access token.
// Returns the access token string, or empty on failure.
// ---------------------------------------------------------------------------
static std::string GetAccessToken(const std::string& clientEmail,
                                  const std::string& privateKey)
{
    // JWT header
    std::string header = R"({"alg":"RS256","typ":"JWT"})";

    // JWT claims
    long now = static_cast<long>(std::time(nullptr));
    std::ostringstream claims;
    claims << "{"
           << "\"iss\":\"" << clientEmail << "\","
           << "\"scope\":\"https://www.googleapis.com/auth/cloud-platform\","
           << "\"aud\":\"https://oauth2.googleapis.com/token\","
           << "\"iat\":" << now << ","
           << "\"exp\":" << (now + 3600)
           << "}";

    std::string signingInput = Base64UrlEncodeStr(header) + "." +
                               Base64UrlEncodeStr(claims.str());

    std::string sig = RS256Sign(signingInput, privateKey);
    if (sig.empty()) return "";

    std::string jwt = signingInput + "." +
                      Base64UrlEncode(reinterpret_cast<const unsigned char*>(sig.data()),
                                      sig.size());

    // Exchange JWT for access token
    httplib::SSLClient cli("oauth2.googleapis.com", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);

    std::string body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
                       "&assertion=" + jwt;

    auto res = cli.Post("/token", body, "application/x-www-form-urlencoded");
    if (!res || res->status != 200) {
        LogE("OAuth2 token exchange failed" +
             (res ? " HTTP " + std::to_string(res->status) + " body=[" + res->body + "]" : ""));
        return "";
    }

    std::string token = JsonString(res->body, "access_token");
    if (token.empty())
        LogE("access_token not found in OAuth2 response");
    return token;
}

// ---------------------------------------------------------------------------
// Read service-account JSON and extract client_email + private_key
// ---------------------------------------------------------------------------
static bool ReadServiceAccount(const std::string& credsFile,
                               std::string& outEmail,
                               std::string& outKey)
{
    std::ifstream f(Utils::Utf8ToWide(credsFile).c_str());
    if (!f.is_open()) {
        LogE("Cannot open credentials file: " + credsFile);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    outEmail = JsonString(json, "client_email");
    outKey   = JsonString(json, "private_key");

    if (outEmail.empty() || outKey.empty()) {
        LogE("credentials file missing client_email or private_key. "
             "Expected a Google service-account JSON file.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string MapGender(const std::string& gender) {
    if (gender == "male")   return "MALE";
    if (gender == "female") return "FEMALE";
    return "NEUTRAL";
}

static std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')       out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// SynthesizeToQueue
// ---------------------------------------------------------------------------
bool GoogleTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& credsFile,
    const std::string& voice,
    const std::string& culture,
    const std::string& gender,
    double speed,
    double volume,
    AudioQueue& queue)
{
    if (credsFile.empty()) {
        LogE("Google credentials file not configured in HoundTTS-credentials.ini");
        queue.MarkDone();
        return false;
    }

    std::string clientEmail, privateKey;
    if (!ReadServiceAccount(credsFile, clientEmail, privateKey)) {
        queue.MarkDone();
        return false;
    }

    LogI("Authenticating as " + clientEmail);
    std::string token = GetAccessToken(clientEmail, privateKey);
    if (token.empty()) {
        queue.MarkDone();
        return false;
    }

    std::string locale = culture.empty() ? "en-US" : culture;
    std::string ssmlGender = MapGender(gender);

    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0)  speed = 4.0;

    // Build JSON request body
    std::ostringstream body;
    body << "{"
         << "\"input\":{\"text\":\"" << JsonEscape(text) << "\"},"
         << "\"voice\":{"
         << "\"languageCode\":\"" << locale << "\","
         << "\"ssmlGender\":\"" << ssmlGender << "\"";
    if (!voice.empty())
        body << ",\"name\":\"" << voice << "\"";
    body << "},"
         << "\"audioConfig\":{"
         << "\"audioEncoding\":\"LINEAR16\","
         << "\"sampleRateHertz\":16000,"
         << "\"speakingRate\":" << speed
         << "}"
         << "}";

    LogI("Connecting to texttospeech.googleapis.com");

    httplib::SSLClient cli("texttospeech.googleapis.com", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + token},
        {"Content-Type",  "application/json"}
    };

    auto res = cli.Post("/v1/text:synthesize", headers, body.str(), "application/json");

    if (!res) {
        LogE("HTTP request failed (connection error)");
        queue.MarkDone();
        return false;
    }
    if (res->status != 200) {
        LogE("HTTP " + std::to_string(res->status) + " body=[" + res->body + "]");
        queue.MarkDone();
        return false;
    }

    // Response: {"audioContent": "<base64 LINEAR16 PCM>"}
    std::string b64 = JsonString(res->body, "audioContent");
    if (b64.empty()) {
        LogE("audioContent not found in response");
        queue.MarkDone();
        return false;
    }

    std::string pcmRaw = Base64Decode(b64);
    LogI("Got " + std::to_string(pcmRaw.size()) + " bytes of LINEAR16 PCM");

    if (pcmRaw.size() < 2) {
        LogE("PCM data too small");
        queue.MarkDone();
        return false;
    }

    OpusFrameEncoder encoder;
    if (!encoder.Init()) {
        LogE("OpusFrameEncoder::Init() failed");
        queue.MarkDone();
        return false;
    }

    int16_t* samples = reinterpret_cast<int16_t*>(&pcmRaw[0]);
    int sampleCount = static_cast<int>(pcmRaw.size() / 2);
    if (volume < 1.0) {
        double vol = std::max(0.0, std::min(1.0, volume));
        for (int i = 0; i < sampleCount; ++i)
            samples[i] = static_cast<int16_t>(samples[i] * vol);
    }
    encoder.EncodeChunk(samples, sampleCount, queue);
    encoder.Flush(queue);
    queue.MarkDone();

    return true;
}

} // namespace HoundTTS
