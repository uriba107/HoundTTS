# Build supertonic.dll — C API wrapper around Supertonic's helper.cpp
#
# Expected variables (set by parent or via -D):
#   SUPERTONIC_SRC_DIR   — path to supertonic repo's cpp/ directory (helper.h/.cpp)
#   ONNXRUNTIME_DIR      — path to onnxruntime pre-built directory
#   SUPERTONIC_API_CPP   — path to tools/supertonic_api.cpp
#   SUPERTONIC_INCLUDE   — path to include/ directory (contains supertonic/supertonic.h)
#
# Produces: supertonic.dll (with C exports only)

cmake_minimum_required(VERSION 3.15)
project(supertonic_dll LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- ONNX Runtime ---
if(NOT ONNXRUNTIME_DIR)
    message(FATAL_ERROR "ONNXRUNTIME_DIR must be set")
endif()

set(ORT_INCLUDE "${ONNXRUNTIME_DIR}/include")
set(ORT_LIB_DIR "${ONNXRUNTIME_DIR}/lib")

# --- nlohmann/json (from vcpkg) ---
find_package(nlohmann_json REQUIRED)

# --- Sources ---
if(NOT SUPERTONIC_SRC_DIR)
    message(FATAL_ERROR "SUPERTONIC_SRC_DIR must be set (path to supertonic cpp/ directory)")
endif()
if(NOT SUPERTONIC_API_CPP)
    message(FATAL_ERROR "SUPERTONIC_API_CPP must be set")
endif()
if(NOT SUPERTONIC_INCLUDE)
    message(FATAL_ERROR "SUPERTONIC_INCLUDE must be set")
endif()

add_library(supertonic SHARED
    "${SUPERTONIC_SRC_DIR}/helper.cpp"
    "${SUPERTONIC_API_CPP}"
)

target_include_directories(supertonic PRIVATE
    "${SUPERTONIC_SRC_DIR}"
    "${SUPERTONIC_INCLUDE}"
    "${ORT_INCLUDE}"
)

# Find onnxruntime.lib
find_library(ORT_LIB onnxruntime PATHS "${ORT_LIB_DIR}" NO_DEFAULT_PATH)
if(NOT ORT_LIB)
    message(FATAL_ERROR "onnxruntime.lib not found in ${ORT_LIB_DIR}")
endif()

target_link_libraries(supertonic PRIVATE
    ${ORT_LIB}
    nlohmann_json::nlohmann_json
)

# Export only C API symbols
target_compile_definitions(supertonic PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    SUPERTONIC_BUILDING_DLL
)

if(MSVC)
    target_compile_options(supertonic PRIVATE
        /MT        # static CRT
        /W3
        /wd4996
        /wd4267
        /MP
        /utf-8
    )
    target_link_options(supertonic PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
endif()

set_target_properties(supertonic PROPERTIES
    PREFIX ""
    OUTPUT_NAME "supertonic"
)

install(TARGETS supertonic RUNTIME DESTINATION .)
