#!/usr/bin/env bash
# Installs the build dependencies for payload/build.sh on Ubuntu 24.04 (WSL2 or Docker).
set -euo pipefail
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
$SUDO apt-get update
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends \
    build-essential ca-certificates curl file git python3 cmake ninja-build \
    xz-utils zstd patch flex bison gettext pkg-config \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    gcc-14-aarch64-linux-gnu \
    gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
    gcc-mingw-w64-i686 g++-mingw-w64-i686 binutils-mingw-w64-i686 \
    libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libxkbregistry-dev libfreetype-dev \
    libegl-dev libgl-dev libwayland-egl-backend-dev llvm-18-dev libpolly-18-dev libzstd-dev meson python3-mako python3-yaml libdrm-dev libexpat1-dev
