# Patch piper v1.5.0 for HoundTTS integration
# 1) Inject HOUNDTTS_PIPER_THREADS → SetIntraOpNumThreads before Session creation
# 2) GetOutputNames → GetOutputNameAllocated loop (never shipped in released ORT)
# 3) Add #include <cstdlib> for getenv/atoi
#
# WIN32 is defined via /DWIN32 in CMake flags (Dockerfile) — upstream uses
# #if defined(WIN32) / #if !defined(WIN32) but MSVC x64 only defines _WIN32.
#
# piper_impl.hpp: no patches needed — upstream v1.5.0 moved Ort::Env to
# function-local static inside piper_create() (same lazy-init pattern).

$f = 'C:\piper1-gpl\libpiper\src\piper.cpp'
$c = [System.IO.File]::ReadAllText($f)

# ---- Patch 1: Inject thread limiting before Ort::Session constructor ----
# In v1.5.0 the Session constructor uses model_path_ort (wchar_t on Win32).
# We inject SetIntraOpNumThreads before creation.
$old1_lf   = "synth->session = std::make_unique<Ort::Session>(`n        Ort::Session(ort_env, model_path_ort, synth->session_options));"
$old1_crlf = "synth->session = std::make_unique<Ort::Session>(`r`n        Ort::Session(ort_env, model_path_ort, synth->session_options));"
$new1      = "{ const char* _htenv = std::getenv(`"HOUNDTTS_PIPER_THREADS`"); int _ht = (_htenv && _htenv[0]) ? std::atoi(_htenv) : 4; if (_ht < 1) _ht = 4; synth->session_options.SetIntraOpNumThreads(_ht); }`n    synth->session = std::make_unique<Ort::Session>(`n        Ort::Session(ort_env, model_path_ort, synth->session_options));"

if ($c.Contains($new1)) {
    Write-Host "patch1 skipped (already applied)"
} elseif ($c.Contains($old1_lf)) {
    $c = $c.Replace($old1_lf, $new1)
    Write-Host "patch1 applied (LF)"
} elseif ($c.Contains($old1_crlf)) {
    $c = $c.Replace($old1_crlf, $new1)
    Write-Host "patch1 applied (CRLF)"
} else {
    throw "patch1: Session constructor pattern (ort_env, model_path_ort) not found in piper.cpp"
}

# ---- Patch 2: GetOutputNames -> GetOutputNameAllocated loop ----
$old2 = 'synth->session->GetOutputNames()'
$new2 = '[&]() { Ort::AllocatorWithDefaultOptions _a; size_t _n = synth->session->GetOutputCount(); std::vector<std::string> _r; for(size_t _i=0;_i<_n;++_i) _r.push_back(synth->session->GetOutputNameAllocated(_i,_a).get()); return _r; }()'

if ($c.Contains($new2)) {
    Write-Host "patch2 skipped (already applied)"
} elseif ($c.Contains($old2)) {
    $c = $c.Replace($old2, $new2)
    Write-Host "patch2 applied (GetOutputNames → GetOutputNameAllocated)"
} else {
    throw "patch2: GetOutputNames pattern not found in piper.cpp"
}

# ---- Patch 3: Add #include <cstdlib> for std::getenv/std::atoi ----
$cstdlibInclude = '#include <cstdlib>'
if (-not $c.Contains($cstdlibInclude)) {
    if (-not $c.Contains('#include <limits>')) {
        throw "patch3: anchor '#include <limits>' not found in piper.cpp"
    }
    $c = $c.Replace('#include <limits>', "#include <limits>`n#include <cstdlib>")
    Write-Host "patch3 applied (added <cstdlib>)"
} else {
    Write-Host "patch3 skipped (<cstdlib> already present)"
}

[System.IO.File]::WriteAllText($f, $c)
Write-Host "piper.cpp patched OK"

# piper_impl.hpp: no patches needed in v1.5.0
Write-Host "piper_impl.hpp: no patches needed (upstream uses static local in piper_create)"
