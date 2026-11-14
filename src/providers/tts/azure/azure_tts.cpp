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

// Escape XML special characters before interpolating into SSML. Utils::XmlEscape
// is the shared policy (text content and attribute values; entity references
// decode back to the original characters in the SSML processor).

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
    // NaN/Inf would bypass the range clamps and make the int cast UB.
    if (!std::isfinite(speed)) speed = 1.0;
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0)  speed = 4.0;
    std::ostringstream rateStr;
    int ratePct = static_cast<int>(std::round((speed - 1.0) * 100.0));
    if (ratePct >= 0)
        rateStr << "+" << ratePct << "%";
    else
        rateStr << ratePct << "%";

    std::ostringstream ssml;
    const std::string lang = culture.empty() ? "en-US" : culture;
    ssml << "<speak version='1.0' xml:lang='" << Utils::XmlEscape(lang) << "'>"
         << "<voice name='" << Utils::XmlEscape(voiceName) << "'>"
         << "<prosody rate='" << rateStr.str() << "'>"
         << Utils::XmlEscape(text)
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
    // Note: completion (queue.MarkDone()) is owned solely by SynthesizeToQueue,
    // so a throw from an internal step cannot make it fire twice.
    if (key.empty() || region.empty()) {
        LogE("Azure key or region not configured");
        return false;
    }
    if (!Utils::IsSafeHostLabel(region, 32)) {
        LogE("Azure region is not a valid host label: " + region);
        return false;
    }
    if (Utils::HasControlChars(key)) {
        LogE("Azure key contains control characters");
        return false;
    }
    if (text.empty()) {
        LogE("Empty text — nothing to synthesize");
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
                const size_t remaining = pcmLen - (off + 8);
                // A size that does not fit the buffer means "not back-patched":
                // use what is actually left rather than reporting no audio. A
                // size of exactly 0 is a genuinely empty chunk — taking
                // `remaining` there would swallow trailing non-PCM chunks.
                if (chunkSize == 0)
                    dataLen = 0;
                else if (static_cast<size_t>(chunkSize) <= remaining)
                    dataLen = static_cast<size_t>(chunkSize);
                else
                    dataLen = remaining;
                break;
            }
            // Guard the advance: a hostile/corrupt size must not wrap and skip
            // past the 'data' chunk or read out of bounds.
            size_t advance = 8 + static_cast<size_t>(chunkSize);
            if (chunkSize & 1u) ++advance;  // chunks are word-aligned
            if (advance < 8 || advance > pcmLen - off) break;  // malformed
            off += advance;
        }
        if (!data || dataLen == 0) {
            LogE("No 'data' chunk found in WAV response (" + std::to_string(pcmLen) + " bytes)");
            return false;
        }
        pcmData = data;
        pcmLen  = dataLen;
    }

    LogI("Received " + std::to_string(pcmLen) + " bytes of PCM");

    std::vector<int16_t> samples = Utils::PcmBytesToSamples(pcmData, pcmLen);
    if (samples.empty()) {
        // A success status with no complete sample is a failure, not silence
        // (odd 'data' length, 1-byte body) — matches Google's size guard.
        LogE("No PCM samples decoded from response (" + std::to_string(pcmLen) + " bytes)");
        return false;
    }
    volume = Utils::NormalizeVolume(volume);
    if (volume < 1.0) {
        for (auto& s : samples)
            s = static_cast<int16_t>(s * volume);
    }
    queue.Push(std::move(samples));
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
    // Single owner of completion: Synthesize() never calls MarkDone(), so a
    // throw from one of its steps cannot deliver it twice.
    bool result = false;
    try {
        result = Synthesize(text, key, region, voice, culture, gender, speed, volume, queue);
    } catch (const std::exception& e) {
        LogE(std::string("Synthesis threw: ") + e.what());
    } catch (...) {
        LogE("Synthesis threw: unknown exception");
    }
    queue.MarkDone();
    return result;
}

} // namespace HoundTTS
