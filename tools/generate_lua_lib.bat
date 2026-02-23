@echo off
setlocal

echo === Generate lua.lib from lua.dll ===
echo.

set "SCRIPT_DIR=%~dp0"
set "LIB_DIR=%SCRIPT_DIR%..\lib"
set "LUA_DLL=%LIB_DIR%\lua.dll"
set "LUA_DEF=%LIB_DIR%\lua.def"
set "LUA_LIB=%LIB_DIR%\lua.lib"

if not exist "%LUA_DLL%" (
    echo ERROR: lua.dll not found at %LUA_DLL%
    exit /b 1
)

if exist "%LUA_LIB%" (
    echo lua.lib already exists at %LUA_LIB%
    echo Delete it first if you want to regenerate.
    exit /b 0
)

:: Try MinGW tools (gendef + dlltool)
where gendef >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo Using MinGW gendef + dlltool ...
    gendef "%LUA_DLL%"
    if %ERRORLEVEL% neq 0 (
        echo ERROR: gendef failed.
        exit /b 1
    )
    :: gendef writes lua.def in current dir
    if exist "lua.def" move /y "lua.def" "%LUA_DEF%" >nul
    dlltool -d "%LUA_DEF%" -l "%LUA_LIB%" -D lua.dll
    if %ERRORLEVEL% neq 0 (
        echo ERROR: dlltool failed.
        exit /b 1
    )
    echo Generated %LUA_LIB% successfully.
    exit /b 0
)

:: Try MSVC tools (dumpbin + lib)
where dumpbin >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo Using MSVC dumpbin + lib ...

    :: Extract exports
    dumpbin /exports "%LUA_DLL%" > "%LIB_DIR%\lua_exports.txt"

    :: Generate .def file
    echo LIBRARY lua.dll > "%LUA_DEF%"
    echo EXPORTS >> "%LUA_DEF%"
    for /f "tokens=4" %%a in ('dumpbin /exports "%LUA_DLL%" ^| findstr /r "^  *[0-9]"') do (
        echo   %%a >> "%LUA_DEF%"
    )

    :: Generate .lib
    lib /def:"%LUA_DEF%" /out:"%LUA_LIB%" /machine:x64
    if %ERRORLEVEL% neq 0 (
        echo ERROR: lib.exe failed.
        exit /b 1
    )

    :: Clean up temp files
    if exist "%LIB_DIR%\lua_exports.txt" del "%LIB_DIR%\lua_exports.txt"
    if exist "%LIB_DIR%\lua.exp" del "%LIB_DIR%\lua.exp"

    echo Generated %LUA_LIB% successfully.
    exit /b 0
)

echo ERROR: No suitable tools found.
echo Install MinGW-w64 (for gendef/dlltool) or MSVC Build Tools (for dumpbin/lib).
exit /b 1

endlocal
