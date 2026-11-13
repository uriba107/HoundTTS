#include "azure_tts.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <cmath>
#include <cctype>
#include <cstring>
#include <algorithm>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Azure";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// Escape XML special characters before interpolating into SSML. Used for both
// text content and attribute values (voice name, locale); entity references
// decode back to the original characters in the SSML processor.
static std::string XmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        // XML 1.0 allows only tab, LF and CR below 0x20; anything else is a
        // parse error, so drop it rather than send malformed SSML.
        if (uc < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

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
    const std::string lang = culture.empty() ? "en-US" : culture;
    ssml << "<speak version='1.0' xml:lang='" << XmlEscape(lang) << "'>"
         << "<voice name='" << XmlEscape(voiceName) << "'>"
         << "<prosody rate='" << rateStr.str() << "'>"
         << XmlEscape(text)
         << "</prosody></voice></speak>";
    return ssml.str();
}

// Synthesis body. May throw (httplib, allocation); the public entry point
// below catches and guarantees queue.MarkDone().
static bool Synthesize(
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

    // If the caller already provided a full SSML document, use it directly.
    // Match the AWS/Google detection: skip leading whitespace and require a tag
    // boundary so "<speaker ..." or "<speakfoo" are treated as plain text.
    size_t ssmlOff = 0;
    while (ssmlOff < text.size() && std::isspace(static_cast<unsigned char>(text[ssmlOff])))
        ++ssmlOff;
    std::string ssml;
    bool isSsml = (text.size() - ssmlOff >= 6 &&
                   text.compare(ssmlOff, 6, "<speak") == 0 &&
                   (text.size() - ssmlOff == 6 ||
                    text[ssmlOff + 6] == '>' ||
                    std::isspace(static_cast<unsigned char>(text[ssmlOff + 6]))));
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

    // Azure returns a RIFF/WAVE container (riff-16khz-16bit-mono-pcm). Walk the
    // chunks to find the 'data' payload instead of assuming a 44-byte header.
    // A body without a RIFF header is treated as raw PCM.
    const char* pcmData = res->body.data();
    size_t pcmLen = res->body.size();
    if (pcmLen >= 12 && std::memcmp(pcmData, "RIFF", 4) == 0 &&
        std::memcmp(pcmData + 8, "WAVE", 4) == 0) {
        const char* data = nullptr;
        size_t dataLen = 0;
        for (size_t off = 12; off + 8 <= pcmLen; ) {
            uint32_t chunkSize = 0;
            std::memcpy(&chunkSize, pcmData + off + 4, sizeof(chunkSize)); // RIFF is little-endian
            if (std::memcmp(pcmData + off, "data", 4) == 0) {
                data = pcmData + off + 8;
                dataLen = std::min<size_t>(chunkSize, pcmLen - (off + 8));
                break;
            }
            off += 8 + static_cast<size_t>(chunkSize) + (chunkSize & 1); // chunks are word-aligned
        }
        if (!data || dataLen == 0) {
            LogE("No 'data' chunk found in WAV response (" + std::to_string(pcmLen) + " bytes)");
            queue.MarkDone();
            return false;
        }
        pcmData = data;
        pcmLen  = dataLen;
    }

    LogI("Received " + std::to_string(pcmLen) + " bytes of PCM");

    std::vector<int16_t> samples = Utils::PcmBytesToSamples(pcmData, pcmLen);
    if (!samples.empty()) {
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
    try {
        return Synthesize(text, key, region, voice, culture, gender, speed, volume, queue);
    } catch (const std::exception& e) {
        LogE(std::string("Synthesis threw: ") + e.what());
    } catch (...) {
        LogE("Synthesis threw: unknown exception");
    }
    queue.MarkDone();
    return false;
}

} // namespace HoundTTS
