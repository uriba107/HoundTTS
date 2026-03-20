#Requires -Version 5.1
<#
.SYNOPSIS
    Build HoundTTS using Docker.

.DESCRIPTION
    Builds the HoundTTS DLL via Docker and extracts the dist packages.
    Defaults to the Linux/MinGW cross-compile pipeline (Dockerfile).
    Use -Windows to build with the MSVC/Windows container pipeline (Dockerfile.windows).

.PARAMETER Windows
    Use Dockerfile.windows (MSVC + Windows SDK). Requires Docker Desktop in Windows containers mode.

.PARAMETER Context
    Docker context to use. Defaults to 'default'.

.PARAMETER NoCache
    Pass --no-cache to docker build (forces full rebuild, bypasses all cached layers).

.EXAMPLE
    .\build-docker.ps1
    .\build-docker.ps1 -Windows
    .\build-docker.ps1 -Windows -Context winhost
    .\build-docker.ps1 -Windows -NoCache
#>
param(
    [switch]$Windows = $true,
    [string]$Context = "default",
    [switch]$NoCache
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$OutputDir  = Join-Path $ScriptDir "dist"
$LogFile    = Join-Path $ScriptDir "build.log"

# Tee all output to build.log
Start-Transcript -Path $LogFile -Force | Out-Null
$Dockerfile = if ($Windows) { "Dockerfile.windows" } else { "Dockerfile" }
$ImageName  = if ($Windows) { "houndtts-builder-msvc" } else { "houndtts-builder" }

Write-Host "Log: $LogFile"
Write-Host ""
Write-Host "=== HoundTTS Docker Build ===" -ForegroundColor Cyan
Write-Host "  Dockerfile : $Dockerfile"
Write-Host "  Context    : $Context"
Write-Host ""

# Load deps.env into a hashtable
$depsEnvPath = Join-Path $ScriptDir "deps.env"
$deps = @{}
if (Test-Path $depsEnvPath) {
    Get-Content $depsEnvPath | Where-Object { $_ -match '^\s*[^#]' -and $_ -match '=' } | ForEach-Object {
        $parts = $_ -split '=', 2
        $deps[$parts[0].Trim()] = $parts[1].Trim()
    }
}

# Dependency version check (Windows builds only)
if ($Windows) {
    Write-Host "Checking dependency pins..." -ForegroundColor Cyan
    $anyOutdated = $false

    function Check-DepPin {
        param([string]$Name, [string]$Pinned, [string]$ApiUrl, [scriptblock]$Parse)
        try {
            $latest = & $Parse (Invoke-RestMethod $ApiUrl -UseBasicParsing)
            if ($latest -eq $Pinned) {
                Write-Host ("  " + $Name.PadRight(10) + " " + $Pinned.PadRight(18) + " (up to date)") -ForegroundColor Green
            } else {
                Write-Host ("  " + $Name.PadRight(10) + " " + $Pinned.PadRight(18) + " -> " + $latest + " available") -ForegroundColor Yellow
                $script:anyOutdated = $true
            }
        } catch {
            Write-Host ("  " + $Name.PadRight(10) + " " + $Pinned.PadRight(18) + " (could not fetch latest)") -ForegroundColor Gray
        }
    }

    Check-DepPin "cmake"   $deps['CMAKE_VERSION']         "https://api.github.com/repos/Kitware/CMake/releases/latest"          { param($r) $r.tag_name -replace '^v','' }
    Check-DepPin "git"     $deps['GIT_VERSION']           "https://api.github.com/repos/git-for-windows/git/releases/latest"    { param($r) $r.tag_name -replace '^v','' -replace '\.windows\.\d+$','' }
    Check-DepPin "vcpkg"   $deps['VCPKG_TAG']             "https://api.github.com/repos/microsoft/vcpkg/releases/latest"        { param($r) $r.tag_name }
    Check-DepPin "httplib" $deps['HTTPLIB_VERSION']       "https://api.github.com/repos/yhirose/cpp-httplib/releases/latest"    { param($r) $r.tag_name }

    if ($anyOutdated) {
        Write-Host ""
        Write-Host "  Some pins are outdated. Update deps.env before building if desired." -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "Proceed with build? [Y/n] (auto-continuing in 10 seconds)" -NoNewline
    $timeout  = 10
    $proceed  = $true
    $deadline = (Get-Date).AddSeconds($timeout)
    while ((Get-Date) -lt $deadline) {
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            Write-Host ""
            if ($key.KeyChar -match '^[Nn]') { $proceed = $false }
            break
        }
        Start-Sleep -Milliseconds 200
    }
    if ((Get-Date) -ge $deadline) { Write-Host " (timed out, continuing)" }
    if (-not $proceed) {
        Write-Host "Build cancelled." -ForegroundColor Red
        Stop-Transcript | Out-Null
        exit 0
    }
    Write-Host ""
}

# Check for Docker
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Error "Docker not found. Please install Docker Desktop."
    exit 1
}

