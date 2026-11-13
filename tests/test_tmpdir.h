#pragma once

// Scratch directory for tests that need real files (INI parsing, voice scan).
// Unique per process+instance so parallel/leftover runs never collide, and
// removed on scope exit.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <string>

namespace testing {

class TempDir {
public:
    explicit TempDir(const std::string& leaf) {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("houndtts-test-" + std::to_string(::GetCurrentProcessId()) + "-" +
                 std::to_string(++counter) + "-" + leaf);
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& Path() const { return path_; }

    // UTF-8 form for the narrow-string APIs (ConfigReader::Load takes UTF-8).
    std::string Utf8() const {
        const std::wstring w = path_.wstring();
        if (w.empty()) return std::string();
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                           static_cast<int>(w.size()),
                                           nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string out(static_cast<size_t>(n), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                              &out[0], n, nullptr, nullptr);
        return out;
    }

private:
    std::filesystem::path path_;
};

} // namespace testing
