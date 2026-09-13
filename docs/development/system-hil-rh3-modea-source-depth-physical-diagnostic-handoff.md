# RH3 Mode A source-depth physical diagnostic handoff

Status: one fresh direct diagnostic row after the reviewed HIL source target
changed from two to three outstanding SDUs per Mode A stream. This is not a
matrix retry, acceptance run, root-cause conclusion, or audio-health claim.

## Goal

Run exactly one runner-owned `rh3.fresh_mode_a_48_4_1` row with the reviewed
three-per-stream source image. Retain strict normal evidence, receiver ISO
link-quality counters, and source terminal timing so results can distinguish
the former four-buffer source pacing from the full six-buffer source pool.

The runner's warning scanner and receiver validation stay strict. Nonzero
controller counters remain diagnostic evidence only.

## Grounding

The completed software phase is documented in:

- `docs/development/system-hil-rh3-modea-source-depth-fix-handoff.md`
- `docs/development/system-hil-resume-state.md`
- `docs/development/system-hil-rh3-software-status.md`

The source now has a compile-time outstanding target of three per active stream.
The two-stream Mode A path can therefore use all six existing host ISO TX
buffers. The source host and controller buffer assignments remain six. Native
source-app Twister passed `68/68`; `fw-build-hil-source` passed; the change has
no hardware result yet.

Predecessor `rh3-20260821-02-iso-link-quality-fix` used target two and recorded
source terminal success but high receiver `rx_unreceived_packets` on both CISes
with near-zero CRC errors. It is immutable failed diagnostic evidence. Its
strict log scan also recorded a transient failed-CIS warning. Earlier Mode A
evidence had the same approximately 99 percent PLC without that warning, so
this run must not attribute outcome to either cause before evidence exists.

NCS documents `bt_iso_chan_ops.sent` as controller completion that may mean
enqueue, on-air transmission, or flush. Source `cb` counts remain host/controller
credit, not proof of radio delivery.

Existing user authorization covers remaining USB/HIL work. Matrix runner is
sole hardware owner.

## Exact image identities

Use existing outputs only. Do not rebuild. Before any hardware action, all four
SHA-256 values must match exactly:

```text
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Matching identities prove reviewed local images only. They do not prove RH3,
transport, audio, analog, release, or root-cause acceptance.

## Frozen scope

In scope:

1. Verify fresh output paths, logical fixture/binding, and exact images.
2. Run one direct fresh Mode A row through the runner.
3. Inspect retained evidence read-only after runner exit.

Out of scope:

- full RH3 matrix, second row, child retry, old-ID reuse, threshold or warning
  policy change, or acceptance claim;
- source, receiver, runner, fixture, binding, row, parser, controller, RF,
  power, pairing, audio, PLC, timing, I2S, FLPR, or source-build change;
- rebuild, unit/Python/canonical test gate, or concurrent source-build command;
- manual serial, `serial-mcp`, extra reader, manual flash/reset/recovery,
  pairing, Bluetooth control, raw HCI, OpenOCD, probe control, RF, or power
  action;
- deleting, overwriting, moving, repairing, or reusing prior evidence;
- repository edit, commit, push, merge, PR, tag, release, or `STATUS.md` edit.

Preserve intentionally dirty worktree. Only runner-created evidence under
`/tmp/opencode/hil-runs` may appear.

## New immutable diagnostic identity

Use exactly:

```text
run ID:     rh3-20260821-03-modea-depth-fix
run dir:    /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix
JUnit path: /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

This destination did not exist at handoff creation. It is not a retry of any
previous run.

## Preflight

Run sequentially from repository root. Stop before hardware action if any
preflight command fails.

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix.junit.xml

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

Use a terminal-tool outer timeout of `1800000` ms. Do not use shell `timeout`.
Run exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260821-03-modea-depth-fix \
  --junit /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner exclusively owns discovery, probe resolution, flash, reset, serial
capture, fresh pairing, Bluetooth connection, streaming, cleanup, and evidence
collection. Do not start another hardware, serial, Bluetooth, RF, source-build,
test, or HIL command while it runs. Do not interrupt except on direct user
cancellation.

## Post-run evidence extraction

If runner created the run directory, inspect it read-only after exit status
`0`, `1`, or `130`. Verify retained payloads first:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix &&
  sha256sum --check SHA256SUMS
)
```

Report from `result.json`, `summary.json`, `source-records.jsonl`,
`receiver-status.txt`, raw console logs, external JUnit, `MANIFEST.md`, and
`commands.jsonl`:

1. Runner exit, outcome, first failed boundary/detail, cleanup failures, exact
   four image identities, evidence-hash result, and runner hardware ownership.
2. Source active and final records for both streams: `sub`, `sc`, `sf`, `cb`,
   `out`; timestamps of `streaming` and `scored_complete`; derived elapsed time.
3. Receiver callback summaries for slots 0 and 1, including `rx_valid`,
   `rx_error`, `rx_lost`, `rx_unknown`, and `rx_no_ts`.
4. `bt iso quality` header, exactly two slot records, handles, and all seven
   nonnegative counters per record.
5. Exact warning, error, assert, health, or protocol lines in timestamp order.
   A warning remains a strict failure; do not whitelist it.
6. Receiver audio/offload/handshake evidence. State whether source target three
   changed observed source timing or receiver loss. Do not call either outcome
   root cause without evidence.

If ISO link quality is unavailable/malformed or run fails for any reason,
retain exact error and all evidence. Do not alter parser, scanner, source,
controller, or hardware, and do not retry.

## Stop and report

Stop after this one run under all outcomes. Preserve evidence. Report no
acceptance, root-cause, audio, analog, or release claim. Confirm runner was
sole hardware owner and no repository file changed or commit was made.
