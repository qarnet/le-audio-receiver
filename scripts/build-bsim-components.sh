#!/usr/bin/env bash
# Build only the Stage 1 PHY and its pinned upstream dependency closure.
set -euo pipefail

if [ "$#" -gt 1 ] || { [ "$#" -eq 1 ] && [ "$1" != "--force" ]; }; then
    echo "Usage: bash scripts/build-bsim-components.sh [--force]" >&2
    exit 2
fi
: "${ZEPHYR_BASE:?ZEPHYR_BASE must point to the Zephyr checkout}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bsim="$(realpath "$ZEPHYR_BASE/../tools/bsim")"
components="$bsim/components"
if [ ! -f "$bsim/Makefile" ] || [ ! -f "$components/common/Makefile" ]; then
    echo "ERROR: missing BabbleSim common Makefile at $components/common/Makefile" >&2
    exit 1
fi
compiler="$(command -v gcc)" || { echo "ERROR: gcc not found" >&2; exit 1; }
compiler="$(realpath "$compiler")"
printf -v cc 'python3 %q %q %q' "$script_dir/bsim-component-cc.py" "$compiler" "$components"
echo "BabbleSim component policy: -Werror; 5 SHA-256-pinned source/diagnostic exceptions (NCS v3.4.1); ext_2G4_phy_v1 closure only"
make_args=()
if [ "${1:-}" = "--force" ]; then
    # -B also forces make's directory recipes and missing-library error
    # targets. -W virtually updates each source in the recursive makes,
    # rebuilding objects/libraries without deleting shared SDK outputs.
    rebuild="make"
    for component in libUtilv1 libPhyComv1 libRandv2 ext_2G4_libPhyComv1 ext_2G4_phy_v1; do
        for source in "$components/$component"/src/*.c; do
            [ -f "$source" ] || { echo "ERROR: no C sources in $component" >&2; exit 1; }
            rebuild+=" -W src/$(basename "$source")"
        done
    done
    make_args+=("MAKE=$rebuild")
fi
BSIM_BUILD_FAIL_ASAP=1 make -C "$bsim" "${make_args[@]}" "CC=$cc" ext_2G4_phy_v1
test -x "$bsim/bin/bs_2G4_phy_v1" || {
    echo "ERROR: BabbleSim PHY binary missing: $bsim/bin/bs_2G4_phy_v1" >&2
    exit 1
}
