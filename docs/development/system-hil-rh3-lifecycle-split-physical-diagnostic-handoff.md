# RH3 lifecycle-split physical diagnostic handoff

Status: one authorized fresh direct diagnostic after accepted host-only
lifecycle-split repair and two review repairs. This is not a retry, matrix
run, acceptance run, firmware change, or root-cause conclusion.

## Goal

Run exactly one runner-owned `rh3.fresh_mode_a_48_4_1` row to test whether the
current host lifecycle split records all of these boundaries correctly:

1. active FLPR offload during scored streaming;
2. live two-CIS ISO quality during source tail;
3. terminal stopped offload/audio/performance/handshake after stream summary;
4. strict active-to-stopped validation without confusing expected stream-stop
   resets with a recovery regression.

Retain high Mode A ISO-loss counters exactly. Do not attribute, waive, mask, or
attempt to fix them in this run. A pass proves only this direct diagnostic row.
A failure is immutable evidence and must not be retried under this ID.

## Grounding

The latest immutable direct row,
`rh3-20260822-03-modea-critical-tail-snapshot`, retained valid live ISO
records but reached normal receiver teardown before its second synchronous
shell command:

```text
result: failed, receiver tail
detail: invalid receiver status: offload state='STOPPED'
slot 0: handle=0x0001 crc_error=2 rx_unreceived=14322 duplicate=0
slot 1: handle=0x0006 crc_error=0 rx_unreceived=14248 duplicate=1
post-teardown offload: STOPPED / epoch=0 gen=3, submit=14377 success=14377
```

Current host code now captures active offload after source `streaming`, keeps
tail collection to `bt iso quality`, then collects terminal diagnostics only
after stream summaries. Its strict recovery comparison normalizes only missing
optional Runtime counters and excludes only `probation_success` from active to
post-stop equality because normal `audio_offload_stream_stop()` resets that
progress field. `probation_cleared`, `busy`, recovery, runtime, heartbeat, and
fault counters remain enforced.

Host-only verification accepted current files:

```text
tests/hil/rh2_test.py: 153 tests, OK
tests/hil/capture_runner_test.py: 19 tests, OK
py_compile: passed
```

User authorization covers USB/HIL work. Runner is sole owner of hardware.

## Frozen scope

### In scope

1. Run current host-only verification and safe preflight.
2. Run one exact direct Mode A row through runner ownership.
3. Verify retained evidence read-only.
4. Update factual state only in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.

### Out of scope

- full RH3 matrix, second direct row, retry, acceptance claim, threshold
  change, warning exception, parser/validator relaxation, or loss attribution;
- source or receiver build, firmware, FLPR, Kconfig, devicetree, controller,
  RF, power, pairing, audio, I2S, PLC, shell, source-tail, or image change;
- manual serial, `serial-mcp`, separate reader, flash/reset/recovery, pairing,
  Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- deleting, overwriting, moving, repairing, or reusing evidence;
- `STATUS.md`, commit, push, merge, PR, tag, or release.

## New immutable diagnostic identity

```text
run ID:     rh3-20260822-04-modea-lifecycle-split
run dir:    /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split.junit.xml
row:        rh3.fresh_mode_a_48_4_1
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

Hashes prove reviewed local inputs only. They are not acceptance evidence.

## Preflight

Run sequentially from repository root. Stop before hardware action on any
failure:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split.junit.xml

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
  --run-id rh3-20260822-04-modea-lifecycle-split \
  --junit /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner alone owns discovery, flash, reset, serial capture, pairing,
connection, streaming, cleanup, and evidence. Stop after exit `0`, `1`, or
`130`. Never retry.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split &&
  sha256sum --check SHA256SUMS
)
```

Report from retained `result.json`, JUnit, manifest, commands, source records,
receiver raw/status logs, and `summary.json` only when present:

1. direct invocation result, result outcome, first boundary/detail, cleanup
   failures, four image hashes, and checksum result;
2. active-offload evidence state, epoch/gen, submit/success, settle result,
   fallback/busy, recovery/probation/runtime/heartbeat/fault counters;
3. tail `receiver-status.txt` command order, ISO header, exactly two active
   stream records, and every quality counter;
4. post-stop command order, stopped offload state, epoch/gen, submit/success,
   audio/performance, handshake, warnings, and shell errors;
5. source lifecycle, receiver stream summaries, active/final status, terminal,
   and persistent Mode A loss evidence;
6. whether lifecycle evidence validates. Do not infer root cause or claim RH3,
   hardware, audio, release, or product acceptance.

Update two listed state documents with concise immutable facts. State direct
invocation status only when independently retained. Do not alter historical
evidence or claim a retry.

## Executor return format

Return:

1. preflight commands/results and exact direct invocation result;
2. confirmation one execution occurred or exact preflight blocker;
3. retained evidence paths and checksum result;
4. all required post-run observations;
5. changed documentation files and factual changes;
6. final `git status --short` summary;
7. confirmation no manual hardware action, no build, and no commit;
8. blocker, deviation, or smallest evidence-backed next step.
