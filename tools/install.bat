@echo off
setlocal EnableDelayedExpansion

:: HoundTTS installer
:: Injects the dofile line into MissionScripting.lua and copies mod files.

set "INJECT_LINE=dofile(lfs.writedir()..[[Mods\\Services\\HoundTTS\\Scripts\\HoundTTS-mission.lua]])"
set "INJECT_MARKER=HoundTTS-mission.lua"
set "INSERT_BEFORE=--Sanitize Mission Scripting environment"

:: -------------------------------------------------------------------------
:: Find DCS install dir
:: -------------------------------------------------------------------------
set "DCS_INSTALL="
for %%K in (
    "HKLM\SOFTWARE\Eagle Dynamics\DCS World OpenBeta"
    "HKLM\SOFTWARE\Eagle Dynamics\DCS World"
    "HKLM\SOFTWARE\WOW6432Node\Eagle Dynamics\DCS World OpenBeta"
    "HKLM\SOFTWARE\WOW6432Node\Eagle Dynamics\DCS World"
) do (
    if not defined DCS_INSTALL (
        for /f "tokens=2*" %%A in ('reg query %%K /v "Path" 2^>nul') do set "DCS_INSTALL=%%B"
    )
)

if not defined DCS_INSTALL (
    echo ERROR: DCS World install not found in registry.
    echo Please set DCS_INSTALL manually and re-run.
    pause & exit /b 1
)

set "MISSION_SCRIPTING=%DCS_INSTALL%\Scripts\MissionScripting.lua"

if not exist "%MISSION_SCRIPTING%" (
    echo ERROR: MissionScripting.lua not found at:
    echo   %MISSION_SCRIPTING%
    pause & exit /b 1
)

echo Found DCS at: %DCS_INSTALL%

:: -------------------------------------------------------------------------
:: Find DCS Saved Games dir
:: -------------------------------------------------------------------------
set "SAVED_GAMES="
for %%V in ("DCS.openbeta" "DCS") do (
    if not defined SAVED_GAMES (
        if exist "%USERPROFILE%\Saved Games\%%~V\Scripts" (
            set "SAVED_GAMES=%USERPROFILE%\Saved Games\%%~V"
        )
    )
)

if not defined SAVED_GAMES (
    set "SAVED_GAMES=%USERPROFILE%\Saved Games\DCS.openbeta"
    echo WARNING: Saved Games folder not found, defaulting to:
    echo   %SAVED_GAMES%
)

echo Saved Games: %SAVED_GAMES%

:: -------------------------------------------------------------------------
:: Patch MissionScripting.lua
:: -------------------------------------------------------------------------
findstr /C:"%INJECT_MARKER%" "%MISSION_SCRIPTING%" >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo MissionScripting.lua already patched, skipping.
) else (
    echo Patching MissionScripting.lua...

    set "TEMP_FILE=%TEMP%\MissionScripting_patched.lua"
    set "PATCHED=0"

    > "%TEMP_FILE%" (
        for /f "usebackq delims=" %%L in ("%MISSION_SCRIPTING%") do (
            set "LINE=%%L"
            if "!PATCHED!"=="0" (
                echo !LINE! | findstr /C:"%INSERT_BEFORE%" >nul 2>&1
                if !ERRORLEVEL! equ 0 (
                    echo %INJECT_LINE%
                    set "PATCHED=1"
                )
            )
            echo !LINE!
        )
    )

    if "!PATCHED!"=="0" (
        echo ERROR: Could not find insertion point in MissionScripting.lua.
        echo Please add this line manually before the sanitizeModule block:
        echo   %INJECT_LINE%
        del "%TEMP_FILE%"
        pause & exit /b 1
    )

    :: Backup original
    copy /y "%MISSION_SCRIPTING%" "%MISSION_SCRIPTING%.bak" >nul
    echo   Backup saved to MissionScripting.lua.bak

    copy /y "%TEMP_FILE%" "%MISSION_SCRIPTING%" >nul
    del "%TEMP_FILE%"
    echo   MissionScripting.lua patched successfully.
)

:: -------------------------------------------------------------------------
:: Copy mod files
:: -------------------------------------------------------------------------
echo Copying mod files...

set "DIST_DIR=%~dp0..\dist"

if not exist "%DIST_DIR%\Mods\Services\HoundTTS\bin\HoundTTS.dll" (
    echo ERROR: dist\ not found. Run build.bat first.
    pause & exit /b 1
)

xcopy /e /i /y "%DIST_DIR%\Mods" "%SAVED_GAMES%\Mods" >nul
xcopy /e /i /y "%DIST_DIR%\Scripts" "%SAVED_GAMES%\Scripts" >nul

echo.
echo === HoundTTS installed successfully! ===
echo.
pause
