#include "polly_tts.h"
#include "opus_encoder.h"
#include "utils.h"

#include "httplib.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Polly";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// Hex encode a byte buffer
// ---------------------------------------------------------------------------
static std::string HexEncode(const unsigned char* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        ss << std::setw(2) << static_cast<int>(data[i]);
    return ss.str();
}

// ---------------------------------------------------------------------------
// SHA-256 of a string → hex string
// ---------------------------------------------------------------------------
static std::string SHA256Hex(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    return HexEncode(hash, SHA256_DIGEST_LENGTH);
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 → raw bytes
// ---------------------------------------------------------------------------
static std::vector<unsigned char> HmacSHA256(const std::vector<unsigned char>& key,
                                              const std::string& data)
{
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int outLen = 0;
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         out, &outLen);
    return std::vector<unsigned char>(out, out + outLen);
}

static std::vector<unsigned char> HmacSHA256(const std::string& key,
                                              const std::string& data)
{
    std::vector<unsigned char> keyVec(key.begin(), key.end());
    return HmacSHA256(keyVec, data);
}

// ---------------------------------------------------------------------------
// SigV4 signing key derivation:
//   kDate    = HMAC-SHA256("AWS4" + secretKey, date)
//   kRegion  = HMAC-SHA256(kDate,   region)
//   kService = HMAC-SHA256(kRegion, "polly")
//   kSigning = HMAC-SHA256(kService,"aws4_request")
// ---------------------------------------------------------------------------
static std::vector<unsigned char> DeriveSigningKey(const std::string& secretKey,
                                                    const std::string& date,
                                                    const std::string& region)
{
    auto kDate    = HmacSHA256("AWS4" + secretKey, date);
    auto kRegion  = HmacSHA256(kDate,              region);
    auto kService = HmacSHA256(kRegion,            "polly");
    auto kSigning = HmacSHA256(kService,           "aws4_request");
    return kSigning;
}

// ---------------------------------------------------------------------------
// Compute SigV4 Authorization header value.
// Returns the full "AWS4-HMAC-SHA256 Credential=..." header value.
// ---------------------------------------------------------------------------
static std::string ComputeSigV4Auth(
    const std::string& accessKey,
    const std::string& secretKey,
    const std::string& region,
    const std::string& method,       // "POST"
    const std::string& uri,          // "/v1/speech"
    const std::string& body,
    const std::string& amzDate,      // "20240101T120000Z"
    const std::string& dateStamp)    // "20240101"
{
    // Canonical headers (must be sorted, lowercase)
    // Note: content-type is intentionally excluded from signed headers because
    // httplib may append "; charset=utf-8" to the wire header, causing a mismatch.
    std::string host = "polly." + region + ".amazonaws.com";
    std::string canonicalHeaders =
        "host:" + host + "\n"
        "x-amz-date:" + amzDate + "\n";
    std::string signedHeaders = "host;x-amz-date";

    std::string bodyHash = SHA256Hex(body);

    // Canonical request
    // canonicalHeaders already ends with '\n' for each header line.
    // AWS SigV4 spec requires: CanonicalHeaders + '\n' + SignedHeaders,
    // so we need an extra '\n' after the last header (blank line separator).
    std::string canonicalRequest =
        method + "\n" +
        uri + "\n" +
        "\n" +                   // empty query string
        canonicalHeaders +       // already ends with '\n'
        "\n" +                   // required blank line after headers
        signedHeaders + "\n" +
        bodyHash;

    // Credential scope
    std::string credentialScope = dateStamp + "/" + region + "/polly/aws4_request";

    // String to sign
    std::string stringToSign =
        "AWS4-HMAC-SHA256\n" +
        amzDate + "\n" +
        credentialScope + "\n" +
        SHA256Hex(canonicalRequest);

    // Signature
    auto signingKey = DeriveSigningKey(secretKey, dateStamp, region);
    auto sigBytes   = HmacSHA256(signingKey, stringToSign);
    std::string signature = HexEncode(sigBytes.data(), sigBytes.size());

    // Authorization header value
    return "AWS4-HMAC-SHA256 "
           "Credential=" + accessKey + "/" + credentialScope + ", "
           "SignedHeaders=" + signedHeaders + ", "
           "Signature=" + signature;
}

// ---------------------------------------------------------------------------
// Default voice selection by culture + gender
// Covers the most common DCS-relevant locales.
// ---------------------------------------------------------------------------
static std::string DefaultVoice(const std::string& culture, const std::string& gender) {
    bool male = (gender == "male");
    std::string loc = culture.empty() ? "en-US" : culture;

    if (loc == "en-US") return male ? "Matthew" : "Joanna";
    if (loc == "en-GB") return male ? "Arthur"  : "Amy";
    if (loc == "en-AU") return male ? "Olivia"  : "Olivia";  // neural only
    if (loc == "fr-FR") return male ? "Mathieu" : "Lea";
    if (loc == "fr-CA") return male ? "Liam"    : "Gabrielle";
    if (loc == "de-DE") return male ? "Daniel"  : "Vicki";
    if (loc == "ru-RU") return male ? "Maxim"   : "Tatyana";
    if (loc == "es-ES") return male ? "Sergio"  : "Lucia";
    if (loc == "es-US") return male ? "Miguel"  : "Lupe";
    if (loc == "it-IT") return male ? "Giorgio" : "Bianca";
    if (loc == "zh-CN") return "Zhiyu";
    if (loc == "ja-JP") return male ? "Takumi"  : "Mizuki";
    if (loc == "ko-KR") return "Seoyeon";
    if (loc == "pl-PL") return male ? "Jacek"   : "Ewa";
    if (loc == "pt-BR") return male ? "Ricardo" : "Camila";
    if (loc == "pt-PT") return male ? "Cristiano": "Ines";
    if (loc == "nl-NL") return male ? "Ruben"   : "Lotte";
    if (loc == "sv-SE") return "Astrid";
    if (loc == "nb-NO") return "Liv";
    if (loc == "da-DK") return "Naja";
    if (loc == "tr-TR") return "Filiz";
    if (loc == "ar-AE") return "Hala";

    // Fallback
    return male ? "Matthew" : "Joanna";
}

