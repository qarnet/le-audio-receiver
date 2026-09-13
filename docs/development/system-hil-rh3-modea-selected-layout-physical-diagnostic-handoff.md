# RH3 Mode A selected CIS-layout physical diagnostic handoff

Status: one authorized, runner-owned diagnostic completing current selected-layout
triad. This is not a retry of `rh3-20260822-04-modea-lifecycle-split`, a matrix
run, an acceptance run, a QoS change, or a causal conclusion.

## Goal

Capture two complete selected C-to-P CIS-layout records for fresh Mode A:
two sequentially packed 120-byte, 10 ms CISes. Compare them factually with
current one-CIS controls:

| Row | CIS layout | Selected evidence | Retained loss |
| --- | --- | --- | --- |
| RH3-08 mono | one 120-byte CIS | `NSE=6`, C-P PDU 120, CIG/CIS delay 5304 us | `rx_unreceived=9`, `rx_lost=12` |
| RH3-07 Mode B | one 240-byte CIS | `NSE=6`, C-P PDU 240, CIG/CIS delay 8184 us | `rx_unreceived=13267`, `rx_lost=13591` |
| this Mode A row | two 120-byte CISes | two active-slot selected records | new evidence only |

Mode A and Mode B both transport 240 codec bytes per 10 ms, but differ in CIS
count and SDU layout. Mono and each Mode A CIS use same 120-byte source preset,
but differ in concurrent CIS count. This creates bounded observations only. Do
not infer RF, controller, payload-size, scheduling, source, receiver, audio, or
root cause.

## Grounding

The NCS v3.3.0 `BT_BAP_LC3_UNICAST_PRESET_48_4_1` macro supplies each Mode A
front-left/front-right stream with unframed 10,000 us interval, 120-byte SDU,
RTN 5, 20 ms transport latency, 40,000 us presentation delay, and 2M PHY
preference. The HIL source constructs the group with
`BT_ISO_PACKING_SEQUENTIAL`. `bt_iso_chan_get_info()` reports selected values
after CIS establishment.

RH3-08 and RH3-07 prove controller selected same interval, `NSE=6`, C-P `BN=1`,
2M PHY, and one-event flush timeout for current one-CIS controls. Their CIG/CIS
sync delay differs 5304 us versus 8184 us. Do not assert expected Mode A values:
all positive selected fields and exactly two slot records are required evidence.

Previous Mode A RH3-04 retained severe loss on both streams and a runtime
`bt_conn` warning. A new Mode A row can pass or fail independently. Regardless,
retain its selected-layout evidence if available and never retry this new ID.

Host J-Link fingerprints now use `gdb port disabled`, `tcl port disabled`, and
`telnet port disabled`. RH3-08 physically proved no legacy deprecation output.

## Frozen scope

### In scope

1. Run host-only preflight against existing image outputs and host tooling.
2. Execute exactly one runner-owned `rh3.fresh_mode_a_48_4_1` row.
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
run ID:     rh3-20260822-09-modea-selected-layout
run dir:    /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout.junit.xml
row:        rh3.fresh_mode_a_48_4_1
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
test ! -e /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout.junit.xml

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
  --run-id rh3-20260822-09-modea-selected-layout \
  --junit /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Stop after exit `0`, `1`, or `130`. Never retry. A failed row can still retain
required selected-layout evidence and must be reported exactly as retained.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout &&
  sha256sum --check SHA256SUMS
)
```

Report only retained evidence:

1. direct invocation result, row outcome, first boundary/detail, cleanup,
   four image hashes, and checksum result;
2. exact two-record Mode A ISO output including every selected field and
   link-quality counter, plus each summary's `rx_valid`, `rx_lost`, `rx_error`,
   `rx_no_ts`, decoded, and PLC;
3. source active/final lifecycle and both streams' `seq/sub/sc/sf/cb/out`;
4. active and post-stop FLPR state/counters, audio/performance/handshake faults,
   warnings, assertions, and shell errors;
5. source fingerprint evidence: no legacy `gdb_port`, `tcl_port`, or
   `telnet_port` deprecation line. Retain exact documented page-tail source
   flash erase extensions if present; flag any other tool warning;
6. bounded factual comparison with RH3-07 Mode B and RH3-08 mono selected
   fields/counters. Do not infer cause.

Update both state documents with exact outcome and recomputed physical counts.
State this uses telemetry receiver image `d8e570...`, is not a retry of RH3-04,
and is not acceptance evidence. Preserve historical rows.

## Executor return format

Return preflight results/counts, direct invocation result, one-execution
confirmation, evidence paths/checksum, exact selected-layout/lifecycle/tool-log
facts, documentation changes, final `git status --short`, no-manual-hardware/
no-build/no-commit confirmation, and blockers or deviations.
