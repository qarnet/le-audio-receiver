#!/bin/bash
# Interactive shell inside the nRF Connect SDK toolchain container.
# Use for west flash, gdb, debugging, etc. --privileged + /dev mount are
# required for J-Link USB access; ACCEPT_JLINK_LICENSE=1 is safe here because
# we attach a TTY (it would hang a non-interactive build).
set -euo pipefail

cd "$(dirname "$0")/.."

WS_HOST="${XDG_CACHE_HOME:-$HOME/.cache}/le-audio-receiver-workspace"
mkdir -p "$WS_HOST"

docker run --rm -ti --privileged \
  --user "$(id -u):$(id -g)" \
  -v /dev:/dev \
  -v /run/udev:/run/udev:ro \
  -v "$WS_HOST:/workspace" \
  -v "$(pwd):/workspace/app" \
  -w /workspace/app \
  -e HOME=/tmp \
  -e ACCEPT_JLINK_LICENSE=1 \
  ghcr.io/nrfconnect/sdk-nrf-toolchain:911f4c5c26 \
  bash
