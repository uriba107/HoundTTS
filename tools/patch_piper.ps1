# Patch piper.cpp for onnxruntime 1.21.0 API compatibility
# 1) Session constructor: const char* -> wchar_t* (removed in ORT >= 1.17 on Windows)
# 2) GetOutputNames(): use GetOutputNameAllocated() loop (GetOutputNames never in released builds)

$f = 'C:\piper1-gpl\libpiper\src\piper.cpp'
$c = [System.IO.File]::ReadAllText($f)

# Patch 1: Session constructor
$old1lf   = "synth->session = std::make_unique<Ort::Session>(`n        Ort::Session(ort_env, model_path, synth->session_options));"
$old1crlf = "synth->session = std::make_unique<Ort::Session>(`r`n        Ort::Session(ort_env, model_path, synth->session_options));"
$new1     = "std::wstring _mp = std::filesystem::path(model_path).wstring();`n    synth->session = std::make_unique<Ort::Session>(ort_env, _mp.c_str(), synth->session_options);"

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

# Patch 3: Add #include <filesystem> for std::filesystem::path (used by patch 1)
$fsInclude = '#include <filesystem>'
if (-not $c.Contains($fsInclude)) {
    $c = $c.Replace('#include <limits>', "#include <limits>`n#include <filesystem>")
    Write-Host "patch3 applied (added <filesystem> include)"
} else {
    Write-Host "patch3 skipped (<filesystem> already present)"
}

[System.IO.File]::WriteAllText($f, $c)
Write-Host "piper.cpp patched OK"
