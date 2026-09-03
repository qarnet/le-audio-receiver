# RH3 current-image mono control diagnostic handoff

Status: one authorized direct control row after immutable Mode A diagnostic
`rh3-20260822-04-modea-lifecycle-split`. This is not a retry, matrix run,
acceptance run, firmware change, threshold change, or root-cause conclusion.

## Goal

Run exactly one runner-owned `rh3.fresh_mono_48_4_1` row with the same current
source and receiver images used by Mode A row
`rh3-20260822-04-modea-lifecycle-split`.

This control distinguishes only whether current one-CIS mono evidence has the
same observed loss/warning symptoms as current two-CIS Mode A evidence:

- mode-specific evidence if current mono is clean while Mode A remains bad;
- non-mode-exclusive evidence if current mono has same symptoms.

Neither outcome establishes RF, controller, firmware, source, receiver, or
audio causality. Preserve all warnings and counters. Do not retry this ID.

## Grounding

RH3-04 retained lifecycle evidence correctly but failed strict log scan:

```text
receiver warning: [00:42:49.737,888] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
Mode A source: both streams sub=12644 sc=12000 sf=0 cb=12644 out=0
Mode A receiver ISO: slot 0 rx_unreceived=14308; slot 1 rx_unreceived=14233
Mode A receiver summaries: slot 0 rx_valid=137 rx_lost=14310;
  slot 1 rx_valid=135 rx_lost=14249
```

The retained receiver CIS durations and per-CIS lost totals agree closely:
stream 0 ran about 144.475 s and `137 + 14310 = 14447`; stream 1 ran about
143.831 s and `135 + 14249 = 14384`. The SDC ISO quality command says its
per-CIS counters start at zero on stream creation and increments
`rx_unreceived` when Link Layer misses a specific payload by its flush point.
This is controller-level missing-payload evidence, not a host parser artifact.

Source `sent` callbacks prove only controller-completion/buffer ownership,
not peer delivery or radio ACK. Stock nRF5340 SW Split has no usable public
source-side ISO link-quality telemetry. A fresh current-image mono control is
therefore the smallest available discriminator before any firmware
instrumentation proposal.

User authorization covers USB/HIL work. Runner is sole hardware owner.

## Frozen scope

### In scope

1. Run host-only verification and safe preflight.
2. Execute exactly one runner-owned fresh mono row.
3. Verify resulting evidence read-only.
4. Update factual state only in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.

### Out of scope

- full RH3 matrix, Mode A retry, another direct row, acceptance claim,
  threshold/warning/validator relaxation, or causal diagnosis;
- source or receiver build, firmware, FLPR, Kconfig, devicetree, controller,
  RF, power, pairing, audio, I2S, PLC, shell, source-tail, or image change;
- manual serial, `serial-mcp`, separate reader, flash/reset/recovery, pairing,
  Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- deleting, overwriting, moving, repairing, or reusing evidence;
- `STATUS.md`, commit, push, merge, PR, tag, or release.

## New immutable diagnostic identity

```text
run ID:     rh3-20260822-05-current-image-mono-control
run dir:    /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control.junit.xml
row:        rh3.fresh_mono_48_4_1
```

Both output paths were absent when this handoff was written. Never reuse this
ID or either path after execution begins.

## Exact image identities

Use existing outputs only. Do not rebuild. Preflight must match:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Hashes prove local inputs only. They are not acceptance evidence.

## Preflight

Run sequentially from repository root. Stop before hardware action on any
failure. `tests/hil/rh2_test.py` previously takes about 144 seconds in this
environment, so invoke its terminal tool with an outer timeout of at least
`1800000` ms. Do not use shell `timeout`:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control.junit.xml

nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/runner.py \
  scripts/hil/receiver.py \
  tests/hil/rh2_test.py \
  tests/hil/capture_runner_test.py

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json

sha256sum --check <<'EOF'
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF

git diff --check
```

Expected fixture validation output:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## One execution

Use outer terminal-tool timeout `1800000` ms. Do not use shell `timeout`.
Run exactly once. Preserve direct invocation result separately from later
read-only commands. Do not start other serial, Bluetooth, RF, HIL,
source-build, or hardware commands while it runs:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-05-current-image-mono-control \
  --junit /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control.junit.xml \
  --row rh3.fresh_mono_48_4_1
```

Runner alone owns discovery, flash, reset, serial capture, pairing,
connection, streaming, cleanup, and evidence. Stop after exit `0`, `1`, or
`130`. Never retry.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control &&
  sha256sum --check SHA256SUMS
)
```

Report only retained evidence:

1. direct invocation result, row outcome, first boundary/detail, cleanup,
   image hashes, and checksum result;
2. active FLPR and post-stop lifecycle state/counters, including settle;
3. tail ISO quality header and sole stream record, then stream summary values
   `rx_valid`, `rx_lost`, `rx_error`, `rx_no_ts`, decoded, and PLC;
4. source active/final lifecycle and sole-stream `seq/sub/sc/sf/cb/out`;
5. audio/performance/handshake faults, warnings, assertions, and shell errors;
6. a bounded comparison with RH3-04: mono symptom evidence either differs or
   matches Mode A. Do not infer a cause or make any acceptance claim.

Update two state documents with exact facts. Recalculate top-level physical-run
counts from retained outcomes: existing count is fourteen live runs, thirteen
failed and one cancelled. Do not call a passing direct mono control acceptance.
Do not alter historical evidence.

## Executor return format

Return:

1. preflight commands/results and exact direct invocation result;
2. confirmation one execution occurred or exact preflight blocker;
3. retained evidence paths and checksum result;
4. all required post-run observations and bounded Mode A comparison;
5. changed documentation files and factual changes;
6. final `git status --short` summary;
7. confirmation no manual hardware action, no build, and no commit;
8. blocker, deviation, or smallest evidence-backed next step.
