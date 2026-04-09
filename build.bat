@echo off
REM Build HoundTTS using Docker via PowerShell script
REM This batch file invokes build-docker.ps1 for systems without PowerShell script execution configured

setlocal

REM Get the directory where this batch file is located
set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%build-docker.ps1"

REM Check if PowerShell script exists
if not exist "%PS_SCRIPT%" (
    echo ERROR: build-docker.ps1 not found in %SCRIPT_DIR%
    exit /b 1
)

REM Check if Docker is installed
where docker >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Docker not found. Please install Docker Desktop.
    exit /b 1
)

REM Check if Docker daemon is running
docker info >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Docker daemon is not running. Please start Docker Desktop.
    exit /b 1
)

REM Pass all arguments to PowerShell script
REM -ExecutionPolicy Bypass allows the script to run regardless of system policy
REM -NoProfile avoids loading user profile for faster startup
powershell.exe -ExecutionPolicy Bypass -NoProfile -File "%PS_SCRIPT%" %*

exit /b %ERRORLEVEL%