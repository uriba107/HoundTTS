# tools — Build & Utility Scripts

## Purpose

Build scripts, dependency patches, vendored headers, and utility tools for developing and packaging HoundTTS.

## Ownership

- `httplib.h` — Vendored cpp-httplib (single-header HTTP/WS library, OpenSSL support)

- `patch_piper.ps1` — Patches piper.dll build artifacts for HoundTTS integration
- `patch_supertonic.ps1` — Patches supertonic.dll for HoundTTS integration
- `install.bat` — Windows install script for end users
- `generate_lua_lib.bat` — Generates `lua.lib` import library from `lua.dll`
- `supertonic_cmake/CMakeLists.txt` — CMake build for supertonic.dll
- `supertonic_api.cpp` — C API shim for supertonic TTS engine

## Local Contracts

- All patch scripts (`.ps1`) target MSVC builds on Windows
- `httplib.h` must remain untouched; upstream fixes should be handled by bumping `HTTPLIB_VERSION` in `deps.env`
- `install.bat` copies distribution files into Saved Games directories

## Work Guidance

- Keep `httplib.h` in sync with upstream for security fixes
- Patch scripts should be idempotent (check before patching)
- Build scripts target x64 Windows only (x86 not supported)

## Verification

Run patch scripts against a clean checkout to verify they apply cleanly.

## Child DOX Index

- `supertonic_cmake/` — CMake build files for supertonic.dll
