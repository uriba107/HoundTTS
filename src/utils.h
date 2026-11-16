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
#include <vector>
#include <cstdint>
#include <chrono>
#include <utility>
#include <new>
#include <cmath>
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

// Fire-and-forget thread spawn.
//
// `std::thread(lambda).detach()` as one expression is not exception-safe: if
// the constructor succeeds but detach() throws, the temporary is still
// joinable when its destructor runs and std::terminate follows. This keeps the
// project's detached-thread convention while making that failure path safe.
//
// NOTE: the callable is moved into the thread and outlives this function, so
// callers must capture by value (shared_ptr, copies). Capturing a local by
// reference is a use-after-free.
//
// The callable also runs with no exception boundary of its own: an escaping
// exception reaches the thread entry point and calls std::terminate, which in
// a DLL means taking DCS down with it. Swallow (and surface) it here instead.
template <typename F>
inline void SpawnDetached(F&& f) {
    std::thread t([fn = std::forward<F>(f)]() mutable {
        try {
            fn();
        } catch (...) {
            // No Logger yet at this point in the header, and allocating in a
            // catch handler can throw again: emit a fixed, lock-free line.
            OutputDebugStringA("[HoundTTS] uncaught exception on detached thread\n");
        }
    });
    try {
        t.detach();
    } catch (...) {
        // detach() failed: never leave a joinable thread behind. join() itself
        // can throw, and an escaping exception here would hit the joinable
        // thread's destructor -> std::terminate.
        try {
            t.join();
        } catch (...) {
            // join() failed too: try detach once more.
            try { t.detach(); } catch (...) {}
        }
    }
    if (t.joinable()) {
        // Last resort: transfer the still-joinable thread to a heap object we
        // deliberately leak, so the local destructor cannot call
        // std::terminate and take the host process down. nothrow-new so the
        // transfer itself cannot throw; if even that allocation fails there is
        // no non-terminating option left.
        // ponytail: leaked by design, unreachable short of a failing OS thread
        // syscall.
        (void)new (std::nothrow) std::thread(std::move(t));
    }
}

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
        std::lock_guard<std::mutex> initLock(initMu_);

        std::string path = writedir;
        if (path.back() != '\\' && path.back() != '/') path += '\\';
        path += "Logs\\HoundTTS.log";

        // Idempotent: l_init() may be called again (mission reload). A second
        // queue would leak a writer thread and interleave writes to the file.
        if (auto existing = std::atomic_load(&queue_)) {
            if (existing->logPath != path)
                Emit("HoundTTS/init", "WARN",
                     "Logger already initialized for a different writedir");
            return;
        }

        auto q = std::make_shared<LogQueue>();
        q->logPath = path;
        q->alive.store(true);

        // Spawn the writer thread FIRST and publish queue_ only once that
        // succeeds. Publishing before the spawn would leave every later Emit
        // enqueuing into a LogQueue with no reader if std::thread throws.
        // Writer thread owns a shared_ptr — state survives even if
        // Logger is destroyed first (DLL unload order).
        try {
            SpawnDetached([q]() {
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
            });
        } catch (...) {
            // No writer thread: leave queue_ unpublished so a later Init() can
            // retry instead of silently black-holing every log line.
            OutputDebugStringA("[HoundTTS] Logger::Init: writer thread creation failed\n");
            return;
        }
        std::atomic_store(&queue_, q);
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
        auto q = std::atomic_load(&queue_);
        if (q) {
            // Set alive under the queue lock: Emit() publishes under the same
            // lock, so this closes the window where a concurrent emit checks
            // alive==true and enqueues after teardown began.
            { std::lock_guard<std::mutex> lk(q->mu); q->alive.store(false); }
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
        // Never throws. Logging is called from catch handlers, detached
        // threads and the Lua C boundary, where an escaping exception is
        // std::terminate or UB — and would skip whatever cleanup follows. The
        // message building, the queue lock and the enqueue may all allocate,
        // so the whole body is one guard: callers must never need their own.
        try {
            std::string line = "[" + tag + "] " + level + ": " + msg + "\n";
            // Lock-free — always visible in DebugView / attached debugger
            OutputDebugStringA(line.c_str());
            // Enqueue for file write (fast: only a deque push under lock)
            auto q = std::atomic_load(&queue_);
            if (!q) return;
            {
                std::lock_guard<std::mutex> lk(q->mu);
                // Re-check under the lock: ~Logger() clears alive under this mutex.
                if (!q->alive.load()) return;
                q->entries.push_back(std::move(line));
            }
            q->cv.notify_one();
        } catch (...) {
            // Best effort: a log line may never fail the caller.
        }
    }

    std::shared_ptr<LogQueue> queue_;   // published via std::atomic_store (Init)
    std::mutex                initMu_;  // serializes Init
    std::atomic<LogLevel>     level_{LogLevel::LEVEL_ERROR};
};

namespace Utils {

// Generate a 22-character base57 UUID string compatible with SRS GUIDs.
// Uses CoCreateGuid for the random source.
std::string GenerateSRSGuid();

// Escape a UTF-8 string for embedding inside a JSON string literal.
// Handles ", \, \b, \f, \n, \r, \t and every other C0 control character
// (emitted as \u00XX). Bytes >= 0x20 are copied verbatim (UTF-8 passthrough).
std::string JsonEscape(const std::string& s);

// Escape XML special characters and drop characters XML 1.0 cannot represent,
// for interpolating untrusted text into SSML. Shared by every TTS provider that
// builds an SSML document so the sanitisation policy cannot drift.
std::string XmlEscape(const std::string& s);

// Clamp a caller-supplied volume multiplier into [0,1]; non-finite -> 1.0.
// TTSPipeline::Produce normalises once, and providers call it again at their
// own entry point because they are also reachable directly.
inline double NormalizeVolume(double v) {
    if (!std::isfinite(v)) return 1.0;
    if (v <= 0.0) return 0.0;
    return v > 1.0 ? 1.0 : v;
}

// Decode the body of a JSON string literal, starting just after the opening
// quote at `pos`. Handles all short escapes plus \uXXXX, combining UTF-16
// surrogate pairs into a single code point. Lone surrogates become '?'.
// If `endPos` is non-null it receives the index just PAST the closing quote
// (i.e. where parsing should continue), or s.size() when unterminated.
std::string JsonDecodeString(const std::string& s, size_t pos,
                             size_t* endPos = nullptr);

// True when `s` contains a C0 control character or DEL. Used to reject values
// that are interpolated into HTTP headers (CR/LF injection).
bool HasControlChars(const std::string& s);

// True when `s` is a plausible DNS label: lowercase letters, digits and '-',
// at most `maxLen` chars, with no leading/trailing or consecutive hyphens.
// Used for region/endpoint fragments from config.
bool IsSafeHostLabel(const std::string& s, size_t maxLen = 32);

// True when `code` is a plausible ISO 639-1/2 language code: 2-3 ASCII letters.
// Used to reject arbitrary Lua strings before they reach a request body/URL.
bool IsAlphaLanguageCode(const std::string& code);

// Copy little-endian int16 PCM bytes into a sample vector. memcpy avoids the
// alignment/strict-aliasing UB of casting a byte buffer to const int16_t*.
std::vector<int16_t> PcmBytesToSamples(const void* data, size_t byteCount);

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
