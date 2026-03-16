#include "piper_tts.h"
#include "utils.h"

#include <windows.h>

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace HoundTTS {

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

int PiperTTS::ReadSampleRate(const std::string& modelPath) {
    std::string json = ReadModelJson(modelPath);
    return json.empty() ? 0 : ParseSampleRate(json);
}

std::string PiperTTS::ResolvePiperExe(const std::string& modelPath,
                                       const std::string& piperExe) {
    if (!piperExe.empty()) return piperExe;
    auto sep = modelPath.find_last_of("/\\");
    std::string dir = (sep != std::string::npos) ? modelPath.substr(0, sep + 1) : "";
    return dir + "piper.exe";
}

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

static const char* kTag = "HoundTTS/Piper";
static void LogE(const std::string& msg) { HoundTTS::Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { HoundTTS::Logger::Instance().Info(kTag, msg); }

bool PiperTTS::SynthesizeToQueue(
    const std::string& text,
    const std::string& modelPath,
    const std::string& piperExeHint,
    const std::string& speaker,
    double /*speed*/,
    double volume,
    PCMQueue& queue)
{
    LogI("model=" + modelPath + " speaker=" + (speaker.empty() ? "(default)" : speaker));
    LogI("text=" + text.substr(0, 80));

    std::string modelJson = ReadModelJson(modelPath);
    int sampleRate = modelJson.empty() ? 0 : ParseSampleRate(modelJson);
    if (sampleRate == 0) { LogE("could not read sample_rate, defaulting to 22050"); sampleRate = 22050; }

    std::string speakerArg;
    if (!speaker.empty() && !modelJson.empty()) {
        int numSpeakers = ParseNumSpeakers(modelJson);
        if (numSpeakers > 1) {
            bool isNumeric = speaker.find_first_not_of("0123456789") == std::string::npos;
            if (isNumeric) speakerArg = speaker;
            else {
                std::string resolved = LookupSpeakerId(modelJson, speaker);
                speakerArg = resolved.empty() ? speaker : resolved;
            }
            LogI("speaker arg: " + speakerArg);
        }
    }

    std::string exePath = ResolvePiperExe(modelPath, piperExeHint);
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

    if (!CreatePipe(&hStdinRd,  &hStdinWr,  &sa, 0)) { LogE("CreatePipe(stdin) failed"); queue.MarkDone(); return false; }
    if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0)) {
        LogE("CreatePipe(stdout) failed");
        CloseHandle(hStdinRd); CloseHandle(hStdinWr);
        queue.MarkDone(); return false;
    }
    SetHandleInformation(hStdinWr,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags   = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRd;
    si.hStdOutput = hStdoutWr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

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
        if (sampleRate != 16000) {
            chunk = Resample(src, sampleCount, sampleRate, 16000);
        } else {
            chunk.assign(src, src + sampleCount);
        }
        if (volume < 1.0) {
            double vol = std::max(0.0, std::min(1.0, volume));
            for (auto& s : chunk)
                s = static_cast<int16_t>(s * vol);
        }
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
