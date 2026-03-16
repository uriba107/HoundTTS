#pragma once

#ifndef HOUNDTTS_UTILS_H
#define HOUNDTTS_UTILS_H

#include <string>
#include <fstream>
#include <mutex>
#include <deque>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <windows.h>

namespace HoundTTS {

// ---------------------------------------------------------------------------
// Async logger — never blocks callers on file I/O.
//
//  OutputDebugStringA  — called directly by caller (always, lock-free).
//  Queue push          — mutex held only for deque::push_back (microseconds).
//  File write          — handled by a dedicated background writer thread.
//
// Call Logger::Init(writedir) once at startup (from l_init).
// Call Error/Info/Debug from any thread — they return almost instantly.
// ---------------------------------------------------------------------------
enum class LogLevel { LEVEL_ERROR = 0, LEVEL_INFO = 1, LEVEL_DEBUG = 2 };

// Shared state between Logger and the writer thread.
// Prevent use-after-free on DLL unload via shared_ptr ownership.
struct LogQueue {
    std::mutex              mu;
    std::condition_variable cv;
    std::deque<std::string> entries;
    std::atomic<bool>       alive{true};
    std::string             logPath;   // set once in Init, read-only after
};

class Logger {
public:
    static Logger& Instance() {
        static Logger inst;
        return inst;
    }

    void Init(const std::string& writedir) {
        if (writedir.empty()) return;
        std::string path = writedir;
        if (path.back() != '\\' && path.back() != '/') path += '\\';
        path += "Logs\\HoundTTS.log";

        auto q = std::make_shared<LogQueue>();
        q->logPath = path;
        q->alive.store(true);
        queue_ = q;

        // Writer thread owns a shared_ptr — state survives even if
        // Logger is destroyed first (DLL unload order).
        std::thread([q]() {
            while (q->alive.load()) {
                std::deque<std::string> batch;
                {
                    std::unique_lock<std::mutex> lk(q->mu);
                    q->cv.wait_for(lk, std::chrono::milliseconds(100),
                        [&] { return !q->entries.empty() || !q->alive.load(); });
                    batch.swap(q->entries);
                }
                if (!batch.empty()) {
                    std::ofstream f(q->logPath, std::ios::app);
                    if (f) for (auto& line : batch) f << line;
                }
            }
            // Final flush after shutdown
            std::deque<std::string> tail;
            {
                std::lock_guard<std::mutex> lk(q->mu);
                tail.swap(q->entries);
            }
            if (!tail.empty()) {
                std::ofstream f(q->logPath, std::ios::app);
                if (f) for (auto& line : tail) f << line;
            }
        }).detach();
    }

    void SetLevel(LogLevel level) { level_.store(level); }

    void Error(const std::string& tag, const std::string& msg) {
        Emit(tag, "ERROR", msg);
    }
    void Info(const std::string& tag, const std::string& msg) {
        if (level_.load(std::memory_order_relaxed) < LogLevel::LEVEL_INFO) return;
        Emit(tag, "INFO", msg);
    }
    void Debug(const std::string& tag, const std::string& msg) {
        if (level_.load(std::memory_order_relaxed) < LogLevel::LEVEL_DEBUG) return;
        Emit(tag, "DEBUG", msg);
    }

    ~Logger() {
        auto q = queue_;
        if (q) {
            q->alive.store(false);
            q->cv.notify_one();
        }
        // Don't join — writer thread holds its own shared_ptr and will
        // flush + exit on its own.  Joining inside a static destructor
        // on Windows risks loader-lock deadlock.
    }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Emit(const std::string& tag, const std::string& level,
              const std::string& msg) {
        std::string line = "[" + tag + "] " + level + ": " + msg + "\n";
        // Lock-free — always visible in DebugView / attached debugger
        OutputDebugStringA(line.c_str());
        // Enqueue for file write (fast: only a deque push under lock)
        auto q = queue_;
        if (!q || !q->alive.load()) return;
        {
            std::lock_guard<std::mutex> lk(q->mu);
            q->entries.push_back(std::move(line));
        }
        q->cv.notify_one();
    }

    std::shared_ptr<LogQueue> queue_;
    std::atomic<LogLevel>     level_{LogLevel::LEVEL_ERROR};
};

namespace Utils {

// Convert UTF-8 std::string to wide string
inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                          static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                        static_cast<int>(str.size()), &wstr[0], size_needed);
    return wstr;
}

// Convert wide string to UTF-8 std::string
inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                          static_cast<int>(wstr.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                        static_cast<int>(wstr.size()),
                        &str[0], size_needed, nullptr, nullptr);
    return str;
}

// Read a string value from the Windows registry
inline std::wstring ReadRegistryString(HKEY hKeyRoot, const wchar_t* subKey,
                                       const wchar_t* valueName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hKeyRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return L"";
    }

    DWORD dataSize = 0;
    DWORD type = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &dataSize) != ERROR_SUCCESS
        || type != REG_SZ) {
        RegCloseKey(hKey);
        return L"";
    }

    std::wstring result(dataSize / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(hKey, valueName, nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&result[0]), &dataSize) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return L"";
    }
    RegCloseKey(hKey);

    // Remove trailing null if present
    while (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

// Ensure path ends with backslash
inline std::wstring EnsureTrailingBackslash(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.back() != L'\\' && path.back() != L'/') {
        return path + L"\\";
    }
    return path;
}

// Quote a wide string for command-line use
inline std::wstring QuoteArg(const std::wstring& arg) {
    return L"\"" + arg + L"\"";
}

} // namespace Utils
} // namespace HoundTTS

#endif // HOUNDTTS_UTILS_H
