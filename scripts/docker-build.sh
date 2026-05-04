#!/bin/bash
# Build the firmware inside the nRF Connect SDK toolchain container.
#
# Layout: this repo is mounted as /workspace/app, and a host cache dir holds
# the west-managed clones (zephyr, nrf, modules, ...) at /workspace. west init
# -l makes topdir the parent of the manifest, so the manifest must live one
# level below the workspace root — hence the /workspace/app split.
set -euo pipefail

cd "$(dirname "$0")/.."

IMG=ghcr.io/nrfconnect/sdk-nrf-toolchain:911f4c5c26
BOARD=nrf5340dk/nrf5340/cpuapp
WS_HOST="${XDG_CACHE_HOME:-$HOME/.cache}/le-audio-receiver-workspace"

mkdir -p "$WS_HOST"

docker run --rm -t \
  --user "$(id -u):$(id -g)" \
  -v "$WS_HOST:/workspace" \
  -v "$(pwd):/workspace/app" \
  -w /workspace/app \
  -e HOME=/tmp \
  "$IMG" \
  '
    set -e
    git config --global --add safe.directory "*"
    if [ ! -d /workspace/.west ]; then
      west init -l /workspace/app
    fi
    cd /workspace
    west update
    cd /workspace/app
    rm -rf build
    west build -b '"$BOARD"' --sysbuild --build-dir build .
  '
