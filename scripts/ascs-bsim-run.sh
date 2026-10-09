#!/usr/bin/env bash
# PB-051 complete six-family matrix only; no ASCS_CASE subset execution.
set -euo pipefail
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
OUT="${ASCS_OUTPUT_ROOT:?absolute new external ASCS_OUTPUT_ROOT required}"
if [[ -n "${ASCS_CASE:-}" ]]; then
    printf '%s\n' 'ASCS_CASE unsupported: full 60 mandatory; use historical diagnostics separately' >&2
    exit 2
fi
source "$SCRIPT_DIR/bsim-env.sh"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
eval "$(nrfutil sdk-manager toolchain env --toolchain-bundle-id 8285d8ad56 --as-script sh)"
if ! link_env="$(python3 "$SCRIPT_DIR/bsim_link_env.py")"; then
    printf 'ERROR: cannot resolve native BSim ELF32 link paths\n' >&2
    exit 1
fi
eval "$link_env"
flags=()
for flag in ${NIX_HARDENING_ENABLE:-}; do
    case "$flag" in fortify|fortify3) ;; *) flags+=("$flag");; esac
done
export NIX_HARDENING_ENABLE="${flags[*]}"
exec python3 "$SCRIPT_DIR/ascs_bsim_run.py" --output "$OUT" \
    --expected-inventory-sha256 addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c
