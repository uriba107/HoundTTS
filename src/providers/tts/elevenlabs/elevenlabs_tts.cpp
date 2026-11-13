#include "elevenlabs_tts.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <algorithm>
#include <array>
#include <cctype>

namespace HoundTTS {

static const char* kTag = "HoundTTS/ElevenLabs";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// Minimal base64 decoder
static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Decode(const std::string& in) {
    // Function-local static initialisation is thread-safe in C++11+; the old
    // lazy `init` flag raced when two synthesis threads decoded concurrently.
    static const std::array<int, 256> table = [] {
        std::array<int, 256> t;
        t.fill(-1);
        for (int i = 0; i < 64; i++) t[static_cast<unsigned char>(kB64Chars[i])] = i;
        return t;
    }();

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

// voice_id / model_id are interpolated into the URL path and query string.
// Accept only the characters ElevenLabs uses so a stray '&', '?', '#' or space
// cannot rewrite the request.
static bool IsSafeUrlToken(const std::string& s, const char* extraChars) {
    if (s.empty() || s.size() > 128) return false;
    for (unsigned char c : s) {
        // Explicit ASCII check: std::isalnum is locale-dependent and would let
        // locale-specific letters into the request URL.
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            continue;
        bool allowed = false;
        for (const char* p = extraChars; *p; ++p)
            if (*p == static_cast<char>(c)) { allowed = true; break; }
        if (!allowed) return false;
    }
    return true;
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

bool ElevenLabsTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& apiKey,
    const std::string& modelId,
    const std::string& voiceId,
    double speed,
    double volume,
    PCMQueue& queue)
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

    // voiceId and model are interpolated into the URL path/query. Reject
    // anything outside the character set ElevenLabs uses (Lua-supplied values).
    if (!IsSafeUrlToken(voiceId, "_-")) {
        LogE("ElevenLabs voice ID contains unsupported characters");
        queue.MarkDone();
        return false;
    }
    if (!IsSafeUrlToken(model, "_-.")) {
        LogE("ElevenLabs model ID contains unsupported characters");
        queue.MarkDone();
        return false;
    }

    // WebSocket path for streaming input
    std::string path = "/v1/text-to-speech/" + voiceId +
                       "/stream-input?model_id=" + model +
                       "&output_format=pcm_16000";

    LogI("Connecting WebSocket to api.elevenlabs.io" + path);

    bool success = false;
    std::vector<uint8_t> leftover;

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
        << "\"xi_api_key\":\"" << Utils::JsonEscape(apiKey) << "\","
        << "\"model_id\":\"" << Utils::JsonEscape(model) << "\""
        << "}";
    if (!ws.send(bos.str())) {
        LogE("WebSocket send failed (BOS frame)");
        ws.close();
        queue.MarkDone();
        return false;
    }

    // Send text chunk
    std::ostringstream textMsg;
    textMsg << "{"
            << "\"text\":\"" << Utils::JsonEscape(text) << "\","
            << "\"flush\":true"
            << "}";
    if (!ws.send(textMsg.str())) {
        LogE("WebSocket send failed (text frame)");
        ws.close();
        queue.MarkDone();
        return false;
    }

    // Send EOS
    if (!ws.send("{\"text\":\"\"}")) {
        LogE("WebSocket send failed (EOS frame)");
        ws.close();
        queue.MarkDone();
        return false;
    }

    // Read audio responses until connection closes
    std::string msg;
    while (ws.read(msg) != httplib::ws::ReadResult::Fail) {
        std::string b64 = ExtractJsonString(msg, "audio");
        if (b64.empty()) continue;

        std::string pcmRaw = Base64Decode(b64);
        if (pcmRaw.empty()) continue;

        leftover.insert(leftover.end(), pcmRaw.begin(), pcmRaw.end());

        size_t sampleBytes = (leftover.size() / 2) * 2;
        if (sampleBytes == 0) continue;

        // memcpy into an int16 vector: casting the byte buffer to int16_t* is
        // undefined behaviour (alignment + strict aliasing).
        std::vector<int16_t> samples = Utils::PcmBytesToSamples(leftover.data(), sampleBytes);
        if (volume < 1.0) {
            double vol = std::max(0.0, std::min(1.0, volume));
            for (auto& s : samples)
                s = static_cast<int16_t>(s * vol);
        }
        queue.Push(std::move(samples));

        size_t remainder = leftover.size() - sampleBytes;
        if (remainder > 0)
            leftover = std::vector<uint8_t>(leftover.end() - remainder, leftover.end());
        else
            leftover.clear();

        success = true;
    }

    ws.close();
    queue.MarkDone();

    if (!success) LogE("No audio received from ElevenLabs");
    return success;
}

} // namespace HoundTTS
