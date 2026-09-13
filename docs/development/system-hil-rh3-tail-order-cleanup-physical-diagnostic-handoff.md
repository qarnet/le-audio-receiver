# RH3 tail-order and source-cleanup physical diagnostic handoff

Status: one authorized fresh direct diagnostic row after host-only repair
review. This is not a matrix retry, acceptance run, root-cause conclusion, or
audio-health claim.

## Goal

Run exactly one runner-owned fresh `rh3.fresh_mode_a_48_4_1` row with unchanged
reviewed images and repaired host tail ordering. Capture whether:

1. `bt iso quality` returns two active-CIS records before teardown;
2. normal terminal/final-status cleanup completes without the previous queued
   terminal cleanup race;
3. Mode A high receiver loss remains under unchanged strict policy.

## Grounding

Host repair is accepted:

- `LIVE_TAIL_COMMANDS` now queries `bt iso quality` first;
- `SourceClient._bounded_stop()` drains only already queued source records before
  deciding whether STOP is needed;
- queued PASS-terminal and queued `parse-error/unbound` cleanup paths have
  explicit runner regressions;
- host suite passed `144/144` in `143.746 s`, Python compilation passed, and
  `git diff --check` passed;
- no firmware or image changed during repair.

Immutable predecessor `rh3-20260821-03-modea-depth-fix` failed because link
quality ran after source teardown and returned `-128`; its cleanup also recorded
`parse-error/unbound`. It retained severe loss despite source outstanding target
three. Do not reuse, replace, or reinterpret predecessor evidence.

User authorization covers remaining USB/HIL work. Only runner may own hardware
operations.

## Exact image identities

Use existing outputs only. Do not rebuild. Before any hardware action, all four
SHA-256 values must match exactly:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Matching hashes prove reviewed local images only. They do not prove RH3,
transport, audio, analog, release, or root-cause acceptance.

## Frozen scope

### In scope

1. Recheck destination absence, logical fixture/binding, and exact images.
2. Run one exact runner-owned direct Mode A row.
3. Read retained evidence after runner exit.
4. Record factual result in resume/status documents.

### Out of scope

- full RH3 matrix, second row, child retry, old-ID reuse, source or receiver
  behavior change, parser change, threshold change, warning waiver, or
  acceptance claim;
- source or receiver build, Kconfig, devicetree, controller, RF, power,
  pairing, PLC, audio, timing, I2S, FLPR, source buffer, or image change;
- manual serial, `serial-mcp`, extra reader, manual flash/reset/recovery,
  pairing, Bluetooth control, raw HCI, OpenOCD, probe control, RF, or power
  action;
- deleting, overwriting, moving, repairing, or reusing prior evidence;
- commit, push, merge, PR, tag, release, or `STATUS.md` edit.

Preserve all unrelated dirty worktree content.

## New immutable diagnostic identity

```text
run ID:     rh3-20260822-01-modea-tail-order-cleanup
run dir:    /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup
JUnit path: /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

This ID and both output paths were absent before handoff creation. It is a new
direct row, never a retry of earlier evidence.

## Preflight

Run sequentially from repository root. Stop before hardware action if any
preflight command fails:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup.junit.xml

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

Expected validation JSON:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## One execution

Use terminal-tool outer timeout `1800000` ms. Do not use shell `timeout`. Run
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260822-01-modea-tail-order-cleanup \
  --junit /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner exclusively owns discovery, fixture lock, probe selection, flash, reset,
serial capture, pairing, connection, streaming, cleanup, and evidence
collection. Do not start any other hardware, serial, Bluetooth, RF, source-build,
test, or HIL command while it runs. Do not interrupt except direct user
cancellation.

Stop after this one execution for exit `0`, `1`, or `130`. Never retry.

## Post-run evidence and documentation

If runner created its directory, inspect it read-only after exit:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup &&
  sha256sum --check SHA256SUMS
)
```

Report from `result.json`, `summary.json` if present, `source-records.jsonl`,
`receiver-status.txt`, raw console logs, JUnit, `MANIFEST.md`, and
`commands.jsonl`:

1. runner exit, outcome, first failure boundary/detail, cleanup failures, exact
   image identities, evidence hash result, and runner-only hardware ownership;
2. source `streaming`, `scored_complete`, teardown, terminal, and final status
   records when present, including both streams' `sub`, `sc`, `sf`, `cb`, and
   `out`;
3. receiver `bt iso quality` command order, header, two slots, handles, and all
   seven counters, or exact unavailable/malformed error;
4. receiver callback summaries for slots 0 and 1 and audio/offload/handshake
   evidence;
5. exact warnings, errors, assertions, protocol diagnostics, or cleanup errors
   in timestamp order.

After evidence review, update only these internal documents with factual result:

- `docs/development/system-hil-resume-state.md`;
- `docs/development/system-hil-rh3-software-status.md`.

State no acceptance result. Preserve predecessor evidence and all hash records.
If result fails for any reason, retain exact failure and state no retry. If it
passes, state only that one direct diagnostic row passed, not RH3 acceptance.

## Executor return format

Return files changed, exact command results, runner exit, evidence root,
checksum result, concise diagnostic facts, documentation updates, final
`git status --short`, and confirmation of no manual hardware action, no retry,
an unresolved safety or design conflict.
