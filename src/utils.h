#pragma once

#ifndef HOUNDTTS_UTILS_H
#define HOUNDTTS_UTILS_H

#include <string>
#include <fstream>
#include <mutex>
#include <windows.h>

namespace HoundTTS {

// ---------------------------------------------------------------------------
// Central logger — writes to <writedir>Logs\HoundTTS.log
// Call Logger::Init(writedir) once at startup (from l_init).
// Call Logger::Log(tag, msg) from any thread.
// ---------------------------------------------------------------------------
enum class LogLevel { LEVEL_ERROR = 0, LEVEL_INFO = 1 };

class Logger {
public:
    static Logger& Instance() {
        static Logger inst;
        return inst;
    }

    void Init(const std::string& writedir) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (writedir.empty()) return;
        std::string path = writedir;
        if (path.back() != '\\' && path.back() != '/') path += '\\';
        path += "Logs\\HoundTTS.log";
        logPath_ = path;
    }

    void SetLevel(LogLevel level) {
        std::lock_guard<std::mutex> lk(mutex_);
        level_ = level;
    }

    void Error(const std::string& tag, const std::string& msg) {
        Write_(tag, "ERROR", msg);
    }

    void Info(const std::string& tag, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (level_ < LogLevel::LEVEL_INFO) return;
        Write_Unlocked(tag, "INFO", msg);
    }

private:
    Logger() = default;

    void Write_(const std::string& tag, const std::string& level, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mutex_);
        Write_Unlocked(tag, level, msg);
    }

    void Write_Unlocked(const std::string& tag, const std::string& level, const std::string& msg) {
        std::string line = "[" + tag + "] " + level + ": " + msg + "\n";
        OutputDebugStringA(line.c_str());
        if (logPath_.empty()) return;
        std::ofstream f(logPath_, std::ios::app);
        if (f) f << line;
    }

    std::mutex  mutex_;
    std::string logPath_;
    LogLevel    level_ = LogLevel::LEVEL_ERROR;
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
