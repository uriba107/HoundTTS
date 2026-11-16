#include "google_tts.h"
#include "providers/shared/google/google_auth.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Google";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

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

static std::string MapGender(const std::string& gender) {
    if (gender == "male")   return "MALE";
    if (gender == "female") return "FEMALE";
    return "NEUTRAL";
}

bool GoogleTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& credsFile,
    const std::string& voice,
    const std::string& culture,
    const std::string& gender,
    double speed,
    double volume,
    PCMQueue& queue)
{
    // Completion backstop for direct (non-pipeline) callers: an internal throw
    // must still complete the queue. MarkDone is idempotent, so the explicit
    // failure-path calls below stay as they are.
    ScopedQueueDone doneGuard(queue);

    if (credsFile.empty()) {
        LogE("Google credentials file not configured in HoundTTS-credentials.ini");
        queue.MarkDone();
        return false;
    }

    std::string clientEmail, privateKey;
    if (!GoogleAuth::ReadServiceAccount(credsFile, clientEmail, privateKey)) {
        queue.MarkDone();
        return false;
    }

    LogI("Authenticating as " + clientEmail);
    std::string token = GoogleAuth::GetAccessToken(
        clientEmail, privateKey,
        "https://www.googleapis.com/auth/cloud-platform");
    if (token.empty()) {
        queue.MarkDone();
        return false;
    }

    std::string locale     = culture.empty() ? "en-US" : culture;
    std::string ssmlGender = MapGender(gender);

    if (!std::isfinite(speed)) speed = 1.0;
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0)  speed = 4.0;
    volume = Utils::NormalizeVolume(volume);

    // Match AWS/Azure: skip leading whitespace and require a tag boundary.
    size_t ssmlOff = 0;
    while (ssmlOff < text.size() && std::isspace(static_cast<unsigned char>(text[ssmlOff])))
        ++ssmlOff;
    bool isSsml = (text.size() - ssmlOff >= 6 &&
                   text.compare(ssmlOff, 6, "<speak") == 0 &&
                   (text.size() - ssmlOff == 6 ||
                    text[ssmlOff + 6] == '>' ||
                    std::isspace(static_cast<unsigned char>(text[ssmlOff + 6]))));

    std::ostringstream body;
    body << "{"
         << "\"input\":{\"" << (isSsml ? "ssml" : "text") << "\":\"" << GoogleAuth::JsonEscape(text) << "\"},"
         << "\"voice\":{"
         << "\"languageCode\":\"" << Utils::JsonEscape(locale) << "\","
         << "\"ssmlGender\":\"" << ssmlGender << "\"";
    if (!voice.empty())
        body << ",\"name\":\"" << Utils::JsonEscape(voice) << "\"";
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

    std::string b64 = GoogleAuth::JsonString(res->body, "audioContent");
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

    // memcpy into an int16 vector: casting the byte buffer to int16_t* is
    // undefined behaviour (alignment + strict aliasing).
    std::vector<int16_t> samples = Utils::PcmBytesToSamples(pcmRaw.data(), pcmRaw.size());
    if (volume < 1.0) {
        for (auto& s : samples)
            s = static_cast<int16_t>(s * volume);
    }
    queue.Push(std::move(samples));
    queue.MarkDone();

    return true;
}

} // namespace HoundTTS
