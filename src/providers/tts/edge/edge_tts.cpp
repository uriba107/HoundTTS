#include "edge_tts.h"
#include "edge_drm.h"
#include "codecs/mp3_decoder.h"
#include "config_reader.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <ctime>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Edge";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }
static void LogD(const std::string& msg) { HoundTTS::Logger::Instance().Debug(kTag, msg); }

// Generate a UUID-hex (no dashes) for ConnectionId
static std::string GenerateConnectionId() {
    return EdgeDRM::GenerateMuid();  // same format: 32-char random hex
}

// Escape text for XML (SSML)
static std::string XmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
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

// Remove control characters that the service rejects
static std::string RemoveIncompatibleChars(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c <= 8 || (c >= 11 && c <= 12) || (c >= 14 && c <= 31))
            out += ' ';
        else
            out += static_cast<char>(c);
    }
    return out;
}

// Build JS-style date string for X-Timestamp header
static std::string DateToString() {
    time_t now;
    time(&now);
    struct tm gmt;
#ifdef _WIN32
    gmtime_s(&gmt, &now);
#else
    gmtime_r(&now, &gmt);
#endif
    char buf[128];
    strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)", &gmt);
    return std::string(buf);
}

// Resolve voice name from voice/culture/gender
static std::string ResolveVoice(const std::string& voice,
                                 const std::string& culture,
                                 const std::string& gender) {
    if (!voice.empty()) return voice;
    std::string loc = culture.empty() ? "en-US" : culture;
    if (gender == "male")
        return loc + "-GuyNeural";
    return loc + "-AriaNeural";
}

// Build SSML for Edge TTS
static std::string BuildSSML(const std::string& text,
                              const std::string& voiceName,
                              double speed) {
    // Map speed multiplier to SSML prosody rate percentage
    int ratePct = static_cast<int>(std::round((speed - 1.0) * 100.0));
    std::ostringstream rateStr;
    if (ratePct >= 0)
        rateStr << "+" << ratePct << "%";
    else
        rateStr << ratePct << "%";

    std::ostringstream ssml;
    ssml << "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>"
         << "<voice name='" << voiceName << "'>"
         << "<prosody pitch='+0Hz' rate='" << rateStr.str() << "' volume='+0%'>"
         << text
         << "</prosody></voice></speak>";
    return ssml.str();
}

// Build speech.config message
static std::string BuildSpeechConfig() {
    return "{\"context\":{\"synthesis\":{\"audio\":{"
           "\"metadataoptions\":{"
           "\"sentenceBoundaryEnabled\":\"false\","
           "\"wordBoundaryEnabled\":\"false\""
           "},"
           "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\""
           "}}}}\r\n";
}

// Build the speech.config text frame with headers
static std::string BuildSpeechConfigFrame() {
    std::string timestamp = DateToString();
    return "X-Timestamp:" + timestamp + "\r\n"
           "Content-Type:application/json; charset=utf-8\r\n"
           "Path:speech.config\r\n\r\n" +
           BuildSpeechConfig();
}

// Build the SSML text frame with headers
static std::string BuildSSMLFrame(const std::string& ssml) {
    std::string requestId = GenerateConnectionId();
    std::string timestamp = DateToString();
    return "X-RequestId:" + requestId + "\r\n"
           "Content-Type:application/ssml+xml\r\n"
           "X-Timestamp:" + timestamp + "Z\r\n"
           "Path:ssml\r\n\r\n" +
           ssml;
}

// Parse Path header from a text message
static std::string ExtractPath(const std::string& msg) {
    // Headers end at \r\n\r\n
    auto sep = msg.find("\r\n\r\n");
    if (sep == std::string::npos) return "";

    std::string headers = msg.substr(0, sep);
    // Find "Path:" (case-sensitive, matching the protocol)
    std::string pathKey = "Path:";
    auto pos = headers.find(pathKey);
    if (pos == std::string::npos) return "";

    pos += pathKey.size();
    auto end = headers.find("\r\n", pos);
    if (end == std::string::npos)
        return headers.substr(pos);
    return headers.substr(pos, end - pos);
}

