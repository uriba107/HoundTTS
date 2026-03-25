#pragma once

#ifndef HOUNDTTS_PIPER_NATIVE_H
#define HOUNDTTS_PIPER_NATIVE_H

#include "piper/piper.h"

#include <windows.h>
#include <string>
#include <mutex>

namespace HoundTTS {

// Lazy-loads piper.dll at runtime via LoadLibrary/GetProcAddress.
// Thread-safe singleton. HoundTTS.dll has no link-time dependency on piper.dll.
class PiperNative {
public:
    static PiperNative& Instance();

    // Attempt to load piper.dll from dllDir. No-op if already loaded.
    // Returns true if piper.dll is available and all function pointers resolved.
    bool Load(const std::string& dllDir);

    // Returns true if piper.dll is loaded and ready.
    bool Available() const;

    // Wrapped piper.h C API — only call if Available() is true.
    piper_synthesizer* Create(const char* modelPath, const char* configPath,
                              const char* espeakDataPath);
    void               Free(piper_synthesizer* synth);
    piper_synthesize_options DefaultOptions(piper_synthesizer* synth);
    int SynthesizeStart(piper_synthesizer* synth, const char* text,
                        const piper_synthesize_options* options);
    int SynthesizeNext(piper_synthesizer* synth, piper_audio_chunk* chunk);

private:
    PiperNative() = default;
    ~PiperNative() = default;
    PiperNative(const PiperNative&) = delete;
    PiperNative& operator=(const PiperNative&) = delete;

    mutable std::mutex mutex_;
    bool               loaded_   = false;
    bool               available_ = false;
    HMODULE            hDll_     = nullptr;

    typedef piper_synthesizer* (*PFN_piper_create)(const char*, const char*, const char*);
    typedef void               (*PFN_piper_free)(piper_synthesizer*);
    typedef piper_synthesize_options (*PFN_piper_default_synthesize_options)(piper_synthesizer*);
    typedef int (*PFN_piper_synthesize_start)(piper_synthesizer*, const char*,
                                              const piper_synthesize_options*);
    typedef int (*PFN_piper_synthesize_next)(piper_synthesizer*, piper_audio_chunk*);

    PFN_piper_create                    fn_create_   = nullptr;
    PFN_piper_free                      fn_free_     = nullptr;
    PFN_piper_default_synthesize_options fn_defaults_ = nullptr;
    PFN_piper_synthesize_start          fn_start_    = nullptr;
    PFN_piper_synthesize_next           fn_next_     = nullptr;
};

} // namespace HoundTTS

#endif // HOUNDTTS_PIPER_NATIVE_H
