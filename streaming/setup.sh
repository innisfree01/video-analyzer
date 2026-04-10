#!/bin/bash
#
# MediaMTX setup script for NVIDIA Jetson AGX Orin (aarch64)
# Downloads and installs MediaMTX for WebRTC video streaming.
#
set -e

MEDIAMTX_VERSION="v1.17.1"
ARCH="linux_arm64v8"
INSTALL_DIR="/opt/mediamtx"
TARBALL="mediamtx_${MEDIAMTX_VERSION}_${ARCH}.tar.gz"
URL="https://github.com/bluenviron/mediamtx/releases/download/${MEDIAMTX_VERSION}/${TARBALL}"

echo "=== MediaMTX Setup for Jetson AGX ==="
echo "Version:  ${MEDIAMTX_VERSION}"
echo "Arch:     ${ARCH}"
echo "Install:  ${INSTALL_DIR}"
echo ""

if [ -f "${INSTALL_DIR}/mediamtx" ]; then
    echo "MediaMTX already installed at ${INSTALL_DIR}/mediamtx"
    "${INSTALL_DIR}/mediamtx" --version 2>/dev/null || true
    echo ""
    read -p "Re-install? [y/N] " answer
    if [ "$answer" != "y" ] && [ "$answer" != "Y" ]; then
        echo "Skipping install."
        exit 0
    fi
fi

echo "Downloading ${URL} ..."
cd /tmp
wget -q --show-progress -O "${TARBALL}" "${URL}"

echo "Installing to ${INSTALL_DIR} ..."
sudo mkdir -p "${INSTALL_DIR}"
sudo tar -xzf "${TARBALL}" -C "${INSTALL_DIR}"
rm -f "${TARBALL}"

echo "Copying custom config ..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
sudo cp "${SCRIPT_DIR}/mediamtx.yml" "${INSTALL_DIR}/mediamtx.yml"

echo ""
echo "=== Installation Complete ==="
"${INSTALL_DIR}/mediamtx" --version 2>/dev/null || true
echo ""
echo "Next steps:"
echo "  1. Start uvc_host with pipe output:  sudo ./uvc_host -p"
echo "  2. Start streaming:                  bash streaming/start.sh"
echo "  3. Open browser:                     http://<AGX_IP>:8889"
