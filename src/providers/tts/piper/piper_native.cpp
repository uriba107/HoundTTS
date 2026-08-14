#include "piper_native.h"
#include "utils.h"

namespace HoundTTS {

static const char* kTag = "HoundTTS/PiperNative";
static void LogE(const std::string& msg) { Logger::Instance().Error(kTag, msg); }
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

PiperNative& PiperNative::Instance() {
    static PiperNative instance;
    return instance;
}

bool PiperNative::Load(const std::string& dllDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_) return available_;

    loaded_ = true;

    std::string dllPath = dllDir;
    if (!dllPath.empty() && dllPath.back() != '\\' && dllPath.back() != '/')
        dllPath += '\\';
    dllPath += "piper.dll";

    LogI("Loading piper.dll from: " + dllPath);

    // Temporarily set the process-wide DLL search directory so that
    // onnxruntime.dll's internal LoadLibrary calls (for execution-provider
    // DLLs like DirectML) can also find DLLs in the piper directory.
    // The narrower AddDllDirectory + LOAD_LIBRARY_SEARCH_* approach only
    // covers the initial LoadLibraryExW and its implicit imports; internal
    // LoadLibrary calls inside onnxruntime use the default search order
    // and fail with GLE 1114 (ERROR_DLL_INIT_FAILED) when a provider DLL
    // cannot be located.
    std::wstring wDllDir = Utils::Utf8ToWide(dllDir);
    wchar_t prevDllDir[MAX_PATH] = {};
    DWORD prevLen = GetDllDirectoryW(MAX_PATH, prevDllDir);
    SetDllDirectoryW(wDllDir.c_str());

    hDll_ = LoadLibraryW(Utils::Utf8ToWide(dllPath).c_str());
    DWORD loadErr = GetLastError();

    // Restore previous DLL directory (nullptr resets to default behaviour).
    SetDllDirectoryW(prevLen > 0 ? prevDllDir : nullptr);

    if (!hDll_) {
        LogE("LoadLibrary failed for piper.dll (GLE=" + std::to_string(loadErr) + ") — piper.exe subprocess fallback active");
        return false;
    }

    fn_create_   = (PFN_piper_create)                    GetProcAddress(hDll_, "piper_create");
    fn_free_     = (PFN_piper_free)                      GetProcAddress(hDll_, "piper_free");
    fn_defaults_ = (PFN_piper_default_synthesize_options)GetProcAddress(hDll_, "piper_default_synthesize_options");
    fn_start_    = (PFN_piper_synthesize_start)          GetProcAddress(hDll_, "piper_synthesize_start");
    fn_next_     = (PFN_piper_synthesize_next)           GetProcAddress(hDll_, "piper_synthesize_next");

    if (!fn_create_ || !fn_free_ || !fn_defaults_ || !fn_start_ || !fn_next_) {
        LogE("piper.dll loaded but missing one or more required exports — disabling");
        FreeLibrary(hDll_);
        hDll_ = nullptr;
        return false;
    }

    available_ = true;

    LogI("piper.dll loaded successfully");
    return true;
}

bool PiperNative::Available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}

piper_synthesizer* PiperNative::Create(const char* modelPath, const char* configPath,
                                        const char* espeakDataPath) {
    return fn_create_(modelPath, configPath, espeakDataPath);
}

void PiperNative::Free(piper_synthesizer* synth) {
    fn_free_(synth);
}

piper_synthesize_options PiperNative::DefaultOptions(piper_synthesizer* synth) {
    return fn_defaults_(synth);
}

int PiperNative::SynthesizeStart(piper_synthesizer* synth, const char* text,
                                   const piper_synthesize_options* options) {
    return fn_start_(synth, text, options);
}

int PiperNative::SynthesizeNext(piper_synthesizer* synth, piper_audio_chunk* chunk) {
    return fn_next_(synth, chunk);
}

} // namespace HoundTTS
