#include "azure_tts.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <cmath>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Azure";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// Build SSML for Azure TTS
static std::string BuildSSML(const std::string& text,
                              const std::string& voice,
                              const std::string& culture,
                              const std::string& gender,
                              double speed)
{
    // Resolve voice name
    std::string voiceName = voice;
    if (voiceName.empty()) {
        // Default voices per gender
        std::string loc = culture.empty() ? "en-US" : culture;
        if (gender == "male")
            voiceName = loc + "-GuyNeural";
        else
            voiceName = loc + "-AriaNeural";
    }

    // Map speed multiplier to SSML prosody rate percentage
    // 1.0 = 0%, 2.0 = +100%, 0.5 = -50%
    std::ostringstream rateStr;
    int ratePct = static_cast<int>(std::round((speed - 1.0) * 100.0));
    if (ratePct >= 0)
        rateStr << "+" << ratePct << "%";
    else
        rateStr << ratePct << "%";

    std::ostringstream ssml;
    ssml << "<speak version='1.0' xml:lang='" << (culture.empty() ? "en-US" : culture) << "'>"
         << "<voice name='" << voiceName << "'>"
         << "<prosody rate='" << rateStr.str() << "'>"
         << text
         << "</prosody></voice></speak>";
    return ssml.str();
}

bool AzureTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& key,
    const std::string& region,
    const std::string& voice,
    const std::string& culture,
    const std::string& gender,
    double speed,
    double volume,
    PCMQueue& queue)
{
    if (key.empty() || region.empty()) {
        LogE("Azure key or region not configured");
        queue.MarkDone();
        return false;
    }

    // If the caller already provided a full SSML document, use it directly
    std::string ssml;
    bool isSsml = (text.size() >= 6 && text.compare(0, 6, "<speak") == 0);
    if (isSsml)
        ssml = text;
    else
        ssml = BuildSSML(text, voice, culture, gender, speed);
    std::string host = region + ".tts.speech.microsoft.com";
    LogI("Connecting to " + host);

    httplib::SSLClient cli(host, 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    httplib::Headers headers = {
        {"Ocp-Apim-Subscription-Key", key},
        {"Content-Type", "application/ssml+xml"},
        {"X-Microsoft-OutputFormat", "riff-16khz-16bit-mono-pcm"},
        {"User-Agent", "HoundTTS"}
    };

    LogI("SSML: " + ssml);

    // Plain POST — keeps error body in res->body for logging
    auto res = cli.Post("/cognitiveservices/v1", headers, ssml, "application/ssml+xml");

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

    // Azure returns RIFF WAV: skip 44-byte header, rest is raw 16kHz 16-bit mono PCM
    static const size_t kWavHeaderSize = 44;
    const char* pcmData = res->body.data();
    size_t pcmLen = res->body.size();
    if (pcmLen > kWavHeaderSize) {
        pcmData += kWavHeaderSize;
        pcmLen  -= kWavHeaderSize;
    } else {
        LogE("Response too short to contain WAV header (" + std::to_string(pcmLen) + " bytes)");
        queue.MarkDone();
        return false;
    }

    LogI("Received " + std::to_string(pcmLen) + " bytes of PCM");

    size_t sampleBytes = (pcmLen / 2) * 2;
    if (sampleBytes > 0) {
        const int16_t* src = reinterpret_cast<const int16_t*>(pcmData);
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
