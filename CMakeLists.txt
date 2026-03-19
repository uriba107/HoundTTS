cmake_minimum_required(VERSION 3.15)

# Allow CI to inject version via -DHOUNDTTS_VERSION_OVERRIDE=x.y.z (numeric only, e.g. 0.2.0)
set(HOUNDTTS_VERSION_OVERRIDE "" CACHE STRING "Override project version (set by CI from git tag)")
# Full version string including pre-release suffix (e.g. 0.2.0-beta1); defaults to numeric version
set(HOUNDTTS_VERSION_FULL     "" CACHE STRING "Full version string with pre-release suffix (e.g. 0.2.0-beta1)")

if(HOUNDTTS_VERSION_OVERRIDE)
    project(HoundTTS VERSION ${HOUNDTTS_VERSION_OVERRIDE} LANGUAGES CXX)
else()
    project(HoundTTS VERSION 0.0.0 LANGUAGES CXX)
endif()

# If no full version was provided, fall back to the numeric project version
if(NOT HOUNDTTS_VERSION_FULL)
    set(HOUNDTTS_VERSION_FULL "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Lua 5.1 headers & import library ---
set(LUA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include/lua")
set(LUA_DLL         "${CMAKE_SOURCE_DIR}/lib/lua.dll")
set(LUA_IMPLIB      "${CMAKE_SOURCE_DIR}/lib/lua.lib")

# --- libopus ---
# Provided by vcpkg x64-windows-static triplet
set(OPUS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include/opus")
find_package(Opus CONFIG REQUIRED)
set(OPUS_TARGET Opus::opus)


# Create imported Lua library target
add_library(lua SHARED IMPORTED)
set_target_properties(lua PROPERTIES
    IMPORTED_LOCATION "${LUA_DLL}"
    IMPORTED_IMPLIB   "${LUA_IMPLIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
)

# --- OpenSSL (for cpp-httplib HTTPS/WebSocket) ---
# Provided by vcpkg x64-windows-static triplet
find_package(OpenSSL REQUIRED)
set(OPENSSL_SSL_LIB OpenSSL::SSL)
set(OPENSSL_CRYPTO_LIB OpenSSL::Crypto)

# --- Version resource (right-click → Properties → Details) ---
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.rc.in"
    "${CMAKE_BINARY_DIR}/version.rc"
    @ONLY
)

# --- HoundTTS DLL ---
add_library(HoundTTS SHARED
    ${CMAKE_BINARY_DIR}/version.rc
    # Core
    src/dllmain.cpp
    src/lua_tts.cpp
    src/lua_translate.cpp
    src/tts_pipeline.cpp
    src/config_reader.cpp
    src/process_launcher.cpp
    src/backends/codecs/opus_encoder.cpp
    # Backends
    src/backends/srs/srs_client.cpp
    src/backends/srs/srs_backend.cpp
    # Shared auth helpers
    src/providers/shared/google/google_auth.cpp
    src/providers/shared/aws/aws_auth.cpp
    # TTS providers
    src/providers/tts/sapi/sapi_tts.cpp
    src/providers/tts/piper/piper_tts.cpp
    src/providers/tts/azure/azure_tts.cpp
    src/providers/tts/google/google_tts.cpp
    src/providers/tts/elevenlabs/elevenlabs_tts.cpp
    src/providers/tts/aws/aws_tts.cpp
    src/providers/tts/kitten/kitten_tts.cpp
    src/providers/tts/openai/openai_tts.cpp
    # Translation providers
    src/providers/translate/openai/openai_chat.cpp
    src/providers/translate/google/google_translate.cpp
    src/providers/translate/libretranslate/libretranslate.cpp
    src/providers/translate/aws/aws_translate.cpp
    src/providers/translate/azure/azure_translate.cpp
)

target_include_directories(HoundTTS PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(HoundTTS PRIVATE
    LUA_BUILD_AS_DLL
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    CPPHTTPLIB_OPENSSL_SUPPORT
    HOUNDTTS_VERSION="${PROJECT_VERSION}"
    HOUNDTTS_VERSION_FULL="${HOUNDTTS_VERSION_FULL}"
)

# MSVC: static runtime (/MT), disable warnings that fire on Windows SDK headers
if(MSVC)
    target_compile_options(HoundTTS PRIVATE
        /MT            # static CRT — self-contained DLL, no VCRUNTIME dependency
        /W3            # reasonable warning level
        /wd4996        # deprecation warnings from older SDK headers
        /wd4267        # size_t → int narrowing in third-party headers
    )
endif()


# MSVC: add Windows SDK include path so sapi.h / sphelper.h are found
if(MSVC)
    # The Windows SDK is on the include path automatically via vcvars64;
    # this makes it explicit for IDEs that don't source vcvars.
    if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
        target_include_directories(HoundTTS PRIVATE
            "$ENV{WindowsSdkDir}Include\\$ENV{WindowsSDKVersion}um"
            "$ENV{WindowsSdkDir}Include\\$ENV{WindowsSDKVersion}shared"
        )
    endif()
endif()

target_link_libraries(HoundTTS PRIVATE
    lua
    ${OPUS_TARGET}
    ${OPENSSL_SSL_LIB}
    ${OPENSSL_CRYPTO_LIB}
    advapi32
    winmm
    ws2_32
    ole32
    oleaut32
    crypt32
)

# MSVC: additional libs needed for SAPI COM and static OpenSSL
if(MSVC)
    target_link_libraries(HoundTTS PRIVATE
        sapi          # SAPI 5.4 — present in Windows 11 SDK
        user32
        gdi32
        bcrypt        # required by OpenSSL static on Windows
    )
endif()


# MSVC: suppress default lib conflicts that arise from /MT + OpenSSL static
if(MSVC)
    target_link_options(HoundTTS PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
endif()

# Output name: HoundTTS.dll
set_target_properties(HoundTTS PROPERTIES
    PREFIX ""
    OUTPUT_NAME "HoundTTS"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Install / dist ---
install(TARGETS HoundTTS
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
install(DIRECTORY dcs/ DESTINATION dcs)
