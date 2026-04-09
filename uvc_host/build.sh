#!/bin/bash
# Build script for uvc_host on NVIDIA Orin AGX (aarch64 Linux)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=== Building UVC Host Controller ==="
echo "Source: ${SCRIPT_DIR}"
echo "Build:  ${BUILD_DIR}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${SCRIPT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)

echo ""
echo "=== Build complete ==="
echo "Binary: ${BUILD_DIR}/uvc_host"
echo ""
echo "Usage examples:"
echo "  # Default: stream from /dev/video0 and /dev/video2"
echo "  sudo ./uvc_host"
echo ""
echo "  # Save H.265 stream to file"
echo "  sudo ./uvc_host -s"
echo ""
echo "  # Custom resolution and encoding"
echo "  sudo ./uvc_host -d /dev/video0 -W 1280 -H 720 -e h264 -f 25 -b 1000 -s"
echo ""
echo "  # Single camera"
echo "  sudo ./uvc_host -d /dev/video0 -s"
