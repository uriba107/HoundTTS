@echo off
setlocal enabledelayedexpansion

echo === HoundTTS Build Script ===
echo.

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "DIST_DIR=%PROJECT_DIR%dist"

:: Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake and add it to PATH.
    exit /b 1
)

:: Detect compiler
set "GENERATOR="
set "TOOLCHAIN="

:: Try MinGW first
where x86_64-w64-mingw32-gcc >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo Found MinGW-w64 cross compiler
    set "GENERATOR=MinGW Makefiles"
    goto :build
)

where gcc >nul 2>&1
if %ERRORLEVEL% equ 0 (
    :: Check if it's MinGW GCC (not MSYS/Cygwin)
    gcc -dumpmachine 2>nul | findstr /i "mingw" >nul
    if !ERRORLEVEL! equ 0 (
        echo Found MinGW GCC
        set "GENERATOR=MinGW Makefiles"
        goto :build
    )
)

:: Try MSVC (Visual Studio Build Tools, no IDE needed)
where cl >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo Found MSVC compiler
    set "GENERATOR=NMake Makefiles"
    goto :build
)

:: Try to find vcvarsall.bat for MSVC
set "VCVARSALL="
for %%v in (2022 2019 2017) do (
    for %%e in (Enterprise Professional Community BuildTools) do (
        set "CANDIDATE=C:\Program Files\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat"
        if exist "!CANDIDATE!" (
            set "VCVARSALL=!CANDIDATE!"
            goto :found_vcvars
        )
        set "CANDIDATE=C:\Program Files (x86)\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat"
        if exist "!CANDIDATE!" (
            set "VCVARSALL=!CANDIDATE!"
            goto :found_vcvars
        )
    )
)

:found_vcvars
if defined VCVARSALL (
    echo Found MSVC via: %VCVARSALL%
    call "%VCVARSALL%" x64
    set "GENERATOR=NMake Makefiles"
    goto :build
)

echo ERROR: No suitable compiler found.
echo Please install one of:
echo   - MinGW-w64 (recommended): https://www.mingw-w64.org/
echo   - Visual Studio Build Tools: https://visualstudio.microsoft.com/visual-cpp-build-tools/
exit /b 1

:build
echo.
echo Using generator: %GENERATOR%
echo.

:: Generate lua.lib if missing and using MinGW
if not exist "%PROJECT_DIR%lib\lua.lib" (
    echo Generating lua.lib from lua.dll ...
    call "%PROJECT_DIR%tools\generate_lua_lib.bat"
    if %ERRORLEVEL% neq 0 (
        echo ERROR: Failed to generate lua.lib
        exit /b 1
    )
)

:: Clean and create build directory
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"

:: Configure
echo Configuring...
cmake -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=Release -S "%PROJECT_DIR%" -B "%BUILD_DIR%"
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

:: Build
echo Building...
cmake --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed.
    exit /b 1
)

:: Copy output to dist/ using DCS mod layout
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%\Mods\Services\HoundTTS\bin"
mkdir "%DIST_DIR%\Mods\Services\HoundTTS\Scripts"
mkdir "%DIST_DIR%\Scripts\Hooks"

copy "%BUILD_DIR%\bin\HoundTTS.dll" "%DIST_DIR%\Mods\Services\HoundTTS\bin\" >nul
copy "%PROJECT_DIR%dcs\Mods\Services\HoundTTS\Scripts\HoundTTS.lua" "%DIST_DIR%\Mods\Services\HoundTTS\Scripts\" >nul
copy "%PROJECT_DIR%dcs\Mods\Services\HoundTTS\Scripts\HoundTTS-mission.lua" "%DIST_DIR%\Mods\Services\HoundTTS\Scripts\" >nul
copy "%PROJECT_DIR%dcs\Scripts\Hooks\HoundTTS-hook.lua" "%DIST_DIR%\Scripts\Hooks\" >nul

echo.
echo === Build successful! ===
echo Output in: %DIST_DIR%
echo.
echo To install, copy the contents of dist\ into:
echo   %%USERPROFILE%%\Saved Games\DCS.openbeta\
echo   (or DCS\ for stable release)
echo.

endlocal
