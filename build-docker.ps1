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

.EXAMPLE
    .\build-docker.ps1
    .\build-docker.ps1 -Windows
    .\build-docker.ps1 -Windows -Context winhost
#>
param(
    [switch]$Windows = $true,
    [string]$Context = "default"
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
docker --context $Context build -f $dockerfilePath -t $ImageName $ScriptDir
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
    Compress-Archive -Path dist\base\*         -DestinationPath release\HoundTTS-windows.zip         -Force
    Compress-Archive -Path dist\piper-addon\*  -DestinationPath release\HoundTTS-piper-addon-windows.zip  -Force
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
