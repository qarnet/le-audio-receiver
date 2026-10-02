#!/usr/bin/env bash
# Build the Stage 1 PHY, static dependencies and default dlopen models.
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
echo "BabbleSim component policy: -Werror; 5 SHA-256-pinned source/diagnostic exceptions (NCS v3.4.1); PHY plus NtNcable/Magic runtime closure"
make_args=()
if [ "${1:-}" = "--force" ]; then
    # -B also forces make's directory recipes and missing-library error
    # targets. -W virtually updates each source in the recursive makes,
    # rebuilding objects/libraries without deleting shared SDK outputs.
    rebuild="make"
    for component in libUtilv1 libPhyComv1 libRandv2 ext_2G4_libPhyComv1 ext_2G4_phy_v1 ext_2G4_channel_NtNcable ext_2G4_modem_magic; do
        for source in "$components/$component"/src/*.c; do
            [ -f "$source" ] || { echo "ERROR: no C sources in $component" >&2; exit 1; }
            rebuild+=" -W src/$(basename "$source")"
        done
    done
    make_args+=("MAKE=$rebuild")
fi
BSIM_BUILD_FAIL_ASAP=1 make -C "$bsim" "${make_args[@]}" "CC=$cc" \
    ext_2G4_phy_v1 ext_2G4_channel_NtNcable ext_2G4_modem_magic
test -x "$bsim/bin/bs_2G4_phy_v1" || {
    echo "ERROR: BabbleSim PHY binary missing: $bsim/bin/bs_2G4_phy_v1" >&2
    exit 1
}
python3 "$script_dir/check-bsim-runtime.py" --root "$bsim"
