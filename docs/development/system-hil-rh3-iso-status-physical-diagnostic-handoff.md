# RH3 Mode A ISO-status physical diagnostic handoff

Status: one fresh diagnostic execution only. This handoff captures receiver
application-visible ISO callback status for the known Mode A delivery-loss
symptom. It is not an RH3 matrix retry, acceptance run, root-cause conclusion,
or audio-health claim.

## Goal

Run exactly one runner-owned fresh `rh3.fresh_mode_a_48_4_1` row using the
reviewed receiver ISO-status diagnostic image. Retain normal runner evidence
plus both receiver stream summaries containing `rx_valid`, `rx_error`,
`rx_lost`, `rx_unknown`, and `rx_no_ts`.

This distinguishes app-delivered valid, error, lost, and unclassified ISO
callbacks. It cannot observe packets for which the controller/host delivers no
callback.

## Grounding

The immutable predecessor is
`rh3-20260820-03.p1.r2.rh3.fresh_mode_a_48_4_1.f92e84021d7c`. Its source
streams each submitted `12348` SDUs. Its receiver raw log recorded:

```text
Stream[0] summary: SDUs=116 decoded=28244 plc=28128 ...
Stream[1] summary: SDUs=0 decoded=0 plc=0 ...
```

The old receiver image had no ISO-status suffix. The row failed at receiver
tail only because `offload submit/success mismatch`; the raw console still
retained both teardown summaries. That proves raw-console extraction remains
available even when strict tail validation stops before the runner's normal
session-summary step.

The reviewed source change records status in `src/bt_bap.c:stream_recv()`
`src/audio_stream_session.[ch]`, and appends the five-field suffix in the
teardown summary. `scripts/hil/receiver.py:parse_stream_summary()` accepts
that exact suffix while retaining legacy parsing.

Software and production-image validation completed on current dirty worktree:

- native `audio_stream_session`: `47` pass, `0` fail;
- `tests/hil/rh2_test.py`: `134` pass;
- `scripts/check-test-matrix.py --repo-root .`: `0 error(s), 0 note(s)`;
- `fw-build-5340` and `fw-build-54l15`: passed;
- build contract: `96 assertions, 0 failed`;
- nRF54L15 DTS messages are existing documented diagnostics. `STATUS.md`
  classifies reserved-memory messages as intentional FLPR overlay effects and
  `rram@165000` as a stock DTS diagnostic. No source/config change is allowed
  here to silence either.

Existing user authorization covers remaining USB/HIL work. Matrix runner is
sole owner of hardware actions.

## Exact image identities

Use existing files only. Do not rebuild. Before runner hardware action, all
four SHA-256 values must match exactly:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
c4b78fcfdf64b2adb204ea05bea4eba2977b353150cdc5e240d1ca54cc78854b  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

The new receiver CPUAPP identity is diagnostic-only. Matching it does not
claim RH3, transport, audio, or release acceptance.

## Frozen scope

In scope:

1. Recheck fresh output destinations.
2. Validate logical fixture and local binding without hardware mutation.
3. Verify four exact image hashes.
4. Run one direct runner-owned fresh Mode A row with a new immutable ID.
5. Read retained evidence only after runner exit and report ISO-status values.

Out of scope:

- any full RH3 matrix, child retry, old-ID reuse, second diagnostic row, or
  acceptance conclusion;
- source, receiver, runner, fixture, binding, row, parser, threshold, policy,
  Kconfig, devicetree, build, RF, power, or pairing-policy change;
- any rebuild, unit/Python test, canonical gate, source-fixture build, or
  concurrent HIL command;
- manual serial, `serial-mcp`, extra serial reader, manual flashing, reset,
  recovery, erase, pairing, Bluetooth control, `btattach`,
  `scripts/bap_central.py`, manual FLPR command, OpenOCD, probe control, RF,
  or power action;
- deleting, overwriting, moving, or repairing previous evidence;
- repository edits, commit, push, merge, PR, tag, release, or `STATUS.md`
  edit.

Preserve intentionally dirty worktree. Build output and repository files must
not change in this execution phase. Runner-created evidence under
`/tmp/opencode/hil-runs` is expected.