// ---------------------------------------------------------------------------
// Map speed multiplier to SSML prosody rate percentage string
// ---------------------------------------------------------------------------
static std::string SpeedToRate(double speed) {
    int pct = static_cast<int>((speed - 1.0) * 100.0);
    std::ostringstream ss;
    if (pct >= 0) ss << "+" << pct << "%";
    else          ss << pct << "%";
    return ss.str();
}

// ---------------------------------------------------------------------------
// JSON escape a string
// ---------------------------------------------------------------------------
static std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
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
bool PollyTTS::SynthesizeToQueue(
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
    AudioQueue& queue)
{
    if (accessKey.empty() || secretKey.empty() || region.empty()) {
        LogE("Polly access_key, secret_key, or region not configured");
        queue.MarkDone();
        return false;
    }

    std::string voiceId = voice.empty() ? DefaultVoice(culture, gender) : voice;
    std::string eng     = engine.empty() ? "standard" : engine;

    // Clamp speed
    if (speed < 0.2) speed = 0.2;
    if (speed > 5.0) speed = 5.0;

    // Wrap in SSML prosody if speed is not 1.0
    std::string inputText;
    std::string textType;
    if (std::abs(speed - 1.0) > 0.01) {
        inputText = "<speak><prosody rate='" + SpeedToRate(speed) + "'>" +
                    JsonEscape(text) + "</prosody></speak>";
        textType = "ssml";
    } else {
        inputText = JsonEscape(text);
        textType = "text";
    }

    // Build JSON body
    std::ostringstream bodyStream;
    bodyStream << "{"
               << "\"Text\":\"" << inputText << "\","
               << "\"TextType\":\"" << textType << "\","
               << "\"VoiceId\":\"" << voiceId << "\","
               << "\"OutputFormat\":\"pcm\","
               << "\"SampleRate\":\"16000\","
               << "\"Engine\":\"" << eng << "\""
               << "}";
    std::string body = bodyStream.str();

    // Compute timestamp strings
    std::time_t now = std::time(nullptr);
    std::tm utc = {};
#if defined(_MSC_VER)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char amzDateBuf[17], dateStampBuf[9];
    std::strftime(amzDateBuf,  sizeof(amzDateBuf),  "%Y%m%dT%H%M%SZ", &utc);
    std::strftime(dateStampBuf, sizeof(dateStampBuf), "%Y%m%d",         &utc);
    std::string amzDate  = amzDateBuf;
    std::string dateStamp = dateStampBuf;

    std::string authHeader = ComputeSigV4Auth(
        accessKey, secretKey, region,
        "POST", "/v1/speech",
        body, amzDate, dateStamp);

    std::string host = "polly." + region + ".amazonaws.com";
    httplib::SSLClient cli(host, 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    httplib::Headers headers = {
        {"Authorization", authHeader},
        {"X-Amz-Date",    amzDate},
        {"Content-Type",  "application/json"},
        {"User-Agent",    "HoundTTS"}
    };

    LogI("Connecting to " + host + " voice=" + voiceId + " engine=" + eng);
    LogI("Request body: " + body);
    LogI("Auth: " + authHeader);

    OpusFrameEncoder encoder;
    if (!encoder.Init()) {
        LogE("OpusFrameEncoder::Init() failed");
        queue.MarkDone();
        return false;
    }

    // First do a plain POST (no content receiver) so error bodies are preserved in res->body.
    // On success (200) the body contains raw PCM — process it after.
    auto res = cli.Post("/v1/speech", headers, body, "application/json");

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
    if (res->body.empty()) {
        LogE("No PCM data received (empty body)");
        queue.MarkDone();
        return false;
    }

    // Process raw PCM from body
    const char* data = res->body.data();
    size_t len = res->body.size();
    LogI("Received " + std::to_string(len) + " bytes of PCM");

    std::vector<uint8_t> leftover(reinterpret_cast<const uint8_t*>(data),
                                   reinterpret_cast<const uint8_t*>(data) + len);
    size_t sampleBytes = (leftover.size() / 2) * 2;
    if (sampleBytes > 0) {
        int16_t* samples = reinterpret_cast<int16_t*>(leftover.data());
        int sampleCount = static_cast<int>(sampleBytes / 2);
        if (volume < 1.0) {
            double vol = std::max(0.0, std::min(1.0, volume));
            for (int i = 0; i < sampleCount; ++i)
                samples[i] = static_cast<int16_t>(samples[i] * vol);
        }
        encoder.EncodeChunk(samples, sampleCount, queue);
    }

    encoder.Flush(queue);
    queue.MarkDone();
    return true;
}

} // namespace HoundTTS
