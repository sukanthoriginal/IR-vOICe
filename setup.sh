#!/usr/bin/env bash
# One-time setup on the Raspberry Pi. Installs deps, builds raspicam + wiringPi,
# then builds raspivoice. Re-run is safe — skips steps already done.
set -euo pipefail

cd "$(dirname "$0")"
ROOT="$PWD"

echo "==> apt packages"
sudo apt-get update
sudo apt-get install -y \
    git cmake build-essential pkg-config \
    libopencv-dev \
    libncurses5-dev \
    libasound2-dev \
    libespeak-dev espeak \
    alsa-utils \
    wget unzip ca-certificates

# raspicam intentionally NOT installed — the build is compiled with NO_RASPICAM
# (we use the USB IR camera via -s2, not the CSI camera). raspicam needs the
# legacy Broadcom MMAL stack which is gone on Bookworm.

if ! command -v gpio >/dev/null 2>&1; then
    echo "==> installing wiringPi"
    arch=$(dpkg --print-architecture)
    tmp=$(mktemp -d)
    cd "$tmp"
    wget -q "https://github.com/WiringPi/WiringPi/releases/download/3.10/wiringpi_3.10_${arch}.deb"
    sudo dpkg -i "wiringpi_3.10_${arch}.deb" || sudo apt-get install -f -y
    cd "$ROOT"
    rm -rf "$tmp"
else
    echo "==> wiringPi already installed, skipping"
fi

echo "==> building raspivoice"
cd "$ROOT/raspivoice"
model=$(tr -d '\0' </proc/device-tree/model 2>/dev/null || echo "")
if [[ "$model" =~ Raspberry\ Pi\ [2-9] ]] || [[ "$model" =~ Raspberry\ Pi\ Zero\ 2 ]]; then
    echo "    detected: $model -> CONFIG=release_rpi2"
    make -j"$(nproc)" CONFIG=release_rpi2
else
    echo "    detected: ${model:-unknown} -> CONFIG=release"
    make -j"$(nproc)"
fi

echo
echo "==> done. run with:  ./run.sh"
