# RH3 ISO link-quality parser physical diagnostic handoff

Status: one authorized fresh direct diagnostic after host parser repair. Not a
matrix retry, acceptance run, root-cause conclusion, or audio-health claim.

## Goal

Run exactly one runner-owned fresh `rh3.fresh_mode_a_48_4_1` row to validate
that the repaired parser accepts real interleaved Zephyr logs and retains both
active CIS link-quality records. Let normal lifecycle proceed far enough to
capture stream summaries, final status, and strict raw-log scan when possible.

The prior direct row `rh3-20260822-01-modea-tail-order-cleanup` is immutable
failed evidence. It proved tail ordering and source cleanup, but parser falsely
classified prefixed `Stream[...]` logs as malformed shell records.

## Grounding

Host repair is accepted:

- `parse_iso_link_quality()` now ignores pre-header lines and only evaluates
  unprefixed whitespace-leading `Stream[` candidates after the exact header;
- expected shell grammar remains exact, including two leading spaces and field
  order;
- physical-transcript-shaped parser tests and malformed-candidate tests passed;
- full host HIL suite passed `146/146` in `143.788 s`;
- no firmware, Kconfig, or image changed.

Prior raw evidence showed valid in-tail output with slot 0
`rx_unreceived=14317` and slot 1 `rx_unreceived=14241`, zero CRC errors, and a
transient `bt_conn ... failed to establish. RF noise?` warning. This run must
retain such evidence under unchanged strict policy. Do not waive, threshold, or
attribute it before new evidence exists.

User authorization covers remaining USB/HIL work. Runner is sole hardware owner.

## Exact image identities

Use existing outputs only. Do not rebuild. Before hardware action, require:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Hashes prove reviewed local images only, never RH3 or product acceptance.

## Frozen scope

### In scope

1. Check new output paths, logical binding, and exact images.
2. Run one exact direct Mode A row through runner.
3. Review retained evidence read-only.
4. Update resume/status documents with factual outcome.

### Out of scope

- full matrix, second row, retry, source/receiver/parser/threshold/warning-policy
  change, acceptance claim, or hardware conclusion beyond evidence;
- source or receiver build, firmware/Kconfig/devicetree/controller/RF/power/
  pairing/audio/PLC/I2S/FLPR/image change;
- manual serial, `serial-mcp`, extra reader, manual flash/reset/recovery,
  pairing, Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- deleting, overwriting, moving, repairing, or reusing evidence;
- `STATUS.md`, commit, push, merge, PR, tag, or release.

## New immutable diagnostic identity

```text
run ID:     rh3-20260822-02-modea-iso-parser-fix
run dir:    /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

Both paths were absent before handoff creation. This is a fresh direct row, not
a retry of any earlier ID.

## Preflight

Run sequentially from repository root. Stop before hardware action on any
failure:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix.junit.xml

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

Expected validation:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## One execution

Use terminal-tool outer timeout `1800000` ms. Do not use shell `timeout`. Run
exactly once and preserve direct command return status separately from every
later read-only evidence command:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-02-modea-iso-parser-fix \
  --junit /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner alone owns all discovery, flash, reset, serial capture, pairing,
connection, streaming, cleanup, and evidence. Start no other hardware, serial,
Bluetooth, RF, source-build, test, or HIL command while it runs. Stop after exit
`0`, `1`, or `130`; never retry.

## Post-run review

If output exists, check it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix &&
  sha256sum --check SHA256SUMS
)
```

Report from result/JUnit/manifest, summary if present, source records,
receiver status/raw logs, and commands:

1. direct command exit, result outcome, first failure/detail, cleanup failures,
   exact image identities, and evidence checksum result;
2. ISO parser result: query position, header, exactly two slots/handles, all
   counters, and whether parser failure recurred;
3. source lifecycle and final per-stream counters;
4. receiver slot summaries, audio/offload/handshake state, warning/error/assert
   lines, and high-loss evidence;
5. any reason later strict validation fails after parser acceptance.

Update only `docs/development/system-hil-resume-state.md` and
`docs/development/system-hil-rh3-software-status.md` with facts. State no
acceptance. A direct pass is not RH3 acceptance; a failure is not retried.

No commit, push, merge, PR, tag, or release.
