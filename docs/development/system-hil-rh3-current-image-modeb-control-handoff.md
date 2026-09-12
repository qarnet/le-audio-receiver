# RH3 current-image Mode B control diagnostic handoff

Status: one authorized direct discriminator after immutable current-image
Mode A and mono controls. This is not a retry, matrix run, acceptance run,
firmware change, threshold change, or root-cause conclusion.

## Goal

Run exactly one runner-owned `rh3.fresh_mode_b_48_4_1` row using same current
source and receiver images as controls:

- RH3-04 Mode A: two 120-byte, 10 ms CIS streams, severe loss;
- RH3-05 mono: one 120-byte, 10 ms CIS stream, passed with low loss;
- this row Mode B: one 240-byte, 10 ms CIS stream.

Source configuration makes Mode B one BAP stream with one 240-byte SDU,
`rtn=5`, `latency=20 ms`, and `presentation delay=40 ms`. Mode A has two
sequentially packed BAP streams, each 120-byte SDU at same timing/reliability
parameters. Both carry 240 bytes per 10 ms across stereo channels, but differ
in CIS count and SDU layout.

This control can narrow observed symptom shape only:

- clean Mode B plus clean mono would isolate severe evidence to concurrent
  two-CIS Mode A behavior;
- severe Mode B evidence would show the problem is not exclusive to two-CIS
  Mode A, while still not identifying a cause.

Neither result proves RF, controller, firmware, source, receiver, audio, or
root cause. Preserve all warnings and counters. Do not retry this ID.

## Grounding

RH3-04 retained correct lifecycle evidence but failed log scan on:

```text
[00:42:49.737,888] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
```

It retained controller-level Mode A loss evidence:

```text
ISO:      rx_unreceived=14308 / 14233
summary:  rx_valid=137 / 135, rx_lost=14310 / 14249
source:   both streams sub=12644 sc=12000 sf=0 cb=12644 out=0
```

RH3-05 used identical image hashes and passed strict fresh mono validation:

```text
ISO:      rx_unreceived=9, crc_error=4
summary:  rx_valid=12644, rx_lost=12
source:   sub=12644 sc=12000 sf=0 cb=12644 out=0
warnings: none
```

Source `sent` callbacks prove controller-completion/buffer ownership only, not
air delivery or peer ACK. Stock nRF5340 SW Split exposes no usable public
source-side ISO link-quality telemetry. Mode B is therefore smallest existing
row that changes concurrent-CIS count while keeping total codec payload rate.

User authorization covers USB/HIL work. Runner is sole hardware owner.

## Frozen scope

### In scope

1. Run host-only verification and safe preflight.
2. Execute exactly one runner-owned fresh Mode B row.
3. Verify resulting evidence read-only.
4. Update factual state only in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.

### Out of scope

- full RH3 matrix, Mode A/mono retry, another direct row, acceptance claim,
  threshold/warning/validator relaxation, or causal diagnosis;
- source or receiver build, firmware, FLPR, Kconfig, devicetree, controller,
  RF, power, pairing, audio, I2S, PLC, shell, source-tail, or image change;
- manual serial, `serial-mcp`, separate reader, flash/reset/recovery, pairing,
  Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- deleting, overwriting, moving, repairing, or reusing evidence;
- `STATUS.md`, commit, push, merge, PR, tag, or release.

## New immutable diagnostic identity

```text
run ID:     rh3-20260822-06-current-image-modeb-control
run dir:    /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control.junit.xml
row:        rh3.fresh_mode_b_48_4_1
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
failure. `tests/hil/rh2_test.py` takes about 144 seconds in this environment,
so invoke its terminal tool with outer timeout at least `1800000` ms. Do not
use shell `timeout`:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control.junit.xml

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
  --run-id rh3-20260822-06-current-image-modeb-control \
  --junit /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control.junit.xml \
  --row rh3.fresh_mode_b_48_4_1
```

Runner alone owns discovery, flash, reset, serial capture, pairing,
connection, streaming, cleanup, and evidence. Stop after exit `0`, `1`, or
`130`. Never retry.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control &&
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
6. bounded raw comparison with RH3-04 Mode A and RH3-05 mono. State only
   whether current Mode B evidence is materially similar to either control;
   do not infer a cause or make an acceptance claim.

Update two state documents with exact facts. Existing count is fifteen live
runs: thirteen failed, one cancelled, one passed mono control. Recalculate
counts from retained outcome. Do not call a passing Mode B control acceptance.
Do not alter historical evidence.

## Executor return format

Return:

1. preflight commands/results and exact direct invocation result;
2. confirmation one execution occurred or exact preflight blocker;
3. retained evidence paths and checksum result;
4. all required post-run observations and bounded comparison;
5. changed documentation files and factual changes;
6. final `git status --short` summary;
7. confirmation no manual hardware action, no build, and no commit;
8. blocker, deviation, or smallest evidence-backed next step.
