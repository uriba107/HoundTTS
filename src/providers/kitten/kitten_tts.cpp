#include "kitten_tts.h"
#include "opus_encoder.h"
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

static const char* kTag = "HoundTTS/KittenTTS";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

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

// Parse the host, port, and path prefix from a URL like "http://host:port" or "http://host:port/prefix"
static bool ParseEndpoint(const std::string& endpoint,
                           bool& outSsl,
                           std::string& outHost,
                           int& outPort,
                           std::string& outPathPrefix)
{
    outSsl        = false;
    outHost       = "localhost";
    outPort       = 8020;
    outPathPrefix = "";

    std::string url = endpoint;
    // Strip scheme
    if (url.substr(0, 8) == "https://") {
        outSsl = true;
        url = url.substr(8);
    } else if (url.substr(0, 7) == "http://") {
        url = url.substr(7);
    }

    // Split host:port from path
    auto slashPos = url.find('/');
    std::string hostPort = (slashPos != std::string::npos) ? url.substr(0, slashPos) : url;
    if (slashPos != std::string::npos)
        outPathPrefix = url.substr(slashPos);

    auto colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        outHost = hostPort.substr(0, colonPos);
        try { outPort = std::stoi(hostPort.substr(colonPos + 1)); }
        catch (...) { outPort = outSsl ? 443 : 8020; }
    } else {
        outHost = hostPort;
        outPort = outSsl ? 443 : 8020;
    }

    return !outHost.empty();
}

// Linear resampler: convert mono int16 from srcRate to 16000 Hz.
// Uses linear interpolation — sufficient quality for voice.
static std::vector<int16_t> Resample(const std::vector<int16_t>& in,
                                      int srcRate, int dstRate = 16000)
{
    if (srcRate == dstRate) return in;
    if (in.empty()) return {};

    size_t outLen = static_cast<size_t>(
        static_cast<int64_t>(in.size()) * dstRate / srcRate);
    if (outLen == 0) return {};

    std::vector<int16_t> out(outLen);
    double ratio = static_cast<double>(srcRate) / dstRate;
    for (size_t i = 0; i < outLen; ++i) {
        double srcPos = i * ratio;
        size_t idx    = static_cast<size_t>(srcPos);
        double frac   = srcPos - idx;
        int16_t a = in[idx];
        int16_t b = (idx + 1 < in.size()) ? in[idx + 1] : a;
        out[i] = static_cast<int16_t>(a + frac * (b - a));
    }
    return out;
}

// Read a little-endian 32-bit uint from a byte buffer
static uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// Read a little-endian 16-bit uint from a byte buffer
static uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

// Attempt to decode a WAV body (16-bit PCM) and encode to Opus.
// Returns true if the body looked like a valid PCM WAV.
static bool DecodeWavAndEncode(const std::string& body, double volume, AudioQueue& queue) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(body.data());
    size_t len = body.size();

    // Minimal RIFF/WAV header: need at least 44 bytes, starts with "RIFF" and "WAVE"
    if (len < 44) return false;
    if (std::memcmp(data, "RIFF", 4) != 0) return false;
    if (std::memcmp(data + 8, "WAVE", 4) != 0) return false;

    // Walk chunks to find "fmt " and "data"
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
            audioFormat  = ReadLE16(data + pos);
            numChannels  = ReadLE16(data + pos + 2);
            sampleRate   = ReadLE32(data + pos + 4);
            bitsPerSample= ReadLE16(data + pos + 14);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            pcmStart = data + pos;
            pcmBytes = chunkSize;
        }
        pos += chunkSize;
        if (pos & 1) pos++; // word-align
    }

    // We only handle 16-bit PCM (format 1), mono or stereo
    if (audioFormat != 1 || bitsPerSample != 16 || !pcmStart || pcmBytes == 0) return false;
    if (sampleRate == 0) return false;

    size_t sampleBytes = (pcmBytes / (bitsPerSample / 8)) * (bitsPerSample / 8);
    if (sampleBytes == 0) return false;

    size_t totalSamples = sampleBytes / 2;
    std::vector<int16_t> raw(totalSamples);
    std::memcpy(raw.data(), pcmStart, sampleBytes);

    // Downmix stereo → mono if needed
    std::vector<int16_t> mono;
    if (numChannels == 2) {
        mono.resize(totalSamples / 2);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = static_cast<int16_t>((static_cast<int32_t>(raw[i * 2]) +
                                             static_cast<int32_t>(raw[i * 2 + 1])) / 2);
    } else {
        mono = std::move(raw);
    }

    // Resample to 16kHz if needed
    std::vector<int16_t> samples = Resample(mono, static_cast<int>(sampleRate));
    if (samples.empty()) return false;

    if (sampleRate != 16000)
        LogI("WAV resampled " + std::to_string(sampleRate) + " Hz → 16000 Hz (" +
             std::to_string(mono.size()) + " → " + std::to_string(samples.size()) + " samples)");

    if (volume < 1.0) {
        double vol = std::max(0.0, std::min(1.0, volume));
        for (auto& s : samples)
            s = static_cast<int16_t>(s * vol);
    }

    OpusFrameEncoder encoder;
    if (!encoder.Init()) {
        LogE("OpusFrameEncoder::Init() failed");
        return false;
    }
    encoder.EncodeChunk(samples.data(), static_cast<int>(samples.size()), queue);
    encoder.Flush(queue);
    return true;
}

