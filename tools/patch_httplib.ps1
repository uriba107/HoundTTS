# patch_httplib.ps1 — Apply WebSocket Host-header fix to cpp-httplib header.
# Called during Docker build (deps stage).
#
# Bug (upstream still present in v0.48.0): perform_websocket_handshake
# always sends Host: host:port, even for default ports (80/443).
# RFC 7230 §5.4 says SHOULD omit. Microsoft CDN may route differently
# with :443 suffix.
#
# Patch 1 (query string dropped by WebSocketClient constructor) was
# fixed upstream as of v0.48.0 and is no longer applied here.

$ErrorActionPreference = 'Stop'
$f = 'C:\deps\include\httplib.h'

Write-Host "Patching httplib at $f ..."

# --- Patch: Host header default port ---
$old = 'req_str += "Host: " + host + ":" + std::to_string(port) + "\r\n";'
$new = 'if (port == 80 || port == 443) { req_str += "Host: " + host + "\r\n"; } else { req_str += "Host: " + host + ":" + std::to_string(port) + "\r\n"; }'

$h = [System.IO.File]::ReadAllText($f)
if ($h.Contains($old)) {
    $h = $h.Replace($old, $new)
    [System.IO.File]::WriteAllText($f, $h)
    Write-Host "  [OK] Host header default-port fix applied"
} elseif ($h.Contains($new)) {
    Write-Host "  [OK] Host header default-port fix already applied (or upstream fixed)"
} else {
    Write-Error "FAILED: could not find target string. httplib version may have changed."
    exit 1
}

# --- Verify ---
$verify = [System.IO.File]::ReadAllText($f)
if (-not $verify.Contains($new)) {
    Write-Error "VERIFY FAILED: patch not present in output"
    exit 1
}

Write-Host "httplib patch verified OK"