// Attempt one WebSocket synthesis session. Returns true if audio was received.
static bool TrySynthesize(const std::string& text,
                           const std::string& voiceName,
                           double speed,
                           double volume,
                           PCMQueue& queue,
                           bool& got403) {
    got403 = false;

    std::string connectionId = GenerateConnectionId();
    std::string url = EdgeDRM::BuildWebSocketUrl(connectionId);

    httplib::Headers headers{
        {"User-Agent",     EdgeDRM::BuildUserAgent()},
        {"Origin",         EdgeDRM::BuildOrigin()},
        {"Cookie",         EdgeDRM::BuildCookieHeader()},
        {"Pragma",         "no-cache"},
        {"Cache-Control",  "no-cache"},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
    };

    LogI("Connecting to speech.platform.bing.com (Edge TTS)");
    LogD("URL: " + url);

    httplib::ws::WebSocketClient ws(url, headers);
    ws.set_read_timeout(60);

    if (!ws.is_valid()) {
        LogE("WebSocket URL parse failed — check URL format");
        return false;
    }

    if (!ws.connect()) {
        LogE("WebSocket connect() failed — possible 403 or network error");
        got403 = true;  // assume 403 for retry logic
        return false;
    }

    // Step 1: Send speech.config
    std::string configFrame = BuildSpeechConfigFrame();
    ws.send(configFrame);
    LogD("Sent speech.config");

    // Step 2: Build and send SSML
    std::string cleanText = RemoveIncompatibleChars(text);
    std::string escapedText = XmlEscape(cleanText);
    std::string ssml = BuildSSML(escapedText, voiceName, speed);
    std::string ssmlFrame = BuildSSMLFrame(ssml);
    ws.send(ssmlFrame);
    LogD("Sent SSML request");

    // Step 3: Read responses
    bool audioReceived = false;
    Mp3Decoder mp3dec;
    std::string msg;
    httplib::ws::ReadResult ret;
    int frameCount = 0;

    // Pre-buffer: accumulate PCM before first push so the downstream SRS
    // startup buffer has time to fill. Configurable via [Edge] buffer_ms
    // in HoundTTS-credentials.ini (default 200ms, range 0-5000).
    const int bufMs = ConfigReader::Instance().GetEdgeBufferMs();
    const size_t preBufTarget = static_cast<size_t>(bufMs) * 16;  // ms → samples @ 16kHz
    std::vector<int16_t> preBuf;
    bool preBufFlushed = (preBufTarget == 0);

    while ((ret = ws.read(msg)) != httplib::ws::ReadResult::Fail) {
        frameCount++;
        if (ret == httplib::ws::ReadResult::Text) {
            // Text frame — check Path header
            std::string path = ExtractPath(msg);
            LogD("Text frame: Path=" + path);
            if (path == "turn.end") {
                LogD("Received turn.end");
                break;
            }
            // Log body of non-trivial frames (response may contain errors)
            if (path != "turn.start") {
                // Body starts after double-CRLF header/body separator
                auto bodyPos = msg.find("\r\n\r\n");
                if (bodyPos != std::string::npos) {
                    std::string body = msg.substr(bodyPos + 4);
                    if (!body.empty()) {
                        LogD("Frame body: " + body.substr(0, 500));
                    }
                }
            }
            continue;

        } else if (ret == httplib::ws::ReadResult::Binary) {
            // Binary frame: first 2 bytes = header length (big-endian)
            if (msg.size() < 2) continue;

            uint16_t headerLen = (static_cast<uint8_t>(msg[0]) << 8)
                               | static_cast<uint8_t>(msg[1]);

            if (static_cast<size_t>(2 + headerLen) > msg.size()) continue;

            // Parse headers to verify this is audio
            std::string binHeaders(msg.data() + 2, headerLen);
            if (binHeaders.find("Path:audio") == std::string::npos) continue;

            // Check Content-Type — skip frames with no content type (stream terminator)
            if (binHeaders.find("Content-Type:") == std::string::npos) continue;

            // Extract MP3 data after headers
            const uint8_t* mp3Data = reinterpret_cast<const uint8_t*>(msg.data() + 2 + headerLen);
            size_t mp3Len = msg.size() - 2 - headerLen;
            if (mp3Len == 0) continue;

            // Decode MP3 → 16kHz mono PCM (decoder keeps state + leftover bytes)
            auto samples = mp3dec.Decode(mp3Data, mp3Len);
            if (!samples.empty()) {
                if (volume < 1.0) {
                    double vol = std::max(0.0, std::min(1.0, volume));
                    for (auto& s : samples)
                        s = static_cast<int16_t>(s * vol);
                }

                if (!preBufFlushed) {
                    // Accumulate into pre-buffer
                    preBuf.insert(preBuf.end(), samples.begin(), samples.end());
                    if (preBuf.size() >= preBufTarget) {
                        queue.Push(std::move(preBuf));
                        preBuf.clear();
                        preBufFlushed = true;
                    }
                } else {
                    queue.Push(std::move(samples));
                }
                audioReceived = true;
            }
        }
    }

    // Flush any remaining pre-buffer (short utterances that never hit threshold)
    if (!preBufFlushed && !preBuf.empty()) {
        queue.Push(std::move(preBuf));
    }

    if (frameCount == 0) {
        LogD("ws.read() returned Fail immediately — server may have closed connection");
    } else {
        LogD("Read loop done: " + std::to_string(frameCount) + " frames, audio=" + (audioReceived ? "yes" : "no"));
    }

    ws.close();
    return audioReceived;
}

bool EdgeTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& voice,
    const std::string& culture,
    const std::string& gender,
    double speed,
    double volume,
    PCMQueue& queue)
{
    std::string voiceName = ResolveVoice(voice, culture, gender);
    LogI("Edge TTS: voice=" + voiceName + " speed=" + std::to_string(speed));

    bool got403 = false;
    bool success = TrySynthesize(text, voiceName, speed, volume, queue, got403);

    if (!success && got403) {
        // Retry once with clock skew adjustment.
        // We can't parse the 403 Date header from WebSocket failure,
        // so apply a heuristic: nudge clock by +5 seconds.
        LogI("Edge TTS: first attempt failed (possible 403), retrying with clock skew adjustment");
        EdgeDRM::AdjustClockSkew(5.0);
        success = TrySynthesize(text, voiceName, speed, volume, queue, got403);
    }

    queue.MarkDone();

    if (!success) LogE("Edge TTS: no audio received");
    return success;
}

} // namespace HoundTTS
