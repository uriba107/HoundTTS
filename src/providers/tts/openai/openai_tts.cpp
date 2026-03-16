#include "openai_tts.h"
#include "utils.h"

#include "httplib.h"
#include <opus/opus.h>

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace HoundTTS {

static const char* kTag = "HoundTTS/OpenAI";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

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

static bool ParseEndpoint(const std::string& endpoint,
                           bool& outSsl, std::string& outHost,
                           int& outPort, std::string& outPath)
{
    outSsl  = false;
    outHost = "localhost";
    outPort = 443;
    outPath = "/v1/audio/speech";

    std::string url = endpoint;
    if (url.substr(0, 8) == "https://") { outSsl = true;  url = url.substr(8); }
    else if (url.substr(0, 7) == "http://") { outSsl = false; url = url.substr(7); }

    auto slashPos = url.find('/');
    std::string hostPort = (slashPos != std::string::npos) ? url.substr(0, slashPos) : url;
    std::string pathPrefix;
    if (slashPos != std::string::npos) {
        pathPrefix = url.substr(slashPos);
        while (pathPrefix.size() > 1 && pathPrefix.back() == '/')
            pathPrefix.pop_back();
    }
    outPath = pathPrefix + "/v1/audio/speech";

    auto colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        outHost = hostPort.substr(0, colonPos);
        try { outPort = std::stoi(hostPort.substr(colonPos + 1)); }
        catch (...) { outPort = outSsl ? 443 : 80; }
    } else {
        outHost = hostPort;
        outPort = outSsl ? 443 : 80;
    }
    return !outHost.empty();
}

static uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
static uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static std::vector<int16_t> Resample(const std::vector<int16_t>& in,
                                      int srcRate, int dstRate = 16000)
{
    if (srcRate == dstRate) return in;
    if (in.empty()) return {};
    size_t outLen = static_cast<size_t>(static_cast<int64_t>(in.size()) * dstRate / srcRate);
    if (outLen == 0) return {};
    std::vector<int16_t> out(outLen);
    double ratio = static_cast<double>(srcRate) / dstRate;
    for (size_t i = 0; i < outLen; ++i) {
        double srcPos = i * ratio;
        size_t idx = static_cast<size_t>(srcPos);
        double frac = srcPos - idx;
        int16_t a = in[idx];
        int16_t b = (idx + 1 < in.size()) ? in[idx + 1] : a;
        out[i] = static_cast<int16_t>(a + frac * (b - a));
    }
    return out;
}

static bool DecodeWavAndPush(const std::string& body, double volume, PCMQueue& queue) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(body.data());
    size_t len = body.size();
    if (len < 44) return false;
    if (std::memcmp(data, "RIFF", 4) != 0) return false;
    if (std::memcmp(data + 8, "WAVE", 4) != 0) return false;

    size_t pos = 12;
    uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const uint8_t* pcmStart = nullptr;
    uint32_t pcmBytes = 0;

    while (pos + 8 <= len) {
        char chunkId[5] = {};
        std::memcpy(chunkId, data + pos, 4);
        uint32_t chunkSize = ReadLE32(data + pos + 4);
        pos += 8;
        if (pos + chunkSize > len) break;
        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat   = ReadLE16(data + pos);
            numChannels   = ReadLE16(data + pos + 2);
            sampleRate    = ReadLE32(data + pos + 4);
            bitsPerSample = ReadLE16(data + pos + 14);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            pcmStart = data + pos;
            pcmBytes = chunkSize;
        }
        pos += chunkSize;
        if (pos & 1) pos++;
    }

    if ((audioFormat != 1 && audioFormat != 3) || !pcmStart || pcmBytes == 0) return false;
    if (audioFormat == 1 && bitsPerSample != 16) return false;
    if (audioFormat == 3 && bitsPerSample != 32) return false;
    if (sampleRate == 0) return false;

    uint16_t bytesPerSample = bitsPerSample / 8;
    size_t sampleBytes = (pcmBytes / bytesPerSample) * bytesPerSample;
    if (sampleBytes == 0) return false;

    size_t totalFrames = sampleBytes / bytesPerSample;
    std::vector<int16_t> raw(totalFrames);

    if (audioFormat == 1) {
        std::memcpy(raw.data(), pcmStart, totalFrames * 2);
    } else {
        LogI("WAV format is IEEE float32, converting to int16");
        const float* floatData = reinterpret_cast<const float*>(pcmStart);
        for (size_t i = 0; i < totalFrames; ++i) {
            float s = floatData[i];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            raw[i] = static_cast<int16_t>(s * 32767.0f);
        }
    }

    std::vector<int16_t> mono;
    if (numChannels == 2) {
        mono.resize(totalFrames / 2);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = static_cast<int16_t>((static_cast<int32_t>(raw[i * 2]) +
                                             static_cast<int32_t>(raw[i * 2 + 1])) / 2);
    } else {
        mono = std::move(raw);
    }

    std::vector<int16_t> samples = Resample(mono, static_cast<int>(sampleRate));
    if (samples.empty()) return false;

    if (volume < 1.0) {
        double vol = std::max(0.0, std::min(1.0, volume));
        for (auto& s : samples)
            s = static_cast<int16_t>(s * vol);
    }

    queue.Push(std::move(samples));
    return true;
}

struct OggPacket { std::vector<uint8_t> data; };

