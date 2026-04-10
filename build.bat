@echo off
REM Build HoundTTS using Docker via PowerShell script
REM This batch file invokes build-docker.ps1 for systems without PowerShell script execution configured

setlocal

REM Capture start time in ISO format (locale-independent)
for /f "usebackq" %%i in (`powershell -Command "Get-Date -Format o"`) do set "START_TIME=%%i"

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

REM Preserve build exit code before PowerShell timing command overwrites ERRORLEVEL
set "BUILD_EXIT=%ERRORLEVEL%"

REM Calculate and display elapsed time — guard against missing/invalid START_TIME
echo.
echo --------------------------------------------------
if defined START_TIME (
    powershell -Command "try { $start = [DateTime]::Parse($env:START_TIME); $end = Get-Date; $diff = $end - $start; Write-Host 'Build completed in:' $diff.ToString('hh\:mm\:ss\.fff') } catch { Write-Host 'Build duration unavailable' }"
) else (
    echo Build duration unavailable
)
echo --------------------------------------------------

exit /b %BUILD_EXIT%