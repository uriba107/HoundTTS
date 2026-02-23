#pragma once

#ifndef HOUNDTTS_PROCESS_LAUNCHER_H
#define HOUNDTTS_PROCESS_LAUNCHER_H

#include <string>

namespace HoundTTS {

// Launch a process asynchronously (fire-and-forget).
// Does not steal focus, does not create a visible window.
// Returns true if the process was successfully started.
bool LaunchProcessAsync(const std::wstring& exePath, const std::wstring& args,
                        const std::wstring& workingDir = L"");

} // namespace HoundTTS

#endif // HOUNDTTS_PROCESS_LAUNCHER_H
