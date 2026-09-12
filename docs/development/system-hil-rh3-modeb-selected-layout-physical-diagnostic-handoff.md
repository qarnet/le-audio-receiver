# RH3 Mode B selected CIS-layout physical diagnostic handoff

Status: completed. One runner-owned selected-layout Mode B diagnostic used the new receiver image and retained immutable evidence. This is not a retry of `rh3-20260822-06-current-image-modeb-control`, a matrix run, an acceptance run, or a causal conclusion.
or a causal conclusion.

## Goal

Capture one complete selected C-to-P CIS-layout record for the established
240-byte, 10 ms Mode B stream. The new receiver CPUAPP image emits eight
selected values beside existing controller link-quality counters:

```text
iso_interval_1250us nse cig_sync_us cis_sync_us c_max_pdu c_phy c_bn c_flush_1250us
```

This is smallest new-image discriminator for current payload-rate question:

| Existing row | CIS shape | Result | Key retained evidence |
| --- | --- | --- | --- |
| RH3-04 Mode A | two 120-byte, 10 ms CISes | failed log scan | severe loss on both CISes |
| RH3-05 mono | one 120-byte, 10 ms CIS | passed control | low loss |
| RH3-06 Mode B | one 240-byte, 10 ms CIS | passed control | severe loss on sole CIS |

Mode B loss proves symptom is not exclusive to concurrent two-CIS Mode A. It
does not prove RF, controller, source, receiver, or payload-size root cause.
This run adds selected controller layout evidence for exact Mode B shape only.

## Grounding and exact data meaning

NCS v3.3.0 `bt_iso_chan_get_info()` copies cached established-CIS data. On this
unicast-server receiver, sink CIS has `BT_ISO_CHAN_TYPE_PERIPHERAL`; its
`unicast.central` values correctly describe central-to-peripheral path.

Field meanings:

- `iso_interval_1250us` and `c_flush_1250us`: counts of 1.25 ms units;
- `cig_sync_us`, `cis_sync_us`: microseconds;
- `c_max_pdu`: selected C-to-P maximum PDU octets, not SDU octets;
- `c_phy`: Zephyr `BT_GAP_LE_PHY_*` encoding: 1M=`1`, 2M=`2`, coded=`4`;
- `nse` and `c_bn`: selected max subevents and burst number.

All eight values are required positive evidence. Existing nonzero
`crc_error`/`rx_unreceived` counters remain diagnostic only. Do not add a
threshold, warning exception, or pass/fail policy from them.

## Frozen scope

### In scope

1. Run host-only preflight against existing image outputs.
2. Execute exactly one runner-owned `rh3.fresh_mode_b_48_4_1` row.
3. Read and checksum retained evidence.
4. Update factual state only in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.

### Out of scope

- another direct row, full RH3 matrix, acceptance claim, or causal diagnosis;
- source or receiver rebuild, firmware-source change, shell/Kconfig/devicetree,
  controller, QoS, RF, power, pairing, audio, I2S, PLC, source-tail, or image
  change;
- manual serial, `serial-mcp`, separate reader, flash/reset/recovery, pairing,
  Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- deleting, overwriting, moving, repairing, or reusing evidence;
- `STATUS.md`, commit, push, merge, PR, tag, or release.

User authorization covers USB/HIL. `scripts/hil-runner.py` remains sole owner
of discovery, flash, reset, serial capture, pairing, connection, streaming,
cleanup, and evidence.

## Immutable diagnostic identity

```text
run ID:     rh3-20260822-07-modeb-selected-layout
run dir:    /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout.junit.xml
row:        rh3.fresh_mode_b_48_4_1
```

Both paths were absent when this handoff was written. After execution starts,
never retry this ID or reuse either path.

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
test ! -e /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout.junit.xml

nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 -m py_compile \
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

Expected fixture validation output:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

Do not remove `/tmp/le-audio-receiver-iso-selected-shell54.jpGre5`.

## One execution

Run once after every preflight command passes. Preserve direct invocation result
separately from later read-only review commands. Do not start another serial,
Bluetooth, RF, HIL, source-build, or hardware command while it runs.

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-07-modeb-selected-layout \
  --junit /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout.junit.xml \
  --row rh3.fresh_mode_b_48_4_1
```

Stop after exit `0`, `1`, or `130`. Never retry. A failed row can still retain
required selected-layout evidence and must be reported exactly as retained.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout &&
  sha256sum --check SHA256SUMS
)
```

Report only retained evidence:

1. direct invocation result, row outcome, first boundary/detail, cleanup,
   four image hashes, and checksum result;
2. exact parsed Mode B ISO record including all eight selected fields and
   existing link-quality counters;
3. stream-summary `rx_valid`, `rx_lost`, `rx_error`, `rx_no_ts`, decoded, PLC;
4. source active/final lifecycle and sole-stream `seq/sub/sc/sf/cb/out`;
5. active and post-stop FLPR state/counters, audio/performance/handshake faults,
   warnings, assertions, and shell errors;
6. bounded factual comparison with RH3-06 counters. State only whether selected
   layout was captured and whether loss evidence remains materially severe;
   do not infer cause.

Update both state documents with exact outcome and recalculate physical counts
from retained evidence. State this run uses telemetry receiver image `d8e570...`
and is not a retry of RH3-06 or acceptance evidence. Preserve historical rows.

## Executor return format

Return:

1. preflight commands/results and direct invocation result;
2. confirmation exactly one runner execution occurred, or exact preflight
   blocker;
3. retained evidence paths and checksum result;
4. required selected-layout and lifecycle observations;
5. changed documentation files and factual changes;
6. final `git status --short` summary;
7. confirmation no manual hardware action, no build, and no commit;
8. blocker, deviation, or smallest evidence-backed next step.
