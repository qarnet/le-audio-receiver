#!/usr/bin/env bash
# Flash both nRF5340 cores (app + net) in a single OpenOCD session.
# Recovers device first so APPROTECT is disabled throughout the session.
# Usage: ./scripts/flash_openocd.sh [build_dir]
set -euo pipefail

BUILD_DIR="${1:-build}"
APP_HEX="$(realpath "$BUILD_DIR/merged.hex")"
NET_HEX="$(realpath "$BUILD_DIR/merged_CPUNET.hex")"

for f in "$APP_HEX" "$NET_HEX"; do
    [[ -f "$f" ]] || { echo "ERROR: not found: $f"; exit 1; }
done

echo "App hex: $APP_HEX"
echo "Net hex: $NET_HEX"
echo ""

openocd \
    -f interface/cmsis-dap.cfg \
    -c "transport select swd" \
    -c "adapter speed 100" \
    -f target/nordic/nrf53.cfg \
    -f scripts/flash_nrf5340.tcl \
    -c "flash_both $APP_HEX $NET_HEX" \
    -c "shutdown"
