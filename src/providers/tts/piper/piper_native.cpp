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

    // Scope DLL search directory so onnxruntime.dll (dependency) resolves
    // without touching the process-wide SetDllDirectoryW state.
    std::wstring wDllDir = Utils::Utf8ToWide(dllDir);
    DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(wDllDir.c_str());
    if (!cookie) {
        LogE("AddDllDirectory failed (GLE=" + std::to_string(GetLastError()) + ")");
    }

    hDll_ = LoadLibraryExW(Utils::Utf8ToWide(dllPath).c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!hDll_) {
        LogE("LoadLibrary failed for piper.dll (GLE=" + std::to_string(GetLastError()) + ") — piper.exe subprocess fallback active");
        if (cookie) RemoveDllDirectory(cookie);
        return false;
    }

    fn_create_   = (PFN_piper_create)                    GetProcAddress(hDll_, "piper_create");
    fn_free_     = (PFN_piper_free)                      GetProcAddress(hDll_, "piper_free");
    fn_defaults_ = (PFN_piper_default_synthesize_options)GetProcAddress(hDll_, "piper_default_synthesize_options");
    fn_start_    = (PFN_piper_synthesize_start)          GetProcAddress(hDll_, "piper_synthesize_start");
    fn_next_     = (PFN_piper_synthesize_next)           GetProcAddress(hDll_, "piper_synthesize_next");

    if (cookie) RemoveDllDirectory(cookie);

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
