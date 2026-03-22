#include "piper_tts.h"
#include "piper_native.h"
#include "piper_model_pool.h"
#include "utils.h"

#include <windows.h>

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <limits>
#include <charconv>

namespace HoundTTS {

static const char* kTag = "HoundTTS/Piper";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// JSON helpers (shared by both paths)
// ---------------------------------------------------------------------------

static int ParseJsonInt(const std::string& json, const char* key) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return 0;
    pos += strlen(key);
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size()) return 0;
    int val = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        val = val * 10 + (json[pos] - '0');
        ++pos;
    }
    return val;
}

static int ParseSampleRate(const std::string& json)  { return ParseJsonInt(json, "\"sample_rate\""); }
static int ParseNumSpeakers(const std::string& json) { return ParseJsonInt(json, "\"num_speakers\""); }

static std::string LookupSpeakerId(const std::string& json, const std::string& name) {
    std::string key = "\"" + name + "\"";
    auto mapPos = json.find("\"speaker_id_map\"");
    if (mapPos == std::string::npos) return "";
    auto pos = json.find(key, mapPos);
    if (pos == std::string::npos) return "";
    pos += key.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size()) return "";
    std::string id;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        id += json[pos++];
    return id;
}

static int SafeStoi(const std::string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    int val = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    return (ec == std::errc{} && ptr == s.data() + s.size()) ? val : fallback;
}

static int ResolveSpeakerId(const std::string& modelJson, const std::string& speaker) {
    if (speaker.empty() || modelJson.empty()) return 0;
    int numSpeakers = ParseNumSpeakers(modelJson);
    if (numSpeakers <= 1) return 0;
    bool isNumeric = speaker.find_first_not_of("0123456789") == std::string::npos;
    if (isNumeric) {
        int parsed = SafeStoi(speaker);
        if (parsed < numSpeakers) return parsed;
        // out-of-range numeric: try as name before falling back to 0
        std::string resolved = LookupSpeakerId(modelJson, speaker);
        return SafeStoi(resolved);
    }
    std::string resolved = LookupSpeakerId(modelJson, speaker);
    return SafeStoi(resolved);
}

static std::string ReadModelJson(const std::string& modelPath) {
    std::string jsonPath = modelPath + ".json";
    HANDLE hFile = CreateFileW(
        Utils::Utf8ToWide(jsonPath).c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};
    DWORD size = GetFileSize(hFile, nullptr);
    if (size == 0 || size > 1024 * 1024) { CloseHandle(hFile); return {}; }
    std::string buf(size, '\0');
    DWORD bytesRead = 0;
    ReadFile(hFile, &buf[0], size, &bytesRead, nullptr);
    CloseHandle(hFile);
    return buf;
}

// ---------------------------------------------------------------------------
// Shared PCM helpers
// ---------------------------------------------------------------------------

static std::vector<int16_t> Resample(const int16_t* in, int inCount,
                                      int inRate, int outRate) {
    if (inRate == outRate) return std::vector<int16_t>(in, in + inCount);
    int outCount = static_cast<int>(static_cast<int64_t>(inCount) * outRate / inRate);
    std::vector<int16_t> out(outCount);
    for (int i = 0; i < outCount; ++i) {
        double srcPos = static_cast<double>(i) * inRate / outRate;
        int lo = static_cast<int>(srcPos);
        double frac = srcPos - lo;
        int hi = std::min(lo + 1, inCount - 1);
        out[i] = static_cast<int16_t>(in[lo] * (1.0 - frac) + in[hi] * frac);
    }
    return out;
}

static void ApplyVolume(std::vector<int16_t>& chunk, double volume) {
    if (volume >= 1.0) return;
    double vol = std::max(0.0, std::min(1.0, volume));
    for (auto& s : chunk)
        s = static_cast<int16_t>(s * vol);
}

// ---------------------------------------------------------------------------
// PiperTTS static helpers
// ---------------------------------------------------------------------------

int PiperTTS::ReadSampleRate(const std::string& modelPath) {
    std::string json = ReadModelJson(modelPath);
    return json.empty() ? 0 : ParseSampleRate(json);
}

std::string PiperTTS::ResolvePiperExe(const std::string& piperPath) {
    std::string dir = piperPath;
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
        dir += '\\';
    return dir + "piper.exe";
}

