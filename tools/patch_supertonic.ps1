# Patch supertonic helper.cpp for onnxruntime 1.22.0 API compatibility
# Session constructor: const char* -> wchar_t* (removed in ORT >= 1.17 on Windows)
# Uses std::filesystem::path to convert UTF-8 path to wide string.

$f = 'C:\supertonic\cpp\helper.cpp'
$c = [System.IO.File]::ReadAllText($f)

# Patch 1: loadOnnx() — Session constructor char* -> wchar_t*
$old1 = 'return std::make_unique<Ort::Session>(env, onnx_path.c_str(), opts);'
$new1 = 'return std::make_unique<Ort::Session>(env, std::filesystem::path(onnx_path).wstring().c_str(), opts);'

if ($c.Contains($old1)) {
    $c = $c.Replace($old1, $new1)
    Write-Host "patch1 applied (loadOnnx: char* -> wchar_t*)"
} else {
    throw "patch1: loadOnnx Session constructor pattern not found in helper.cpp"
}

# Patch 2: Add #include <filesystem> if not already present
$fsInclude = '#include <filesystem>'
if (-not $c.Contains($fsInclude)) {
    # Anchor on an existing include near the top
    $anchor = '#include <fstream>'
    if (-not $c.Contains($anchor)) {
        $anchor = '#include <string>'
    }
    if ($c.Contains($anchor)) {
        $c = $c.Replace($anchor, "$anchor`n$fsInclude")
        Write-Host "patch2 applied (added <filesystem> include)"
    } else {
        throw "patch2: no suitable anchor include found to inject <filesystem>"
    }
} else {
    Write-Host "patch2 skipped (<filesystem> already present)"
}

[System.IO.File]::WriteAllText($f, $c)
Write-Host "helper.cpp patched OK"
