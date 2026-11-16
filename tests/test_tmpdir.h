#pragma once

// Scratch directory for tests that need real files (INI parsing, voice scan).
// Unique per process+instance so parallel/leftover runs never collide, and
// removed on scope exit.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace testing {

class TempDir {
public:
    explicit TempDir(const std::string& leaf) {
        // `leaf` is interpolated into a path that is later removed recursively:
        // a separator or ".." would operate outside the temp root. Callers pass
        // literals today, but the failure mode is destructive, so validate.
        const bool badLeaf =
            leaf.empty() || leaf == "." || leaf == ".." ||
            leaf.find_first_of("/\\") != std::string::npos ||
            leaf.find("..") != std::string::npos;
        if (badLeaf)
            throw std::runtime_error("TempDir: invalid leaf name: " + leaf);

        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("houndtts-test-" + std::to_string(::GetCurrentProcessId()) + "-" +
                 std::to_string(++counter) + "-" + leaf);
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        if (ec)
            throw std::runtime_error("TempDir: remove_all failed: " + ec.message());
        std::filesystem::create_directories(path_, ec);
        if (ec)
            throw std::runtime_error("TempDir: create_directories failed: " + ec.message());
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        if (ec) {
            // A destructor cannot throw; at least leave a trace so a leftover
            // scratch directory is diagnosable.
            std::fprintf(stderr, "TempDir: remove_all(%s) failed: %s\n",
                         path_.string().c_str(), ec.message().c_str());
        }
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
        if (n <= 0)
            throw std::runtime_error("TempDir: WideCharToMultiByte(size) failed");
        std::string out(static_cast<size_t>(n), '\0');
        if (::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                  &out[0], n, nullptr, nullptr) != n)
            throw std::runtime_error("TempDir: WideCharToMultiByte(convert) failed");
        return out;
    }

private:
    std::filesystem::path path_;
};

} // namespace testing
