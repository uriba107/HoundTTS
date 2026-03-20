#include "aws_tts.h"
#include "providers/shared/aws/aws_auth.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Polly";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

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
    if (accessKey.empty() || secretKey.empty() || region.empty()) {
        LogE("Polly access_key, secret_key, or region not configured");
        queue.MarkDone();
        return false;
    }

    std::string voiceId = voice.empty() ? DefaultVoice(culture, gender) : voice;
    std::string eng     = engine.empty() ? "standard" : engine;

    if (speed < 0.2) speed = 0.2;
    if (speed > 5.0) speed = 5.0;

    bool isSsml = (text.size() >= 7 && text.compare(0, 7, "<speak>") == 0);

    std::string inputText, textType;
    if (isSsml) {
        inputText = JsonEscape(text);
        textType = "ssml";
    } else if (std::abs(speed - 1.0) > 0.01) {
        inputText = "<speak><prosody rate='" + SpeedToRate(speed) + "'>" +
                    JsonEscape(text) + "</prosody></speak>";
        textType = "ssml";
    } else {
        inputText = JsonEscape(text);
        textType = "text";
    }

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

    const char* data = res->body.data();
    size_t len = res->body.size();
    LogI("Received " + std::to_string(len) + " bytes of PCM");

    size_t sampleBytes = (len / 2) * 2;
    if (sampleBytes > 0) {
        const int16_t* src = reinterpret_cast<const int16_t*>(data);
        int sampleCount = static_cast<int>(sampleBytes / 2);
        std::vector<int16_t> samples(src, src + sampleCount);
        if (volume < 1.0) {
            double vol = std::max(0.0, std::min(1.0, volume));
            for (auto& s : samples)
                s = static_cast<int16_t>(s * vol);
        }
        queue.Push(std::move(samples));
    }

    queue.MarkDone();
    return true;
}

} // namespace HoundTTS