# When targeting Windows containers, verify the daemon OS matches
if ($Windows) {
    $daemonOs = docker --context $Context version --format "{{.Server.Os}}" 2>$null
    if ($daemonOs -ne "windows") {
        Write-Host "ERROR: Docker daemon is running in '$daemonOs' mode." -ForegroundColor Red
        Write-Host "Switch to Windows containers mode first:" -ForegroundColor Yellow
        Write-Host "  & 'C:\Program Files\Docker\Docker\DockerCli.exe' -SwitchDaemon" -ForegroundColor Yellow
        Write-Host "Then re-run this script." -ForegroundColor Yellow
        exit 1
    }
}

# Build the Docker image
Write-Host "Building Docker image..." -ForegroundColor Cyan
$dockerfilePath = Join-Path $ScriptDir $Dockerfile
$dockerBuildArgs = @("--context", $Context, "build", "-f", $dockerfilePath, "-t", $ImageName)
if ($NoCache) { $dockerBuildArgs += "--no-cache" }
foreach ($kv in $deps.GetEnumerator()) {
    $dockerBuildArgs += "--build-arg"
    $dockerBuildArgs += ($kv.Key + "=" + $kv.Value)
}
$dockerBuildArgs += $ScriptDir
docker @dockerBuildArgs
if ($LASTEXITCODE -ne 0) { Write-Error "docker build failed."; exit 1 }

# Extract dist packages via docker cp (avoids volume-mount path issues)
Write-Host "Extracting dist packages..." -ForegroundColor Cyan
$containerId = docker --context $Context create $ImageName
if ($LASTEXITCODE -ne 0) { Write-Error "docker create failed."; exit 1 }

try {
    if (Test-Path $OutputDir) {
        Remove-Item -Recurse -Force $OutputDir -ErrorAction SilentlyContinue
        if (Test-Path $OutputDir) {
            Write-Host "Warning: could not fully remove existing dist\ (file lock). Overwriting in place." -ForegroundColor Yellow
        }
    }

    # Windows containers expose C:\dist; Linux containers expose /dist
    # Copy to the *parent* so docker cp names the folder 'dist' itself.
    # (Copying into an existing destination dir would produce dist\dist.)
    $containerPath = if ($Windows) { "${containerId}:C:\dist" } else { "${containerId}:/dist" }
    $parentDir = Split-Path -Parent $OutputDir
    docker --context $Context cp $containerPath $parentDir
    if ($LASTEXITCODE -ne 0) { Write-Error "docker cp failed."; exit 1 }
} finally {
    docker --context $Context rm $containerId | Out-Null
    Compress-Archive -Path (Join-Path $ScriptDir "dist\base\*")        -DestinationPath (Join-Path $ScriptDir "release\HoundTTS-windows.zip")              -Force
    Compress-Archive -Path (Join-Path $ScriptDir "dist\piper-addon\*") -DestinationPath (Join-Path $ScriptDir "release\HoundTTS-piper-addon-windows.zip")  -Force
}


Write-Host ""
Write-Host "=== Build successful! ===" -ForegroundColor Green
Write-Host "Output in: $OutputDir"
Write-Host ""
Write-Host "Packages:"
Write-Host "  dist\base\        <- DLL + Lua scripts (ExternalAudio)"
Write-Host "  dist\piper-addon\ <- Piper TTS engine + bundled voices"
Write-Host "  release\          <- Zip packages"
Write-Host ""
Write-Host "To install, copy the contents of the desired package(s) into:"
Write-Host "  $env:USERPROFILE\Saved Games\DCS.openbeta\"
Write-Host "  (or DCS\ for stable release)"
Write-Host ""
Write-Host "For Piper TTS, install both base\ and piper-addon\."
Write-Host ""

Stop-Transcript | Out-Null
