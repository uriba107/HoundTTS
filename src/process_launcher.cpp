#include "process_launcher.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace HoundTTS {

bool LaunchProcessAsync(const std::wstring& exePath, const std::wstring& args,
                        const std::wstring& workingDir) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    // Build full command line: "exePath" args
    std::wstring cmdLine = L"\"" + exePath + L"\" " + args;

    // CreateProcessW needs a mutable command line buffer
    std::wstring cmdBuf(cmdLine);

    const wchar_t* cwd = workingDir.empty() ? nullptr : workingDir.c_str();

    BOOL ok = CreateProcessW(
        nullptr,                              // lpApplicationName (use cmdLine)
        &cmdBuf[0],                           // lpCommandLine (mutable)
        nullptr,                              // lpProcessAttributes
        nullptr,                              // lpThreadAttributes
        FALSE,                                // bInheritHandles
        CREATE_NO_WINDOW | DETACHED_PROCESS,  // dwCreationFlags
        nullptr,                              // lpEnvironment
        cwd,                                  // lpCurrentDirectory
        &si,                                  // lpStartupInfo
        &pi                                   // lpProcessInformation
    );

    if (ok) {
        // Fire-and-forget: close handles immediately
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }

    return false;
}

} // namespace HoundTTS
