# RH3 critical tail snapshot physical diagnostic handoff

Status: one authorized fresh direct diagnostic after accepted host-only tail
snapshot repair. This is not a matrix retry, acceptance run, firmware change,
or root-cause conclusion.

## Goal

Run exactly one runner-owned `rh3.fresh_mode_a_48_4_1` row to determine whether
the repaired critical-tail sequence retains both live CIS link-quality records
and a live FLPR offload snapshot before normal teardown. Retain all strict
validation, source lifecycle, receiver summary, and warning evidence.

The direct row may pass or fail. A pass proves only this one diagnostic row,
not RH3 matrix, hardware, audio, release, or product acceptance. A failure is
immutable evidence and must not be retried under this ID.

## Grounding

Host repair review accepted these current code facts:

- `scripts/hil/runner.py::LIVE_TAIL_COMMANDS` now orders `bt iso quality`,
  `flpr offload`, `audio status`, `audio perf`, and `flpr status`.
- `Runner._collect_receiver_tail()` invokes existing bounded
  `_settle_receiver_offload()` immediately after first `flpr offload` response.
  Any settle retry is retained before `audio status`.
- Missing offload capture fails closed at receiver-tail.
- `tests/hil/rh2_test.py` passes `146/146`; Python compile check and
  `git diff --check` passed. No image, firmware, Kconfig, or hardware action
  occurred during repair.

The prior immutable direct diagnostic
`rh3-20260822-02-modea-iso-parser-fix` established why this row is needed:
`bt iso quality` succeeded before teardown, but `flpr offload` ran after audio
commands and reported `STOPPED`. It failed only with
`invalid receiver status: offload state='STOPPED'`. It also retained unresolved
high Mode A loss, so this run must preserve rather than explain or waive those
counters.

User authorization covers remaining USB/HIL work. Runner is sole owner of
hardware actions.

## Frozen scope

### In scope

1. Verify output paths, fixture binding, and exact existing image hashes.
2. Run one exact direct Mode A row with runner ownership.
3. Review resulting evidence read-only.
4. Update factual run state only in:
   - `docs/development/system-hil-resume-state.md`;
   - `docs/development/system-hil-rh3-software-status.md`.
5. Correct existing unsupported claims that immutable evidence retains direct
   runner process exit `0` for `rh3-20260822-02-modea-iso-parser-fix`. Retained
   `result.json`, JUnit, and manifest are authoritative; no process exit code
   is retained in that evidence root.

### Out of scope

- full RH3 matrix, second direct row, retry, acceptance claim, threshold
  change, warning exception, parser/validator relaxation, or loss attribution;
- source or receiver build, firmware, FLPR, Kconfig, devicetree, controller,
  RF, power, pairing, audio, I2S, PLC, tail duration, shell, or image change;
- manual serial, `serial-mcp`, separate reader, flash/reset/recovery, pairing,
  Bluetooth/HCI, OpenOCD, probe, RF, or power action;
- deleting, overwriting, moving, repairing, or reusing evidence;
- `STATUS.md`, commit, push, merge, PR, tag, or release.

## New immutable diagnostic identity

```text
run ID:     rh3-20260822-03-modea-critical-tail-snapshot
run dir:    /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

Both output paths were absent before this handoff. Never reuse this ID or
either path after execution begins.

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
test ! -e /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot.junit.xml

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

Expected validation output:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## One execution

Use outer terminal-tool timeout `1800000` ms. Do not use shell `timeout`. Run
exactly once, preserve host command result separately from later read-only
commands, and do not start other serial, Bluetooth, RF, HIL, source-build, or
hardware commands while it runs:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-03-modea-critical-tail-snapshot \
  --junit /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner alone owns discovery, flash, reset, serial capture, pairing,
connection, streaming, cleanup, and evidence. Stop after exit `0`, `1`, or
`130`. Never retry.

## Post-run review and factual documentation

If output exists, verify it read-only:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot &&
  sha256sum --check SHA256SUMS
)
```

Report from retained `result.json`, JUnit, manifest, commands, source records,
receiver status/raw log, and `summary.json` only when present:

1. result outcome, first boundary/detail, cleanup failures, four image hashes,
   and evidence checksum result;
2. `receiver-status.txt` command/evidence order: ISO first, initial offload
   second, any settle retry before audio status; ISO header, exactly two stream
   records, and all counter values;
3. offload state, epoch/gen, submit/success, settle outcome, fallback/busy,
   recovery, probation, and fault counters. State whether active capture
   succeeded or later strict validation failed for a different reason;
4. source lifecycle, active/final status, terminal, and per-stream counters;
5. receiver stream summaries, audio/performance facts, handshake, warnings,
   assertions, shell errors, and persistent high-loss evidence;
6. no causal diagnosis without evidence. Do not say a direct pass is RH3
   acceptance.

Update two state documents with concise facts. Describe direct invocation
process status only if it was independently retained as durable evidence; do
not infer it from later read-only commands. Correct prior unsupported exit-code
claims as listed in scope. State no acceptance and never modify historical
evidence.

## Executor return format

Return:

1. preflight commands/results and exact host invocation result;
2. confirmation that one execution occurred or exact preflight blocker;
3. retained evidence paths and checksum result;
4. all required post-run observations;
5. changed documentation files and factual changes;
6. final `git status --short` summary;
7. confirmation no manual hardware action, no build, and no commit;
8. blocker, deviation, or smallest evidence-backed next step.