// Extract all complete Ogg packets from a body into a flat list.
// Each element is one reassembled packet (may span multiple segments/pages).
struct OggPacket {
    std::vector<uint8_t> data;
};

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
        for (uint8_t i = 0; i < numSegs; ++i)
            pageDataSize += data[pos + 27 + i];

        if (pageDataOffset + pageDataSize > len) break;

        size_t dataPos = pageDataOffset;
        for (uint8_t i = 0; i < numSegs; ++i) {
            uint8_t lace = data[pos + 27 + i];
            partial.insert(partial.end(), data + dataPos, data + dataPos + lace);
            dataPos += lace;
            if (lace < 255) {
                if (!partial.empty()) {
                    packets.push_back({ std::move(partial) });
                    partial.clear();
                }
            }
        }

        pos = pageDataOffset + pageDataSize;
    }
    return packets;
}

// Decode an Ogg/Opus stream to 16kHz PCM and re-encode via OpusFrameEncoder.
// libopus decodes directly at the requested output rate (16kHz here), handling
// any source rate internally — no manual resampling needed.
static bool DecodeOggOpusAndEncode(const std::string& body,
                                    double volume, AudioQueue& queue)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(body.data());
    size_t len = body.size();

    if (len < 4 || std::memcmp(data, "OggS", 4) != 0) return false;

    std::vector<OggPacket> packets = ParseOggPackets(data, len);
    if (packets.empty()) return false;

    // Decode directly at 16000 Hz — libopus can efficiently downsample to any
    // supported rate (8k/12k/16k/24k/48k) as part of the decode step itself.
    // This avoids any manual resampling for the Ogg/Opus path entirely.
    const int kDecodeRate = 16000;
    int opusErr = 0;
    OpusDecoder* dec = opus_decoder_create(kDecodeRate, 1, &opusErr);
    if (!dec || opusErr != OPUS_OK) {
        LogE("opus_decoder_create failed: " + std::string(opus_strerror(opusErr)));
        return false;
    }

    // Log what OpusHead says (informational only)
    for (const auto& pkt : packets) {
        if (pkt.data.size() >= 16 &&
            std::memcmp(pkt.data.data(), "OpusHead", 8) == 0) {
            uint32_t r = ReadLE32(pkt.data.data() + 12);
            LogI("OpusHead input_sample_rate tag: " + std::to_string(r) +
                 " Hz (decoding at " + std::to_string(kDecodeRate) + " Hz)");
            break;
        }
    }

    // 120ms max frame at 16kHz = 1920 samples
    const int kMaxDecodeSamples = kDecodeRate * 120 / 1000;
    std::vector<int16_t> decodeBuf(kMaxDecodeSamples);

    OpusFrameEncoder encoder;
    if (!encoder.Init()) {
        LogE("OpusFrameEncoder::Init() failed");
        opus_decoder_destroy(dec);
        return false;
    }

    bool decodedAny = false;
    const double vol = std::max(0.0, std::min(1.0, volume));

    for (const auto& pkt : packets) {
        // Skip header packets
        if (pkt.data.size() >= 8 &&
            (std::memcmp(pkt.data.data(), "OpusHead", 8) == 0 ||
             std::memcmp(pkt.data.data(), "OpusTags", 8) == 0))
            continue;

        int nSamples = opus_decode(dec,
            pkt.data.data(),
            static_cast<opus_int32>(pkt.data.size()),
            decodeBuf.data(), kMaxDecodeSamples, 0);

        if (nSamples > 0) {
            if (volume < 1.0) {
                for (int i = 0; i < nSamples; ++i)
                    decodeBuf[i] = static_cast<int16_t>(decodeBuf[i] * vol);
            }
            encoder.EncodeChunk(decodeBuf.data(), nSamples, queue);
            decodedAny = true;
        }
    }

    opus_decoder_destroy(dec);

    if (decodedAny)
        encoder.Flush(queue);

    return decodedAny;
}