// ---------------------------------------------------------------------------
// Public entry point: dispatch to native DLL or subprocess
// ---------------------------------------------------------------------------

bool PiperTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& modelPath,
    const std::string& piperPath,
    const std::string& speaker,
    double speed,
    double volume,
    PCMQueue& queue)
{
    LogI("model=" + modelPath + " speaker=" + (speaker.empty() ? "(default)" : speaker));
    LogI("text=" + text.substr(0, 80));

    // Attempt to load piper.dll (no-op if already loaded or previously failed)
    if (!piperPath.empty())
        PiperNative::Instance().Load(piperPath);

    if (PiperNative::Instance().Available()) {
        std::string espeakDataPath = piperPath;
        if (!espeakDataPath.empty() && espeakDataPath.back() != '\\' && espeakDataPath.back() != '/')
            espeakDataPath += '\\';
        espeakDataPath += "espeak-ng-data";
        return SynthesizeViaNative(text, modelPath, espeakDataPath, speaker, speed, volume, queue);
    }

    LogI("[DEPRECATED] piper.dll not available \u2014 falling back to piper.exe subprocess");
    return SynthesizeViaSubprocess(text, modelPath, piperPath, speaker, speed, volume, queue);
}

// ---------------------------------------------------------------------------
// Native DLL path
// ---------------------------------------------------------------------------

bool PiperTTS::SynthesizeViaNative(
    const std::string& text,
    const std::string& modelPath,
    const std::string& espeakDataPath,
    const std::string& speaker,
    double speed,
    double volume,
    PCMQueue& queue)
{
    auto& pool   = PiperModelPool::Instance();
    auto& native = PiperNative::Instance();

    piper_synthesizer* synth = pool.Acquire(modelPath, espeakDataPath);
    if (!synth) {
        LogE("Failed to acquire synthesizer for model: " + modelPath);
        queue.MarkDone();
        return false;
    }

    // Resolve speaker ID (numeric or named, with bounds check and name fallback)
    std::string modelJson = ReadModelJson(modelPath);
    int speakerId = ResolveSpeakerId(modelJson, speaker);

    // Build synthesis options
    piper_synthesize_options opts = native.DefaultOptions(synth);
    opts.speaker_id = speakerId;
    if (speed > 0.0 && speed != 1.0) {
        // piper length_scale is inverse of speed: 0.5 = 2x faster, 2.0 = 2x slower
        opts.length_scale = static_cast<float>(1.0 / speed);
    }

    // Phonemize (serialized — espeak-ng global state)
    int rc = pool.StartSynthesize(synth, text.c_str(), &opts);
    if (rc != PIPER_OK) {
        LogE("piper_synthesize_start failed: " + std::to_string(rc));
        pool.Release(modelPath, synth);
        queue.MarkDone();
        return false;
    }

    // Inference loop (fully parallel across concurrent requests)
    piper_audio_chunk chunk{};
    int totalSamples = 0;
    bool failed = false;
    while (true) {
        rc = native.SynthesizeNext(synth, &chunk);
        if (rc == PIPER_ERR_GENERIC) {
            LogE("piper_synthesize_next error");
            failed = true;
            break;
        }

        if (chunk.num_samples > 0 && chunk.samples != nullptr) {
            // Convert float → int16 (piper outputs float in ~[-1, 1])
            std::vector<int16_t> pcm(chunk.num_samples);
            for (size_t i = 0; i < chunk.num_samples; ++i) {
                float s = chunk.samples[i];
                if (s >  1.0f) s =  1.0f;
                if (s < -1.0f) s = -1.0f;
                pcm[i] = static_cast<int16_t>(s * static_cast<float>(
                    std::numeric_limits<int16_t>::max()));
            }

            // Resample to 16kHz if needed
            int sampleRate = chunk.sample_rate > 0 ? chunk.sample_rate : 22050;
            if (sampleRate != 16000)
                pcm = Resample(pcm.data(), static_cast<int>(pcm.size()), sampleRate, 16000);

            ApplyVolume(pcm, volume);
            totalSamples += static_cast<int>(pcm.size());
            queue.Push(std::move(pcm));
        }

        if (rc == PIPER_DONE || chunk.is_last) break;
    }

    if (failed)
        LogE("native synthesis failed after " + std::to_string(totalSamples) + " samples");
    else
        LogI("native synthesis complete, total output samples: " + std::to_string(totalSamples));
    pool.Release(modelPath, synth);
    queue.MarkDone();
    return !failed;
}

