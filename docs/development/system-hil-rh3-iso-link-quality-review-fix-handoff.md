# RH3 ISO link-quality sink-only filter fix handoff

Status: one narrow software correction after immutable physical diagnostic
failure. This fixes telemetry eligibility only. It does not diagnose or repair
Mode A ISO delivery loss.

## Physical evidence

Fresh direct row `rh3-20260821-01-iso-link-quality` used reviewed images and
failed only at receiver-tail ISO link-quality collection:

```text
ISO link quality unavailable: -128
invalid ISO link quality: ISO link quality header missing;
ISO link quality expected 2 stream records, got 0;
ISO link quality missing slot(s): [0, 1]
```

The row had two active Mode A CISes, source `sc=12000` and `sf=0` for both,
clean audio/offload/FLPR tail counters, and two raw callback-status summaries:

```text
slot 0: rx_valid=118 rx_error=0 rx_lost=14014 rx_unknown=0 rx_no_ts=50
slot 1: rx_valid=0   rx_error=0 rx_lost=14102 rx_unknown=0 rx_no_ts=14102
```

Evidence root is immutable:

```text
/tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality/
```

`SHA256SUMS` verified all 23 retained payloads. Never treat this row as
acceptance. Do not retry, overwrite, or edit it.

## Root cause

`src/bt_bap.c:bt_bap_iso_link_quality_get_active()` currently rejects a sink
endpoint when `ep_info.can_recv` is false. That predicate is wrong for this
sink-only receiver build.

Installed NCS v3.3.0 `bt_bap_ep_get_info()` initializes `can_recv` false and
sets it only inside `IS_ENABLED(CONFIG_BT_AUDIO_TX)`. Current resolved nRF54L15
app configuration has:

```text
CONFIG_BT_AUDIO_RX=y
# CONFIG_BT_AUDIO_TX is not set
CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT=2
CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT=0
```

Therefore both active sink CISes pass BAP endpoint lookup but are skipped at
`!ep_info.can_recv`; the helper reaches zero snapshots and returns `-ENOTCONN`
without calling `bt_hci_get_conn_handle()` or HCI opcode `0x2075`.

This is evidence-backed, not an HCI-controller inference:

- `bt_bap_ep_get_info()` does not return `-ENOTCONN` in installed NCS;
- `bt_hci_get_conn_handle()` returns `-ENOTCONN` only for an ISO connection not
  in `BT_CONN_CONNECTED` state;
- an SDC unknown/stale CIS handle produces HCI status `0x02`, mapped by Zephyr
  to `-EIO`, not `-ENOTCONN`.

Physical `-128` with two active streams therefore identifies local filter
exclusion. It says nothing new about original ISO loss cause.

## Goal

Allow active sink CIS telemetry on RX-only builds by removing only the invalid
`can_recv` eligibility requirement. Continue to require sink direction and live
public ISO-channel pointers; connection-state and HCI failures remain strict.

## Scope

In scope:

1. `src/bt_bap.c`: remove `!ep_info.can_recv` from active sink CIS eligibility.
2. Add a short local rationale that `can_recv` is gated by `CONFIG_BT_AUDIO_TX`
   in NCS and cannot identify RX-only sink activity.
3. Update active implementation handoff
   `docs/development/system-hil-rh3-iso-link-quality-diagnostic-handoff.md`
   so its API algorithm no longer directs use of `can_recv`.
4. Build both production targets, check resolved build contract, and record new
   receiver image SHA-256 values.

Out of scope:

- any HCI command, response, parser, shell output, runner-tail, threshold,
  warning-scanner, controller, RF, pairing, source, audio, PLC, Mode A, timing,
  I2S, or FLPR change;
- enabling `CONFIG_BT_AUDIO_TX`, changing sink/source ASE counts, or adding
  source-direction behavior;
- physical HIL, flashing, serial, reset, manual Bluetooth/HCI action, matrix
  retry, source build, `STATUS.md`, commit, push, merge, PR, tag, or release;
- edits outside listed files or unrelated dirty worktree content.

Do not add a mock-driven test of private `sinks[]` or HCI internals. The smallest
public regression proof is one later runner-owned direct Mode A row: before this
fix `bt iso quality` returns `-128`; after it, strict runner evidence must retain
two controller snapshots or fail with its exact HCI/response error.

## Exact implementation

In `src/bt_bap.c`, replace this active-sink eligibility condition:

```c
if (ep_info.dir != BT_AUDIO_DIR_SINK || !ep_info.can_recv ||
    ep_info.iso_chan == NULL || ep_info.iso_chan->iso == NULL) {
    continue;
}
```

with the same condition minus `!ep_info.can_recv`:

```c
if (ep_info.dir != BT_AUDIO_DIR_SINK || ep_info.iso_chan == NULL ||
    ep_info.iso_chan->iso == NULL) {
    continue;
}
```

Place concise rationale immediately above or alongside this condition. Do not
change the later `bt_hci_get_conn_handle()` call, `-ENOTCONN` skip behavior,
capacity check, HCI response validation, counter conversion, or shell API.

In the active implementation handoff, revise its step 2 to require only:

```text
ep_info.dir == BT_AUDIO_DIR_SINK, non-NULL ep_info.iso_chan, and non-NULL
ep_info.iso_chan->iso.
```

State that `can_recv` must not be used because NCS gates it on
`CONFIG_BT_AUDIO_TX`, which is intentionally absent in the RX-only sink build.

## Verification

Run sequentially from repository root. Preserve host test build directories and
all existing evidence. Treat any new compiler/Kconfig/CMake warning as failure;
only documented NCS baseline diagnostics may remain.

```bash
nix develop --command fw-build-5340
nix develop --command fw-build-54l15
nix develop --command python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
python3 - <<'PY'
from pathlib import Path

config = Path("build/nrf54l15/le-audio-receiver/zephyr/.config").read_text(
    encoding="utf-8"
)
if "CONFIG_BT_AUDIO_RX=y\n" not in config:
    raise SystemExit("expected CONFIG_BT_AUDIO_RX=y")
if "CONFIG_BT_AUDIO_TX=y\n" in config:
    raise SystemExit("unexpected CONFIG_BT_AUDIO_TX=y")
print("RX-only nRF54L15 audio configuration confirmed")
PY
sha256sum \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex
```

## Executor rules

Implement only this handoff. Preserve unrelated dirty changes and immutable
evidence, including `rh3-20260821-01-iso-link-quality`. Do not use hardware or
run physical HIL. Do not commit. Return changed files, exact verification
results, new image hashes, final `git diff --check`, final `git status --short`,
