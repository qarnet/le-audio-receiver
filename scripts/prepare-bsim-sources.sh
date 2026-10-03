#!/usr/bin/env bash
# Populate pinned SDK sources needed by unit policy tests and the BSim worker.
# This is source provisioning only: no make, compiler, SDK installer or cache
# shortcut. A cache hit does not prove the optional west projects are present.
set -euo pipefail

if [ "$#" -ne 0 ]; then
    echo "Usage: bash scripts/prepare-bsim-sources.sh" >&2
    exit 2
fi
: "${ZEPHYR_BASE:?ZEPHYR_BASE must point to the Zephyr checkout}"
test -d "$ZEPHYR_BASE" || { echo "ERROR: Zephyr checkout missing" >&2; exit 1; }
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ncs_root="$(realpath "$ZEPHYR_BASE/..")"
cd "$ncs_root"
test "$(west topdir)" = "$ncs_root" || {
    echo "ERROR: west workspace does not match ZEPHYR_BASE parent" >&2
    exit 1
}
# NCS disables the babblesim group by default. Resolve revisions through its
# pinned imported manifest on every invocation, including partial/warm caches.
west update --narrow -o=--depth=1 --group-filter +babblesim
python3 "$script_dir/bsim-component-cc.py" --check-sources "$ncs_root/tools/bsim/components"
