#include "elevenlabs_tts.h"
#include "opus_encoder.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <algorithm>

namespace HoundTTS {

static const char* kTag = "HoundTTS/ElevenLabs";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// Minimal base64 decoder (shared pattern with google_tts)
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

// Escape text for JSON string
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

// Extract a JSON string field value (minimal, no external dep)
static std::string ExtractJsonString(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + key.size());
    if (pos == std::string::npos) return "";
    ++pos;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// ElevenLabs WebSocket streaming TTS
// Uses the /v1/text-to-speech/{voice_id}/stream-input WebSocket endpoint.
// Sends text in one chunk, receives audio/pcm_16000 chunks.
// cpp-httplib supports WebSocket via httplib::WebSocketClient.
bool ElevenLabsTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& apiKey,
    const std::string& modelId,
    const std::string& voiceId,
    double speed,
    double volume,
    AudioQueue& queue)
{
    if (apiKey.empty()) {
        LogE("ElevenLabs API key not configured");
        queue.MarkDone();
        return false;
    }
    if (voiceId.empty()) {
        LogE("ElevenLabs voice ID not set (pass via provider_params.voice)");
        queue.MarkDone();
        return false;
    }

    // Clamp speed to ElevenLabs supported range
    if (speed < 0.7) speed = 0.7;
    if (speed > 1.2) speed = 1.2;

    std::string model = modelId.empty() ? "eleven_turbo_v2" : modelId;

    // WebSocket path for streaming input
    std::string path = "/v1/text-to-speech/" + voiceId +
                       "/stream-input?model_id=" + model +
                       "&output_format=pcm_16000";

    LogI("Connecting WebSocket to api.elevenlabs.io" + path);

    OpusFrameEncoder encoder;
    if (!encoder.Init()) {
        LogE("OpusFrameEncoder::Init() failed");
        queue.MarkDone();
        return false;
    }

    bool success = false;
    std::vector<uint8_t> leftover;

    // cpp-httplib ws API: WebSocketClient(url, headers), connect(), send(), read() loop
    std::string url = "wss://api.elevenlabs.io" + path;
    httplib::Headers headers{{"xi-api-key", apiKey}};
    httplib::ws::WebSocketClient ws(url, headers);

    if (!ws.connect()) {
        LogE("WebSocket connect() failed");
        queue.MarkDone();
        return false;
    }

    // Send BOS (beginning of stream)
    std::ostringstream bos;
    bos << "{"
        << "\"text\":\" \","
        << "\"voice_settings\":{\"stability\":0.5,\"similarity_boost\":0.8,\"speed\":" << speed << "},"
        << "\"xi_api_key\":\"" << apiKey << "\","
        << "\"model_id\":\"" << model << "\""
        << "}";
    ws.send(bos.str());

    // Send text chunk
    std::ostringstream textMsg;
    textMsg << "{"
            << "\"text\":\"" << JsonEscape(text) << "\","
            << "\"flush\":true"
            << "}";
    ws.send(textMsg.str());

    // Send EOS
    ws.send("{\"text\":\"\"}");

    // Read audio responses until connection closes (ReadResult::Fail = 0 means done)
    std::string msg;
    while (ws.read(msg) != httplib::ws::ReadResult::Fail) {
        std::string b64 = ExtractJsonString(msg, "audio");
        if (b64.empty()) continue;

        std::string pcmRaw = Base64Decode(b64);
        if (pcmRaw.empty()) continue;

        leftover.insert(leftover.end(), pcmRaw.begin(), pcmRaw.end());

        size_t sampleBytes = (leftover.size() / 2) * 2;
        if (sampleBytes == 0) continue;

        int16_t* samples = reinterpret_cast<int16_t*>(leftover.data());
        int sampleCount = static_cast<int>(sampleBytes / 2);
        if (volume < 1.0) {
            double vol = std::max(0.0, std::min(1.0, volume));
            for (int i = 0; i < sampleCount; ++i)
                samples[i] = static_cast<int16_t>(samples[i] * vol);
        }
        encoder.EncodeChunk(samples, sampleCount, queue);

        size_t remainder = leftover.size() - sampleBytes;
        if (remainder > 0)
            leftover = std::vector<uint8_t>(leftover.end() - remainder, leftover.end());
        else
            leftover.clear();

        success = true;
    }

    ws.close();
    encoder.Flush(queue);
    queue.MarkDone();

    if (!success) LogE("No audio received from ElevenLabs");
    return success;
}

} // namespace HoundTTS
