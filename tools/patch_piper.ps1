# Patch piper for onnxruntime 1.22.0 API compatibility + DLL-safe init
# 1) Session constructor: const char* -> wchar_t* (removed in ORT >= 1.17 on Windows)
# 2) GetOutputNames(): use GetOutputNameAllocated() loop (GetOutputNames never in released builds)
# 3) Add #include <filesystem> for std::filesystem::path (used by patch 1)
# 4) piper_impl.hpp: replace global Ort::Env with lazy-init accessor (GLE 1114 fix)
# 5) piper.cpp: use ort_env() accessor instead of bare global
# 6) Limit ONNX intra-op threads per session via HOUNDTTS_PIPER_THREADS env var (default 4)

$f = 'C:\piper1-gpl\libpiper\src\piper.cpp'
$c = [System.IO.File]::ReadAllText($f)

# Patch 1: Session constructor (uses ort_env() accessor from patch 5)
$old1lf   = "synth->session = std::make_unique<Ort::Session>(`n        Ort::Session(ort_env, model_path, synth->session_options));"
$old1crlf = "synth->session = std::make_unique<Ort::Session>(`r`n        Ort::Session(ort_env, model_path, synth->session_options));"
$new1     = "{ const char* _htenv = std::getenv(`"HOUNDTTS_PIPER_THREADS`"); int _ht = (_htenv && _htenv[0]) ? std::atoi(_htenv) : 4; if (_ht < 1) _ht = 4; synth->session_options.SetIntraOpNumThreads(_ht); }`n    std::wstring _mp = std::filesystem::path(model_path).wstring();`n    synth->session = std::make_unique<Ort::Session>(ort_env(), _mp.c_str(), synth->session_options);"

if ($c.Contains($old1lf)) {
    $c = $c.Replace($old1lf, $new1)
    Write-Host "patch1 applied (LF)"
} elseif ($c.Contains($old1crlf)) {
    $c = $c.Replace($old1crlf, $new1)
    Write-Host "patch1 applied (CRLF)"
} else {
    throw "patch1: Session constructor pattern not found in piper.cpp"
}

# Patch 2: GetOutputNames -> GetOutputNameAllocated loop
$old2 = 'synth->session->GetOutputNames()'
$new2 = '[&]() { Ort::AllocatorWithDefaultOptions _a; size_t _n = synth->session->GetOutputCount(); std::vector<std::string> _r; for(size_t _i=0;_i<_n;++_i) _r.push_back(synth->session->GetOutputNameAllocated(_i,_a).get()); return _r; }()'

if ($c.Contains($old2)) {
    $c = $c.Replace($old2, $new2)
    Write-Host "patch2 applied"
} else {
    throw "patch2: GetOutputNames pattern not found in piper.cpp"
}

# Patch 3: Add #include <filesystem> and <cstdlib> for std::filesystem::path (used by patch 1)
$fsInclude = '#include <filesystem>'
$cstdlibInclude = '#include <cstdlib>'
$needsFilesystem = -not $c.Contains($fsInclude)
$needsCstdlib = -not $c.Contains($cstdlibInclude)

if ($needsFilesystem -or $needsCstdlib) {
    if (-not $c.Contains('#include <limits>')) {
        throw "patch3: anchor '#include <limits>' not found in piper.cpp; cannot inject <filesystem>/<cstdlib> includes"
    }
    $newIncludes = '#include <limits>'
    if ($needsFilesystem) { $newIncludes += "`n#include <filesystem>" }
    if ($needsCstdlib) { $newIncludes += "`n#include <cstdlib>" }
    $c = $c.Replace('#include <limits>', $newIncludes)
    Write-Host "patch3 applied (added $(if ($needsFilesystem) { '<filesystem> ' })$(if ($needsCstdlib) { '<cstdlib>' })includes)"
} else {
    Write-Host "patch3 skipped (<filesystem> and <cstdlib> already present)"
}

# Patch 4 (NEW): replace ort_env bare reference with ort_env() call
# After patches 1-3, the only remaining bare ort_env in piper.cpp is gone
# (patch 1 already emits ort_env()), but guard against future occurrences.
# No-op if patch 1 already converted all uses.

[System.IO.File]::WriteAllText($f, $c)
Write-Host "piper.cpp patched OK"

# ---- Patch piper_impl.hpp ----
# Patch 5: Replace global Ort::Env with lazy-initialized function.
# The global constructor runs under DllMain loader lock, which is illegal
# for heavy init like ORT thread pools/providers -> GLE 1114.
$h = 'C:\piper1-gpl\libpiper\include\piper_impl.hpp'
$hc = [System.IO.File]::ReadAllText($h)

$old5 = 'Ort::Env ort_env{ORT_LOGGING_LEVEL_WARNING, "piper"};'
$new5 = @'
inline Ort::Env& ort_env() {
    static Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "piper"};
    return env;
}
'@

if ($hc.Contains($old5)) {
    $hc = $hc.Replace($old5, $new5)
    [System.IO.File]::WriteAllText($h, $hc)
    Write-Host "patch5 applied (piper_impl.hpp: lazy ort_env)"
} else {
    throw "patch5: global Ort::Env pattern not found in piper_impl.hpp"
}
