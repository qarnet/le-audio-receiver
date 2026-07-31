#!/usr/bin/env bash
# Regenerate the checked-in LC3 fixtures (tests/fixtures/lc3/*.lc3, *.pcm).
#
# This is a REPRODUCIBILITY tool only.  The test suites never run it;
# they embed the checked-in binaries.  Run it from anywhere; it writes
# the fixture files next to this script and removes its temporary
# executable afterwards.
#
# Prerequisite: NCS v3.3.0 installed at $HOME/ncs/v3.3.0 (override with
# NCS=/path/to/ncs).  Uses the host C compiler with the SAME relevant
# flags as the Zephyr liblc3 module build (-O3 -std=c11 -ffast-math) so
# the golden PCM is bit-exact against the native_sim production decoder.
#
# Usage:
#   bash tests/fixtures/lc3/generate.sh
#
# After regenerating, update the SHA-256 / CRC-32 records in
# tests/fixtures/lc3/README.md from the printed output.

set -euo pipefail

NCS="${NCS:-$HOME/ncs/v3.3.0}"
LC3="$NCS/modules/lib/liblc3"

if [ ! -f "$LC3/include/lc3.h" ]; then
    echo "FATAL: liblc3 module not found at $LC3" >&2
    exit 1
fi

HERE="$(cd -- "$(dirname "$0")" && pwd)"
cd "$HERE"

CC="${CC:-cc}"

# Same relevant flags as zephyr/modules/liblc3/CMakeLists.txt.
"$CC" -O3 -std=c11 -ffast-math -Wno-array-bounds -Wall -Wextra \
      -Wdouble-promotion -Wvla -pedantic \
      -I "$LC3/include" \
      gen_fixtures.c "$LC3"/src/*.c \
      -lm -o /tmp/le-audio-lc3-gen-fixtures

trap 'rm -f /tmp/le-audio-lc3-gen-fixtures' EXIT

/tmp/le-audio-lc3-gen-fixtures

echo "---"
sha256sum ./*.lc3 ./*.pcm
