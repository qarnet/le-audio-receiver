# RH3 Mode B 7.5 ms selected CIS-layout physical diagnostic handoff

Status: one authorized, runner-owned existing-row diagnostic after completed
10 ms selected-layout triad. This is not a retry, a matrix run, an acceptance
run, a source/QoS change, or a causal conclusion.

## Goal

Run existing `rh3.fresh_mode_b_48_3_1` once and retain selected C-to-P CIS
layout beside controller link-quality counters. Compare bounded transport
evidence with 10 ms Mode B:

| Property | RH3-07 Mode B 48_4_1 | This Mode B 48_3_1 |
| --- | --- | --- |
| CIS / ASE shape | one CIS, one stereo ASE | one CIS, one stereo ASE |
| Frame interval | 10 ms | 7.5 ms |
| SDU | 240 bytes | 180 bytes |
| Nominal SDU byte rate | 24,000 B/s | 24,000 B/s |
| RTN preference | 5 | 5 |
| Max transport latency | 20 ms | 15 ms |
| Presentation delay | 40 ms | 40 ms |
| Preferred PHY | 2M | 2M |
| Frame samples | 480 | 360 |

The equal nominal source SDU byte rate makes this smallest existing-row
cadence/selected-layout discriminator. It does not control transport latency,
SDU size, frame duration, or CPUAPP ASRC fallback. It cannot identify root
cause regardless of result.

RH3-07 10 ms Mode B retained `c_max_pdu=240`, `nse=6`, CIG/CIS delay 8184 us,
and severe loss (`rx_unreceived=13267`, `rx_lost=13591`). This run adds only
existing 7.5 ms row evidence.

## Grounding and 7.5 ms behavior

`hil/source/app/src/hil_source_bap.c` already defines 7.5 ms Mode B as one
stereo 180-byte SDU using unframed 7500 us interval, RTN 5, 15 ms latency, and
40,000 us presentation delay. `scripts/hil/rows.py` already defines
`rh3.fresh_mode_b_48_3_1` with 16,000 scored SDUs.

On nRF54L15, 360-frame ASRC input deliberately falls back to CPUAPP because
FLPR accepts 480-frame input only. This profile is already modeled by receiver
validation: active and post-stop FLPR `submit`, `success`, `fallback`, and
`busy` must remain zero while transport/receiver evidence remains strict. Do
not change FLPR, ASRC, or this validation.

Selected ISO fields must be positive and one record must exist. Do not assert
exact selected controller values before execution. `c_flush_1250us` divided by
`iso_interval_1250us` gives selected FT only when evenly divisible; report it
as a derived observation, not a configuration request.

Host J-Link fingerprints use current commands `gdb port disabled`, `tcl port
disabled`, and `telnet port disabled`. RH3-08 and RH3-09 physically proved no
legacy deprecation output.

## Frozen scope

### In scope

1. Run host-only preflight against existing image outputs and host tooling.
2. Execute exactly one runner-owned `rh3.fresh_mode_b_48_3_1` row.
3. Read and checksum retained evidence.
4. Update factual state only in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.

### Out of scope

- another direct row, full RH3 matrix, acceptance claim, or causal diagnosis;
- source or receiver rebuild, firmware/source/Kconfig/devicetree/controller/QoS/
  RF/power/pairing/audio/I2S/PLC/shell/image change;
- manual serial, `serial-mcp`, separate reader, flash/reset/recovery, pairing,
  Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- flash-helper or warning-policy changes, `STATUS.md`, deleting/overwriting/
  repairing evidence, commit, push, merge, PR, tag, or release.

User authorization covers USB/HIL. `scripts/hil-runner.py` remains sole owner
of discovery, flash, reset, serial capture, pairing, connection, streaming,
cleanup, and evidence.

## Immutable diagnostic identity

```text
run ID:     rh3-20260822-10-modeb-7p5-selected-layout
run dir:    /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout.junit.xml
row:        rh3.fresh_mode_b_48_3_1
```

Both output paths were absent when this handoff was written. After execution
starts, never retry this ID or reuse either path.

## Exact input identities

Use existing outputs only. Do not rebuild. Preflight must match:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Hashes prove local inputs only. They are not hardware or acceptance evidence.

## Preflight

Run sequentially from repository root. Stop before hardware action on any
failure. Invoke outer terminal tool with timeout at least `1800000` ms for
`tests/hil/rh2_test.py` and runner execution. Do not use shell `timeout`.

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout.junit.xml

nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/discovery.py \
  scripts/hil/runner.py \
  scripts/hil/receiver.py \
  tests/hil/hil_fakes.py \
  tests/hil/rh2_test.py \
  tests/hil/capture_runner_test.py

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json

sha256sum --check <<'EOF'
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF

git diff --check
```

Expected fixture output:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

Expected host test counts: RH2 `154/154`, capture `19/19`. The Nix dirty-tree
notice is retained environment state, not source/compiler diagnostic. Do not
remove `/tmp/le-audio-receiver-iso-selected-shell54.jpGre5`.

## One execution

Run once after every preflight command passes. Preserve direct invocation result
separately from later read-only review. Do not start another serial, Bluetooth,
RF, HIL, source-build, or hardware command while it runs.

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-10-modeb-7p5-selected-layout \
  --junit /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout.junit.xml \
  --row rh3.fresh_mode_b_48_3_1
```

Stop after exit `0`, `1`, or `130`. Never retry. A failed row can still retain
required selected-layout evidence and must be reported exactly as retained.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout &&
  sha256sum --check SHA256SUMS
)
```

Report only retained evidence:

1. direct invocation result, row outcome, first boundary/detail, cleanup,
   four image hashes, and checksum result;
2. exact Mode B 7.5 ms ISO record with all selected fields and link-quality
   counters; derive FT only if exact integer division is valid;
3. stream summary `rx_valid`, `rx_lost`, `rx_error`, `rx_no_ts`, decoded, PLC;
4. source active/final lifecycle and sole-stream `seq/sub/sc/sf/cb/out`;
5. active/post-stop FLPR values and CPUAPP-ASRC expected idle plane: active and
   post-stop `submit=success=fallback=busy=0`, plus audio/performance/handshake
   faults, warnings, assertions, and shell errors;
6. source fingerprint has no legacy port-name deprecation. Retain exact
   documented page-tail source erase extensions if present; flag other tool
   warnings;
7. bounded factual comparison with RH3-07 Mode B 10 ms, RH3-08 mono, and RH3-09
   Mode A. Do not infer cause.

Update both state documents with exact outcome and recomputed physical counts.
State this uses telemetry receiver image `d8e570...`, is an existing-row
diagnostic rather than a retry, and is not acceptance evidence. Preserve
historical rows.

## Executor return format

Return preflight results/counts, direct invocation result, one-execution
confirmation, evidence paths/checksum, exact selected-layout/lifecycle/tool-log
facts, documentation changes, final `git status --short`, no-manual-hardware/
no-build/no-commit confirmation, and blockers or deviations.