## New immutable diagnostic identity

Use exactly:

```text
run ID:     rh3-20260820-04-iso-status
run dir:    /tmp/opencode/hil-runs/rh3-20260820-04-iso-status
JUnit path: /tmp/opencode/hil-runs/rh3-20260820-04-iso-status.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

This is a fresh direct row, not a retry of any child from
`rh3-20260815-*` or `rh3-20260820-01` through `rh3-20260820-03`.

## Preflight

Run sequentially from repository root. Do not start hardware action after any
preflight failure.

1. Require both destinations absent and not symlinks:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260820-04-iso-status && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260820-04-iso-status && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260820-04-iso-status.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260820-04-iso-status.junit.xml
```

2. Validate fixture and local binding. This performs no hardware mutation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected JSON:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

3. Verify exact local images:

```bash
sha256sum --check <<'EOF'
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
c4b78fcfdf64b2adb204ea05bea4eba2977b353150cdc5e240d1ca54cc78854b  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

4. Record whitespace and intentional dirty baseline:

```bash
git diff --check
```

## One execution

Use terminal-tool outer timeout `1800000` ms. Do not use shell `timeout`.
Run exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260820-04-iso-status \
  --junit /tmp/opencode/hil-runs/rh3-20260820-04-iso-status.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner exclusively owns discovery, flash, reset, serial capture, fresh pairing,
Bluetooth connection and streaming, cleanup, and evidence collection. Do not
start any other hardware, serial, Bluetooth, RF, source-build, test, or HIL
command while it runs. Do not interrupt except on direct user cancellation.

## Post-run evidence extraction

Run this read-only inspection after runner exits, whether exit status is `0`,
`1`, or `130`. It does not alter runner verdict. It must find exactly one
summary for each Mode A slot, require all suffix fields, and prove legacy
`SDUs == rx_valid` semantics:

```bash
nix develop --command python3 - <<'PY'
import json
import sys
from pathlib import Path

sys.path.insert(0, "scripts")
from hil.receiver import parse_stream_summary

path = Path("/tmp/opencode/hil-runs/rh3-20260820-04-iso-status/receiver-console.bin")
summaries = parse_stream_summary(path.read_bytes().decode("utf-8", errors="strict"))
if len(summaries) != 2 or {item["slot"] for item in summaries} != {0, 1}:
    raise SystemExit("expected exactly one parseable summary for slots 0 and 1")
for item in summaries:
    if any(item[field] is None for field in (
        "rx_valid", "rx_error", "rx_lost", "rx_unknown", "rx_no_ts"
    )):
        raise SystemExit("ISO-status suffix missing from receiver summary")
    if item["sdus"] != item["rx_valid"]:
        raise SystemExit("SDUs and rx_valid differ for slot %d" % item["slot"])
print(json.dumps(summaries, sort_keys=True))
PY
```

Then inspect and report `result.json`, `images.json`, external JUnit,
`receiver-status.txt`, `receiver-console.bin`, `source-console.bin`,
`commands.jsonl`, `MANIFEST.md`, and `SHA256SUMS`. Confirm `images.json`
contains all four expected hashes.

Interpretation boundary:

- `rx_*` values classify only callbacks delivered to app.
- They do not count HCI packets absent from app callbacks.
- `rx_valid=0`, `rx_lost=0`, or no callback-error count is not a healthy-link
  conclusion.
- Any strict runner warning/fault/tail failure remains failure. Do not change
  scanner policy, counters, or thresholds after evidence capture.

## Stop and report

Stop after this one run. On passed, failed, cancelled, preflight failure, or
missing suffix:

1. Preserve evidence exactly. Do not retry, rebuild, modify hardware, or launch
   another HIL command.
2. Return preflight output, runner exit status, outcome, failure boundary/detail,
   cleanup failures, exact images, parsed JSON summaries, and relevant raw-log
   lines in timestamp order.
3. State whether `SDUs == rx_valid` for each slot and list each status field.
4. State that no acceptance, root-cause, audio, analog, or release claim follows.
5. Confirm runner was sole hardware owner and no repository file changed or
   commit was made.
