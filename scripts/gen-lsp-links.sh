#!/usr/bin/env bash
# LSP compile-database link maintainer (idempotent, safe to re-run).
#
# The repo's clangd setup uses per-tree compile_commands.json symlinks so
# clangd's closest-ancestor discovery picks the right database for each
# image (see ~/.config/opencode/rules/clangd-zephyr.md for the full
# background and AGENTS.md "LSP (clangd) setup" for the repo contract).
#
# Five links are owned by the app CMakeLists (file(CREATE_LINK), emitted at
# every configure):
#     <repo>/compile_commands.json                (receiver, nRF54L15 only)
#     src/flpr/compile_commands.json              (FLPR RISC-V image)
#     hil/source/compile_commands.json            (HIL source fixture app)
#     dongle/hci_ipc/compile_commands.json        (dongle net core)
#     tests/bsim{,/client}/compile_commands.json  (BSim receiver + client)
#
# This script covers what CMake cannot:
#   - tests/unit: suites build in ephemeral mktemp dirs during
#     scripts/test-all.sh, so no persistent configure owns a link.  The
#     persistent manual suite build (build/unit-decode/decode) provides a
#     native_sim database; if it is missing, this script builds it.
#   - Repair of any dangling CMake- or script-owned link whose target
#     exists (e.g. after the BSim out-of-tree build dir moved).
#
# BSim links are absolute: the BSim build tree lives in the shared SDK
# bsim_out outside this repo.  A missing bsim_out is reported, not fatal;
# clangd falls back to inference for those trees until the next
# bsim-stage1-run.sh rebuild.
#
# Usage: bash scripts/gen-lsp-links.sh [--check]
#   --check  only report status, never create/repair (exit 1 if any
#            expected link is missing/dangling)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname -- "$SCRIPT_DIR")"
CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

# ZEPHYR_BASE is required to locate bsim_out for the BSim links.
ZEPHYR_BASE="${ZEPHYR_BASE:-$HOME/ncs/v3.3.0/zephyr}"

FIXED=0
CREATED=0

link_status() {
  # $1 = link path, $2 = link target (echoes "ok"|"dangling"|"missing"|"absent")
  local link="$1" target="$2"
  if [ -L "$link" ]; then
    if [ -e "$link" ]; then
      if [ "$(readlink "$link")" = "$target" ]; then
        echo "ok"
      else
        echo "wrong-target"
      fi
    else
      echo "dangling"
    fi
  elif [ -e "$link" ]; then
    echo "not-a-link"
  else
    echo "absent"
  fi
}

fix_link() {
  # $1 = link path, $2 = target.  Creates or repairs when the target exists.
  local link="$1" target="$2" status
  status="$(link_status "$link" "$target")"
  case "$status" in
    ok)
      printf '  ok       %s -> %s\n' "$link" "$target"
      ;;
    absent|dangling|wrong-target)
      if [ ! -e "$target" ]; then
        printf '  SKIP     %s (target missing: %s)\n' "$link" "$target"
        return 0
      fi
      if [ "$CHECK_ONLY" = "1" ]; then
        printf '  BROKEN   %s (expected -> %s)\n' "$link" "$target"
        return 1
      fi
      mkdir -p "$(dirname "$link")"
      ln -sfn "$target" "$link"
      printf '  %s %s -> %s\n' "$([ "$status" = absent ] && echo created || echo repaired)" "$link" "$target"
      if [ "$status" = "absent" ]; then CREATED=$((CREATED+1)); else FIXED=$((FIXED+1)); fi
      ;;
    not-a-link)
      printf '  ERROR    %s exists but is not a symlink (expected -> %s)\n' "$link" "$target" >&2
      return 1
      ;;
  esac
}

# ---------- tests/unit: persistent native_sim database ----------
# All unit suites share the native_sim/native/64 shape; one representative
# persistent build serves the tests/unit tree.  build/unit-decode/decode is
# the canonical persistent suite build; create it when absent.
UNIT_DB_DIR="$REPO_ROOT/build/unit-decode/decode"
if [ ! -f "$UNIT_DB_DIR/compile_commands.json" ]; then
  if [ "$CHECK_ONLY" = "1" ]; then
    printf '  BROKEN   tests/unit database missing (run: west build --no-sysbuild -b native_sim/native -d %s tests/unit/decode)\n' "$UNIT_DB_DIR"
  else
    printf 'Building persistent tests/unit native_sim database (%s)...\n' "$UNIT_DB_DIR"
    if ! command -v west >/dev/null 2>&1; then
      echo "FATAL: west not on PATH; enter the NCS dev shell, nix develop, first" >&2
      exit 1
    fi
    west build --no-sysbuild -b native_sim/native -d "$UNIT_DB_DIR" \
      "$REPO_ROOT/tests/unit/decode" >/dev/null
  fi
fi

# ---------- links ----------
# NOTE: <repo>/compile_commands.json (root) is intentionally NOT managed
# here: it is owned by the root CMakeLists and must be created only by an
# nRF54L15 receiver configure (never by a 5340 build or this script).
BSIM_OUT="${BSIM_OUT_PATH:-$ZEPHYR_BASE/bsim_out}"
BSIM_RCV_DB="$BSIM_OUT/tests/bsim/bs_nrf5340bsim_nrf5340_cpuapp_le_audio_receiver_bsim_prj_conf/bsim/compile_commands.json"
BSIM_CLI_DB="$BSIM_OUT/tests/bsim/client/bs_nrf5340bsim_nrf5340_cpuapp_bsim_client_bsim_prj_conf/client/compile_commands.json"

FAIL=0
fix_link "$REPO_ROOT/tests/unit/compile_commands.json"   "$UNIT_DB_DIR/compile_commands.json" || FAIL=1
fix_link "$REPO_ROOT/tests/bsim/compile_commands.json"      "$BSIM_RCV_DB" || FAIL=1
fix_link "$REPO_ROOT/tests/bsim/client/compile_commands.json" "$BSIM_CLI_DB" || FAIL=1

# ---------- CMake-owned links: report-only ----------
for l in \
  "$REPO_ROOT/compile_commands.json" \
  "$REPO_ROOT/src/flpr/compile_commands.json" \
  "$REPO_ROOT/hil/source/compile_commands.json" \
  "$REPO_ROOT/dongle/hci_ipc/compile_commands.json" \
; do
  if [ -L "$l" ] && [ -e "$l" ]; then
    printf '  ok       %s (CMake-owned)\n' "$l"
  else
    printf '  note     %s absent (created by the next configure of its owning build)\n' "$l"
  fi
done

if [ "$FAIL" = "1" ]; then
  exit 1
fi
if [ "$CHECK_ONLY" = "0" ]; then
  printf 'gen-lsp-links: %d created, %d repaired\n' "$CREATED" "$FIXED"
fi