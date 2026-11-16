#include "aws_tts.h"
#include "providers/shared/aws/aws_auth.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <ctime>
#include <cmath>
#include <cctype>
#include <algorithm>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Polly";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// Escape XML special characters before interpolating plain text into SSML.
// Polly parses the request `Text` as SSML, so a bare '&' or '<' in the source
// text would produce a 400 instead of speech. Utils::XmlEscape is the shared
// SSML sanitiser (same policy as Azure).

static std::string DefaultVoice(const std::string& culture, const std::string& gender) {
    bool male = (gender == "male");
    std::string loc = culture.empty() ? "en-US" : culture;
    if (loc == "en-US") return male ? "Matthew" : "Joanna";
    if (loc == "en-GB") return male ? "Arthur"  : "Amy";
    if (loc == "fr-FR") return male ? "Mathieu" : "Lea";
    if (loc == "de-DE") return male ? "Daniel"  : "Vicki";
    if (loc == "ru-RU") return male ? "Maxim"   : "Tatyana";
    if (loc == "es-ES") return male ? "Sergio"  : "Lucia";
    if (loc == "it-IT") return male ? "Giorgio" : "Bianca";
    if (loc == "pt-BR") return male ? "Ricardo" : "Camila";
    if (loc == "pl-PL") return male ? "Jacek"   : "Ewa";
    return male ? "Matthew" : "Joanna";
}

static std::string SpeedToRate(double speed) {
    int pct = static_cast<int>((speed - 1.0) * 100.0);
    std::ostringstream ss;
    if (pct >= 0) ss << "+" << pct << "%";
    else          ss << pct << "%";
    return ss.str();
}

// Synthesis body. May throw (httplib, allocation); the public entry point
// below catches and guarantees queue.MarkDone().
static bool Synthesize(
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
    PCMQueue& queue)
{
    // Note: completion (queue.MarkDone()) is owned solely by SynthesizeToQueue,
    // so a throw from an internal step cannot make it fire twice.
    if (accessKey.empty() || secretKey.empty() || region.empty()) {
        LogE("Polly access_key, secret_key, or region not configured");
        return false;
    }
    // Credentials are interpolated into the Authorization header; reject
    // control characters here rather than reporting a generic signing failure.
    if (Utils::HasControlChars(accessKey) || Utils::HasControlChars(secretKey)) {
        LogE("Polly credentials contain control characters");
        return false;
    }
    if (!AwsAuth::IsValidAwsRegion(region)) {
        LogE("Polly region is not a valid AWS region: " + region);
        return false;
    }
    if (text.empty()) {
        return false;
    }

    std::string voiceId = voice.empty() ? DefaultVoice(culture, gender) : voice;
    std::string eng     = engine.empty() ? "standard" : engine;

    // NaN/Inf bypass both comparisons; SpeedToRate's int cast on NaN is UB.
    if (!std::isfinite(speed)) speed = 1.0;
    if (speed < 0.2) speed = 0.2;
    if (speed > 5.0) speed = 5.0;
    volume = Utils::NormalizeVolume(volume);

    size_t ssmlOff = 0;
    while (ssmlOff < text.size() && std::isspace((unsigned char)text[ssmlOff])) ++ssmlOff;
    bool isSsml = (text.size() - ssmlOff >= 6 &&
                   text.compare(ssmlOff, 6, "<speak") == 0 &&
                   (text.size() - ssmlOff == 6 ||
                    text[ssmlOff + 6] == '>' ||
                    std::isspace((unsigned char)text[ssmlOff + 6])));

    std::string inputText, textType;
    if (isSsml) {
        inputText = Utils::JsonEscape(text);
        textType = "ssml";
    } else if (std::abs(speed - 1.0) > 0.01) {
        // Plain text wrapped in SSML: XML-escape it first, then JSON-escape the
        // finished document for the request body (the isSsml path is already XML).
        inputText = Utils::JsonEscape(
            "<speak><prosody rate='" + SpeedToRate(speed) + "'>" +
            Utils::XmlEscape(text) + "</prosody></speak>");
        textType = "ssml";
    } else {
        inputText = Utils::JsonEscape(text);
        textType = "text";
    }

    std::ostringstream bodyStream;
    bodyStream << "{"
               << "\"Text\":\"" << inputText << "\","
               << "\"TextType\":\"" << textType << "\","
               << "\"VoiceId\":\"" << Utils::JsonEscape(voiceId) << "\","
               << "\"OutputFormat\":\"pcm\","
               << "\"SampleRate\":\"16000\","
               << "\"Engine\":\"" << Utils::JsonEscape(eng) << "\""
               << "}";
    std::string body = bodyStream.str();

    std::time_t now = std::time(nullptr);
    std::tm utc = {};
#if defined(_MSC_VER)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char amzDateBuf[17], dateStampBuf[9];
    std::strftime(amzDateBuf,   sizeof(amzDateBuf),   "%Y%m%dT%H%M%SZ", &utc);
    std::strftime(dateStampBuf, sizeof(dateStampBuf), "%Y%m%d",         &utc);
    std::string amzDate   = amzDateBuf;
    std::string dateStamp = dateStampBuf;

    std::string host = "polly." + region + ".amazonaws.com";
    std::string authHeader = AwsAuth::ComputeSigV4Auth(
        accessKey, secretKey, region, "polly", host,
        "POST", "/v1/speech", body, amzDate, dateStamp);
    if (authHeader.empty()) {
        LogE("SigV4 signing failed (empty Authorization header)");
        return false;
    }

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

    auto res = cli.Post("/v1/speech", headers, body, "application/json");
    if (!res) {
        LogE("HTTP request failed (connection error)");
        return false;
    }
    if (res->status != 200) {
        LogE("HTTP " + std::to_string(res->status) + " body=[" + res->body + "]");
        return false;
    }
    if (res->body.empty()) {
        LogE("No PCM data received (empty body)");
        return false;
    }

    const char* data = res->body.data();
    size_t len = res->body.size();
    LogI("Received " + std::to_string(len) + " bytes of PCM");

    size_t sampleBytes = (len / 2) * 2;
    std::vector<int16_t> samples = Utils::PcmBytesToSamples(data, sampleBytes);
    if (samples.empty()) {
        // A success status with no complete sample is a failure, not silence:
        // callers must not observe "ok" with no audio (matches Google/Azure).
        LogE("No PCM samples decoded from response (" + std::to_string(len) + " bytes)");
        return false;
    }
    if (volume < 1.0) {
        for (auto& s : samples)
            s = static_cast<int16_t>(s * volume);
    }
    queue.Push(std::move(samples));
    return true;
}

bool AwsTTS::SynthesizeToQueue(
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
    PCMQueue& queue)
{
    // Single owner of completion: Synthesize() never calls MarkDone(), so a
    // throw from one of its steps cannot deliver it twice.
    bool result = false;
    try {
        result = Synthesize(text, accessKey, secretKey, region, voice, engine,
                            culture, gender, speed, volume, queue);
    } catch (const std::exception& e) {
        LogE(std::string("Synthesis threw: ") + e.what());
    } catch (...) {
        LogE("Synthesis threw: unknown exception");
    }
    queue.MarkDone();
    return result;
}

} // namespace HoundTTS
