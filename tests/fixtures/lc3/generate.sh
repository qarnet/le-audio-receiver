#!/usr/bin/env bash
# Regenerate the checked-in LC3 fixtures (tests/fixtures/lc3/*.lc3, *.pcm).
#
# This is a REPRODUCIBILITY tool only.  The test suites never run it;
# they embed the checked-in binaries.  Run it from anywhere; it writes
# the fixture files next to this script and removes its temporary
# executable (mktemp + EXIT trap) afterwards.
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

TMPBIN="$(mktemp /tmp/le-audio-lc3-gen.XXXXXX)" || {
    echo "FATAL: mktemp failed" >&2
    exit 1
}
trap 'rm -f "$TMPBIN"' EXIT

# Same relevant flags as zephyr/modules/liblc3/CMakeLists.txt.  -Wno-array-bounds
# is intentionally NOT passed: the generator must compile warning-free on its
# own, and no suppression may be added without a recorded compiler diagnostic
# naming a source file (see the review-fix handoff policy).
"$CC" -O3 -std=c11 -ffast-math \
      -Wall -Wextra -Wdouble-promotion -Wvla -pedantic \
      -I "$LC3/include" \
      gen_fixtures.c "$LC3"/src/*.c \
      -lm -o "$TMPBIN"

"$TMPBIN"

echo "---"
sha256sum ./*.lc3 ./*.pcm
