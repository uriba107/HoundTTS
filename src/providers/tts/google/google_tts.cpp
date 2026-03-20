#include "google_tts.h"
#include "providers/shared/google/google_auth.h"
#include "utils.h"

#include "httplib.h"

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <algorithm>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Google";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

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

    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0)  speed = 4.0;

    bool isSsml = (text.size() >= 6 && text.compare(0, 6, "<speak") == 0 &&
                   (text.size() == 6 || text[6] == '>' || std::isspace((unsigned char)text[6])));

    std::ostringstream body;
    body << "{"
         << "\"input\":{\"" << (isSsml ? "ssml" : "text") << "\":\"" << GoogleAuth::JsonEscape(text) << "\"},"
         << "\"voice\":{"
         << "\"languageCode\":\"" << locale << "\","
         << "\"ssmlGender\":\"" << ssmlGender << "\"";
    if (!voice.empty())
        body << ",\"name\":\"" << voice << "\"";
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

    int sampleCount = static_cast<int>(pcmRaw.size() / 2);
    std::vector<int16_t> samples(reinterpret_cast<const int16_t*>(pcmRaw.data()),
                                  reinterpret_cast<const int16_t*>(pcmRaw.data()) + sampleCount);
    if (volume < 1.0) {
        double vol = std::max(0.0, std::min(1.0, volume));
        for (auto& s : samples)
            s = static_cast<int16_t>(s * vol);
    }
    queue.Push(std::move(samples));
    queue.MarkDone();

    return true;
}

} // namespace HoundTTS