bool KittenTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& endpoint,
    const std::string& voice,
    const std::string& language,
    double speed,
    double volume,
    AudioQueue& queue)
{
    if (endpoint.empty()) {
        LogE("KittenTTS endpoint not configured (set [KittenTTS] endpoint in HoundTTS-credentials.ini)");
        queue.MarkDone();
        return false;
    }
    if (voice.empty()) {
        LogE("KittenTTS voice not set (pass via provider_params.voice, e.g. \"Bella\", \"Hugo\", etc.)");
        queue.MarkDone();
        return false;
    }

    bool   ssl        = false;
    std::string host, pathPrefix;
    int    port       = 8020;

    if (!ParseEndpoint(endpoint, ssl, host, port, pathPrefix)) {
        LogE("Failed to parse KittenTTS endpoint: " + endpoint);
        queue.MarkDone();
        return false;
    }

    // Build JSON body — voice and text are required; speed/language are optional overrides
    std::ostringstream body;
    body << "{"
         << "\"text\":\""   << JsonEscape(text)  << "\""
         << ",\"voice\":\"" << JsonEscape(voice) << "\"";
    if (!language.empty())
        body << ",\"language\":\"" << JsonEscape(language) << "\"";
    if (speed > 0.0) {
        if (speed < 0.1) speed = 0.1;
        if (speed > 4.0) speed = 4.0;
        // Force float notation so server receives 1.0 not 1 (Python may treat them differently)
        body << std::fixed << std::setprecision(2) << ",\"speed\":" << speed;
    }
    body << ",\"output_format\":\"opus\"";
    body << "}";

    std::string bodyStr = body.str();
    std::string path    = pathPrefix + "/tts";

    LogI("POST " + endpoint + "/tts body=" + bodyStr);

    httplib::Headers headers = {{"Content-Type", "application/json"}};

    std::string responseBody;
    int         responseStatus = 0;

    auto handleResponse = [&](const httplib::Response& res) {
        responseStatus = res.status;
        responseBody   = res.body;
    };

    if (ssl) {
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(60);
        auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
        if (!res) {
            LogE("HTTP request failed (connection error) to " + endpoint);
            queue.MarkDone();
            return false;
        }
        handleResponse(*res);
    } else {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(60);
        auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
        if (!res) {
            LogE("HTTP request failed (connection error) to " + endpoint);
            queue.MarkDone();
            return false;
        }
        handleResponse(*res);
    }

    if (responseStatus != 200) {
        LogE("HTTP " + std::to_string(responseStatus) + " from KittenTTS body=[" + responseBody + "]");
        queue.MarkDone();
        return false;
    }
    if (responseBody.empty()) {
        LogE("Empty response body from KittenTTS");
        queue.MarkDone();
        return false;
    }

    LogI("Received " + std::to_string(responseBody.size()) + " bytes from KittenTTS");

    // Try Ogg/Opus first (output_format=opus returns Ogg container).
    bool ok = false;
    if (responseBody.size() >= 4 &&
        std::memcmp(responseBody.data(), "OggS", 4) == 0) {
        ok = DecodeOggOpusAndEncode(responseBody, volume, queue);
        if (!ok)
            LogE("Failed to decode Ogg/Opus response");
    } else if (responseBody.size() >= 4 &&
               std::memcmp(responseBody.data(), "RIFF", 4) == 0) {
        // Server returned WAV instead — decode PCM and encode to Opus
        ok = DecodeWavAndEncode(responseBody, volume, queue);
        if (!ok)
            LogE("Failed to decode WAV response from KittenTTS");
    } else {
        LogE("Unrecognised audio format in KittenTTS response");
    }

    queue.MarkDone();
    return ok;
}

} // namespace HoundTTS