static std::vector<OggPacket> ParseOggPackets(const uint8_t* data, size_t len)
{
    std::vector<OggPacket> packets;
    size_t pos = 0;
    std::vector<uint8_t> partial;
    while (pos + 27 <= len) {
        if (std::memcmp(data + pos, "OggS", 4) != 0) break;
        uint8_t numSegs = data[pos + 26];
        if (pos + 27 + numSegs > len) break;
        size_t pageDataOffset = pos + 27 + numSegs;
        uint32_t pageDataSize = 0;
        for (uint8_t i = 0; i < numSegs; ++i) pageDataSize += data[pos + 27 + i];
        if (pageDataOffset + pageDataSize > len) break;
        size_t dataPos = pageDataOffset;
        for (uint8_t i = 0; i < numSegs; ++i) {
            uint8_t lace = data[pos + 27 + i];
            partial.insert(partial.end(), data + dataPos, data + dataPos + lace);
            dataPos += lace;
            if (lace < 255) {
                if (!partial.empty()) { packets.push_back({ std::move(partial) }); partial.clear(); }
            }
        }
        pos = pageDataOffset + pageDataSize;
    }
    return packets;
}

static bool DecodeOggOpusAndPush(const std::string& body, double volume, PCMQueue& queue)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(body.data());
    size_t len = body.size();
    if (len < 4 || std::memcmp(data, "OggS", 4) != 0) return false;

    std::vector<OggPacket> packets = ParseOggPackets(data, len);
    if (packets.empty()) return false;

    const int kDecodeRate = 16000;
    int opusErr = 0;
    OpusDecoder* dec = opus_decoder_create(kDecodeRate, 1, &opusErr);
    if (!dec || opusErr != OPUS_OK) {
        LogE("opus_decoder_create failed: " + std::string(opus_strerror(opusErr)));
        return false;
    }

    for (const auto& pkt : packets) {
        if (pkt.data.size() >= 16 && std::memcmp(pkt.data.data(), "OpusHead", 8) == 0) {
            uint32_t r = ReadLE32(pkt.data.data() + 12);
            LogI("OpusHead input_sample_rate: " + std::to_string(r) + " Hz");
            break;
        }
    }

    const int kMaxDecodeSamples = kDecodeRate * 120 / 1000;
    std::vector<int16_t> decodeBuf(kMaxDecodeSamples);
    bool decodedAny = false;
    const double vol = std::max(0.0, std::min(1.0, volume));

    for (const auto& pkt : packets) {
        if (pkt.data.size() >= 8 &&
            (std::memcmp(pkt.data.data(), "OpusHead", 8) == 0 ||
             std::memcmp(pkt.data.data(), "OpusTags", 8) == 0))
            continue;
        int nSamples = opus_decode(dec, pkt.data.data(),
            static_cast<opus_int32>(pkt.data.size()), decodeBuf.data(), kMaxDecodeSamples, 0);
        if (nSamples > 0) {
            std::vector<int16_t> chunk(decodeBuf.begin(), decodeBuf.begin() + nSamples);
            if (volume < 1.0)
                for (auto& s : chunk)
                    s = static_cast<int16_t>(s * vol);
            queue.Push(std::move(chunk));
            decodedAny = true;
        }
    }

    opus_decoder_destroy(dec);
    return decodedAny;
}

bool OpenAITTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& endpoint,
    const std::string& apiKey,
    const std::string& model,
    const std::string& voice,
    double speed,
    double volume,
    PCMQueue& queue)
{
    if (endpoint.empty()) {
        LogE("OpenAI endpoint not configured (set [OpenAI] endpoint in HoundTTS-credentials.ini)");
        queue.MarkDone();
        return false;
    }

    bool ssl = false;
    std::string host, path;
    int port = 443;
    if (!ParseEndpoint(endpoint, ssl, host, port, path)) {
        LogE("Failed to parse OpenAI endpoint: " + endpoint);
        queue.MarkDone();
        return false;
    }

    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0)  speed = 4.0;

    std::ostringstream body;
    body << "{"
         << "\"model\":\""  << JsonEscape(model.empty() ? "tts-1" : model) << "\""
         << ",\"input\":\"" << JsonEscape(text) << "\"";
    if (!voice.empty())
        body << ",\"voice\":\"" << JsonEscape(voice) << "\"";
    body << std::fixed << std::setprecision(2)
         << ",\"speed\":" << speed
         << ",\"response_format\":\"wav\""
         << "}";
    std::string bodyStr = body.str();

    LogI("POST " + std::string(ssl ? "https://" : "http://") + host + ":" +
         std::to_string(port) + path);

    httplib::Headers headers = {{"Content-Type", "application/json"}};
    if (!apiKey.empty())
        headers.emplace("Authorization", "Bearer " + apiKey);

    std::string responseBody;
    int responseStatus = 0;

    if (ssl) {
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(120);
        auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
        if (!res) { LogE("HTTPS request failed"); queue.MarkDone(); return false; }
        responseStatus = res->status;
        responseBody   = res->body;
    } else {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(120);
        auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
        if (!res) { LogE("HTTP request failed"); queue.MarkDone(); return false; }
        responseStatus = res->status;
        responseBody   = res->body;
    }

    if (responseStatus != 200) {
        LogE("HTTP " + std::to_string(responseStatus) + " body=[" +
             responseBody.substr(0, 512) + "]");
        queue.MarkDone();
        return false;
    }
    if (responseBody.empty()) {
        LogE("Empty response body from OpenAI endpoint");
        queue.MarkDone();
        return false;
    }

    LogI("Received " + std::to_string(responseBody.size()) + " bytes");

    bool ok = false;
    if (responseBody.size() >= 4 && std::memcmp(responseBody.data(), "OggS", 4) == 0)
        ok = DecodeOggOpusAndPush(responseBody, volume, queue);
    else if (responseBody.size() >= 4 && std::memcmp(responseBody.data(), "RIFF", 4) == 0)
        ok = DecodeWavAndPush(responseBody, volume, queue);
    else
        LogE("Unrecognised audio format in OpenAI response");

    queue.MarkDone();
    return ok;
}

} // namespace HoundTTS
