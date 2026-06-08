# patch_httplib.ps1 — Apply WebSocket fixes to cpp-httplib header.
# Called during Docker build (deps stage).
#
# Bug 1: WebSocketClient constructor drops uc.query from path_.
#         Edge TTS auth tokens (TrustedClientToken, Sec-MS-GEC, etc.)
#         are silently lost → server accepts WS upgrade but sends no audio.
#
# Bug 2: perform_websocket_handshake always sends Host: host:port,
#         even for default ports (80/443). RFC 7230 §5.4 says SHOULD omit.
#         Microsoft CDN may route differently with :443 suffix.

$ErrorActionPreference = 'Stop'
$f = 'C:\deps\include\httplib.h'

Write-Host "Patching httplib at $f ..."
$h = [System.IO.File]::ReadAllText($f)

# --- Patch 1: query string ---
$old1 = 'path_ = std::move(uc.path);'
$new1 = 'path_ = std::move(uc.path); if (!uc.query.empty()) { path_ += uc.query; }'

if ($h.Contains($old1)) {
    $h = $h.Replace($old1, $new1)
    Write-Host "  [OK] Patch 1: query string fix applied"
} elseif ($h.Contains('path_ += uc.query')) {
    Write-Host "  [OK] Patch 1: already applied (or upstream fixed)"
} else {
    Write-Error "Patch 1 FAILED: could not find target string. httplib version may have changed."
    exit 1
}

# --- Patch 2: Host header default port ---
$old2 = 'req_str += "Host: " + host + ":" + std::to_string(port) + "\r\n";'
$new2 = 'if (port == 80 || port == 443) { req_str += "Host: " + host + "\r\n"; } else { req_str += "Host: " + host + ":" + std::to_string(port) + "\r\n"; }'

if ($h.Contains($old2)) {
    $h = $h.Replace($old2, $new2)
    Write-Host "  [OK] Patch 2: Host header default-port fix applied"
} elseif ($h.Contains('port == 80 || port == 443) { req_str')) {
    Write-Host "  [OK] Patch 2: already applied (or upstream fixed)"
} else {
    Write-Error "Patch 2 FAILED: could not find target string. httplib version may have changed."
    exit 1
}

[System.IO.File]::WriteAllText($f, $h)

# --- Verify ---
$verify = [System.IO.File]::ReadAllText($f)
if (-not $verify.Contains('path_ += uc.query')) {
    Write-Error "VERIFY FAILED: Patch 1 not present in output"
    exit 1
}
if (-not $verify.Contains('port == 80 || port == 443) { req_str')) {
    Write-Error "VERIFY FAILED: Patch 2 not present in output"
    exit 1
}

Write-Host "httplib patches verified OK"