// ---------------------------------------------------------------------------
// Deprecated subprocess path
// ---------------------------------------------------------------------------

bool PiperTTS::SynthesizeViaSubprocess(
    const std::string& text,
    const std::string& modelPath,
    const std::string& piperPath,
    const std::string& speaker,
    double /*speed*/,
    double volume,
    PCMQueue& queue)
{
    std::string modelJson = ReadModelJson(modelPath);
    int sampleRate = modelJson.empty() ? 0 : ParseSampleRate(modelJson);
    if (sampleRate == 0) { LogE("could not read sample_rate, defaulting to 22050"); sampleRate = 22050; }

    int resolvedSpeakerId = ResolveSpeakerId(modelJson, speaker);
    std::string speakerArg;
    if (!speaker.empty() && ParseNumSpeakers(modelJson) > 1) {
        speakerArg = std::to_string(resolvedSpeakerId);
        LogI("speaker arg: " + speakerArg);
    }

    std::string exePath = ResolvePiperExe(piperPath);
    LogI("exe=" + exePath);

    std::wstring wExe   = Utils::Utf8ToWide(exePath);
    std::wstring wModel = Utils::Utf8ToWide(modelPath);
    std::wstring cmdLine = L"\"" + wExe + L"\" --model \"" + wModel + L"\" --output-raw";
    if (!speakerArg.empty())
        cmdLine += L" --speaker " + Utils::Utf8ToWide(speakerArg);

    HANDLE hStdinRd = nullptr, hStdinWr = nullptr;
    HANDLE hStdoutRd = nullptr, hStdoutWr = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hStdinRd, &hStdinWr, &sa, 0)) { LogE("CreatePipe(stdin) failed"); queue.MarkDone(); return false; }
    if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0)) {
        LogE("CreatePipe(stdout) failed");
        CloseHandle(hStdinRd); CloseHandle(hStdinWr);
        queue.MarkDone(); return false;
    }
    SetHandleInformation(hStdinWr,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = hStdinRd;
    si.hStdOutput = hStdoutWr;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hStdinRd);
    CloseHandle(hStdoutWr);

    if (!ok) {
        LogE("CreateProcessW failed GLE=" + std::to_string(GetLastError()));
        CloseHandle(hStdinWr); CloseHandle(hStdoutRd);
        queue.MarkDone(); return false;
    }
    LogI("piper.exe launched PID=" + std::to_string(pi.dwProcessId));

    DWORD written = 0;
    WriteFile(hStdinWr, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(hStdinWr);

    static const int kChunkBytes = 4096;
    std::vector<uint8_t> rawBuf(kChunkBytes);
    std::vector<uint8_t> leftover;
    DWORD totalPcmBytes = 0;

    for (;;) {
        DWORD bytesRead = 0;
        BOOL readOk = ReadFile(hStdoutRd, rawBuf.data(), kChunkBytes, &bytesRead, nullptr);
        if (!readOk || bytesRead == 0) break;
        totalPcmBytes += bytesRead;
        leftover.insert(leftover.end(), rawBuf.begin(), rawBuf.begin() + bytesRead);
        size_t sampleBytes = (leftover.size() / 2) * 2;
        if (sampleBytes == 0) continue;
        const int16_t* src = reinterpret_cast<const int16_t*>(leftover.data());
        int sampleCount = static_cast<int>(sampleBytes / 2);
        std::vector<int16_t> chunk;
        if (sampleRate != 16000)
            chunk = Resample(src, sampleCount, sampleRate, 16000);
        else
            chunk.assign(src, src + sampleCount);
        ApplyVolume(chunk, volume);
        queue.Push(std::move(chunk));
        size_t remainder = leftover.size() - sampleBytes;
        if (remainder > 0) leftover = {leftover.end() - (int)remainder, leftover.end()};
        else leftover.clear();
    }

    LogI("total PCM bytes: " + std::to_string(totalPcmBytes));
    queue.MarkDone();
    CloseHandle(hStdoutRd);
    DWORD exitCode = 0;
    WaitForSingleObject(pi.hProcess, 5000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    if (exitCode != 0) LogE("piper.exe exit code: " + std::to_string(exitCode));
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}

} // namespace HoundTTS
