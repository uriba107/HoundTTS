#pragma once

#ifndef HOUNDTTS_PROCESS_LAUNCHER_H
#define HOUNDTTS_PROCESS_LAUNCHER_H

#include <string>

namespace HoundTTS {

// Launch a process asynchronously (fire-and-forget).
// Does not steal focus, does not create a visible window.
// Returns true if the process was successfully started.
//
// exePath is wrapped in quotes by the callee; `args` is appended verbatim as a
// raw command-line tail and is NOT quoted or escaped. Callers must pass a
// trusted absolute exePath and a pre-quoted tail built only from trusted values
// (CommandLineToArgvW rules) — never raw Lua/mission input.
// Process and thread handles are closed before returning; the child is not
// tracked or waited on. `workingDir` empty means inherit the DLL's cwd.
// ponytail: legacy piper.exe fallback, no call sites today.
bool LaunchProcessAsync(const std::wstring& exePath, const std::wstring& args,
                        const std::wstring& workingDir = L"");

} // namespace HoundTTS

#endif // HOUNDTTS_PROCESS_LAUNCHER_H
