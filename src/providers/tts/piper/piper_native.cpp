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

    std::string dllPath = dllDir;
    if (!dllPath.empty() && dllPath.back() != '\\' && dllPath.back() != '/')
        dllPath += '\\';
    dllPath += "piper.dll";

    // Narrow->wide conversions can allocate, so do them BEFORE any process-wide
    // state changes or module loads below.
    const std::wstring wDllPath = Utils::Utf8ToWide(dllPath);
    const std::wstring wDllDir  = Utils::Utf8ToWide(dllDir);
    LogI("Loading piper.dll from: " + dllPath);

    // Clear loaded_ if this attempt exits via an exception so a later call can
    // retry; keep it set on permanent outcomes (success / permanent failure).
    //
    // On a mid-load exception the module is deliberately left loaded (a
    // concurrent caller may already hold a snapshotted export pointer, and
    // FreeLibrary would invalidate it), but every export is dropped and the
    // failure is made permanent: retrying cannot leak a second module handle.
    struct LoadGuard {
        bool& loaded;
        bool& available;
        PFN_piper_create& fnCreate;
        PFN_piper_free& fnFree;
        PFN_piper_default_synthesize_options& fnDefaults;
        PFN_piper_synthesize_start& fnStart;
        PFN_piper_synthesize_next& fnNext;
        bool keep = false;
        // LoadLibrary failed: nothing was loaded, so a later corrected path may
        // retry. Any other failure (module loaded, exports half-resolved) is
        // made permanent to avoid leaking/duplicating the module handle.
        bool retryable = false;
        ~LoadGuard() {
            if (keep) return;
            fnCreate   = nullptr;
            fnFree     = nullptr;
            fnDefaults = nullptr;
            fnStart    = nullptr;
            fnNext     = nullptr;
            available  = false;
            loaded     = !retryable;
        }
    } loadGuard{loaded_, available_,
                 fn_create_, fn_free_, fn_defaults_, fn_start_, fn_next_};

    loaded_ = true;

    // Temporarily set the process-wide DLL search directory so that
    // onnxruntime.dll's internal LoadLibrary calls (for execution-provider
    // DLLs like DirectML) can also find DLLs in the piper directory.
    // The narrower AddDllDirectory + LOAD_LIBRARY_SEARCH_* approach only
    // covers the initial LoadLibraryExW and its implicit imports; internal
    // LoadLibrary calls inside onnxruntime use the default search order
    // and fail with GLE 1114 (ERROR_DLL_INIT_FAILED) when a provider DLL
    // cannot be located.
    std::wstring prevDllDir;
    {
        wchar_t prevBuf[MAX_PATH] = {};
        if (GetDllDirectoryW(MAX_PATH, prevBuf) > 0) prevDllDir = prevBuf;
    }
    // Restore the process-wide directory on every exit path, including a throw.
    struct DllDirGuard {
        const std::wstring& prev;
        ~DllDirGuard() { SetDllDirectoryW(prev.empty() ? nullptr : prev.c_str()); }
    } dllDirGuard{prevDllDir};
    SetDllDirectoryW(wDllDir.c_str());

    hDll_ = LoadLibraryW(wDllPath.c_str());
    DWORD loadErr = GetLastError();

    if (!hDll_) {
        LogE("LoadLibrary failed for piper.dll (GLE=" + std::to_string(loadErr) + ") — piper.exe subprocess fallback active");
        loadGuard.retryable = true;   // nothing loaded: a corrected path may retry
        return false;
    }

    fn_create_   = (PFN_piper_create)                    GetProcAddress(hDll_, "piper_create");
    fn_free_     = (PFN_piper_free)                      GetProcAddress(hDll_, "piper_free");
    fn_defaults_ = (PFN_piper_default_synthesize_options)GetProcAddress(hDll_, "piper_default_synthesize_options");
    fn_start_    = (PFN_piper_synthesize_start)          GetProcAddress(hDll_, "piper_synthesize_start");
    fn_next_     = (PFN_piper_synthesize_next)           GetProcAddress(hDll_, "piper_synthesize_next");

    if (!fn_create_ || !fn_free_ || !fn_defaults_ || !fn_start_ || !fn_next_) {
        LogE("piper.dll loaded but missing one or more required exports — disabling");
        // Drop every pointer: FreeLibrary invalidates them, and callers (e.g.
        // PiperModelPool::Release) call Free() without checking Available().
        fn_create_   = nullptr;
        fn_free_     = nullptr;
        fn_defaults_ = nullptr;
        fn_start_    = nullptr;
        fn_next_     = nullptr;
        // Keep the module loaded. FreeLibrary here would invalidate a function
        // pointer a concurrent caller may already have snapshotted, so the
        // failure is permanent (a retry cannot fix missing exports anyway).
        available_ = false;
        loadGuard.keep = true;
        return false;
    }

    available_ = true;
    loadGuard.keep = true;

    LogI("piper.dll loaded successfully");
    return true;
}

bool PiperNative::Available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}

// Safety invariant: Load() holds mutex_ for its entire duration and there is no
// unload path, so snapshotting a function pointer under the lock and invoking it
// after unlocking cannot race a FreeLibrary that would invalidate the pointer.
// Any future unload must take mutex_ and wait for in-flight callers.
piper_synthesizer* PiperNative::Create(const char* modelPath, const char* configPath,
                                        const char* espeakDataPath) {
    PFN_piper_create fn = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn = fn_create_;
    }
    return fn ? fn(modelPath, configPath, espeakDataPath) : nullptr;
}

void PiperNative::Free(piper_synthesizer* synth) {
    PFN_piper_free fn = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn = fn_free_;
    }
    if (fn && synth) fn(synth);
}

piper_synthesize_options PiperNative::DefaultOptions(piper_synthesizer* synth) {
    PFN_piper_default_synthesize_options fn = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn = fn_defaults_;
    }
    if (!fn || !synth) return piper_synthesize_options{};
    return fn(synth);
}

int PiperNative::SynthesizeStart(piper_synthesizer* synth, const char* text,
                                   const piper_synthesize_options* options) {
    PFN_piper_synthesize_start fn = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn = fn_start_;
    }
    if (!fn || !synth) return PIPER_ERR_GENERIC;
    return fn(synth, text, options);
}

int PiperNative::SynthesizeNext(piper_synthesizer* synth, piper_audio_chunk* chunk) {
    PFN_piper_synthesize_next fn = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn = fn_next_;
    }
    if (!fn || !synth || !chunk) return PIPER_ERR_GENERIC;
    return fn(synth, chunk);
}

} // namespace HoundTTS
