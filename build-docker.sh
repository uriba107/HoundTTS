#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/dist"
LOG_FILE="${SCRIPT_DIR}/build.log"

# Tee all stdout+stderr to build.log
exec > >(tee "${LOG_FILE}") 2>&1

# --- Argument parsing ---
DOCKERFILE="Dockerfile"
IMAGE_NAME="houndtts-builder"
DOCKER_CONTEXT="default"

usage() {
    echo "Usage: $0 [--windows|-w] [--context|-c <docker-context>]"
    echo ""
    echo "  (no flags)           Linux/MinGW cross-compile (default)"
    echo "  --windows, -w        Windows container / MSVC build (requires Windows Docker host)"
    echo "  --context, -c NAME   Docker context to use (default: 'default')"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --windows|-w)
            DOCKERFILE="Dockerfile.windows"
            IMAGE_NAME="houndtts-builder-msvc"
            shift ;;
        --context|-c)
            [[ -z "${2:-}" ]] && usage
            DOCKER_CONTEXT="$2"
            shift 2 ;;
        --help|-h) usage ;;
        *) echo "Unknown option: $1"; usage ;;
    esac
done

echo "=== HoundTTS Docker Build ==="
echo "  Dockerfile : ${DOCKERFILE}"
echo "  Context    : ${DOCKER_CONTEXT}"
echo ""

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo "ERROR: Docker not found. Please install Docker."
    exit 1
fi

# Build the Docker image
echo "Building Docker image..."
docker --context "${DOCKER_CONTEXT}" build -f "${SCRIPT_DIR}/${DOCKERFILE}" -t "${IMAGE_NAME}" "${SCRIPT_DIR}"

# Extract both packages from the container
# (uses docker cp to avoid volume-mount issues with spaces in paths on macOS)
echo "Extracting dist packages..."
CONTAINER_ID=$(docker --context "${DOCKER_CONTEXT}" create "${IMAGE_NAME}")
rm -rf "${OUTPUT_DIR}"
docker --context "${DOCKER_CONTEXT}" cp "${CONTAINER_ID}:/dist" "${OUTPUT_DIR}"
docker --context "${DOCKER_CONTEXT}" rm "${CONTAINER_ID}" > /dev/null

echo ""
echo "=== Build successful! ==="
echo "Output in: ${OUTPUT_DIR}"
echo ""
echo "Packages:"
echo "  dist/base/        <- DLL + Lua scripts (ExternalAudio)"
echo "  dist/piper-addon/ <- Piper TTS engine + bundled voices"
echo ""
echo "To install, copy the contents of the desired package(s) into:"
echo "  %USERPROFILE%\\Saved Games\\DCS.openbeta\\"
echo "  (or DCS\\ for stable release)"
echo ""
echo "For Piper TTS, install both base/ and piper-addon/."
echo ""
