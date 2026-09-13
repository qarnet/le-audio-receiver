# RH3 ISO link-quality filter-fix physical diagnostic handoff

Status: one fresh direct diagnostic row after sink-only eligibility correction.
This validates telemetry collection only. It is not an RH3 matrix retry,
acceptance run, root-cause conclusion, or audio-health claim.

## Goal

Run exactly one runner-owned `rh3.fresh_mode_a_48_4_1` row with corrected
nRF54L15 ISO link-quality eligibility. Retain one controller link-quality
snapshot for each active Mode A CIS, along with normal strict runner evidence
and both receiver callback-status summaries.

The command remains active-tail-only and read-only. Nonzero controller counters
remain diagnostic evidence. They do not alter existing warning, health, or
acceptance thresholds.

## Grounding

Immutable predecessor `rh3-20260821-01-iso-link-quality` used the first
link-quality image and failed at receiver tail because `bt iso quality` printed:

```text
ISO link quality unavailable: -128
```

Physical evidence proved two active Mode A CISes and raw summaries for slots 0
and 1. Installed NCS source and resolved configuration prove that the helper
incorrectly filtered both endpoints through `ep_info.can_recv`, a field NCS
sets only with `CONFIG_BT_AUDIO_TX`; this receiver is intentionally RX-only.

The correction removes only that gate. Sink direction, non-NULL public ISO
channel/connection, connection-handle lookup, HCI response length/status/handle
validation, strict host parser, and strict runner failure semantics are
unchanged.

Software verification completed after correction:

- `fw-build-5340`: passed;
- `fw-build-54l15`: passed;
- build contract: `96` assertions, `0` failed;
- resolved nRF54L15 configuration confirms `CONFIG_BT_AUDIO_RX=y` and no
  `CONFIG_BT_AUDIO_TX=y`;
- `git diff --check`: passed;
- no unexpected compiler, Kconfig, or CMake diagnostic appeared.

Existing user authorization covers remaining USB/HIL work. Matrix runner is sole
owner of hardware actions.

## Exact image identities

Use existing outputs only. Do not rebuild. Before hardware action, all four
SHA-256 values must match exactly:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Matching identities prove only that this run uses reviewed local images. They do
not prove RH3, transport, audio, analog, release, or root-cause acceptance.

## Frozen scope

In scope:

1. Recheck fresh output destinations.
2. Validate logical fixture and local binding without hardware mutation.
3. Verify exact local images.
4. Run exactly one direct runner-owned fresh Mode A row.
5. Read retained evidence after runner exit.

Out of scope:

- full RH3 matrix, child retry, old-ID reuse, second diagnostic row, threshold
  change, or acceptance conclusion;
- source, receiver, runner, fixture, binding, row, parser, controller, RF,
  power, pairing, audio, PLC, timing, I2S, or FLPR change;
- rebuild, unit/Python test, source build, canonical gate, or concurrent HIL
  command;
- manual serial, `serial-mcp`, extra reader, manual flashing, reset, recovery,
  erase, pairing, Bluetooth control, raw HCI, OpenOCD, probe control, RF, or
  power action;
- deleting, overwriting, moving, or repairing prior evidence;
- repository edit, commit, push, merge, PR, tag, release, or `STATUS.md` edit.

Preserve intentionally dirty worktree. Only runner-created evidence under
`/tmp/opencode/hil-runs` may appear.

## New immutable diagnostic identity

Use exactly:

```text
run ID:     rh3-20260821-02-iso-link-quality-fix
run dir:    /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix
JUnit path: /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

This destination did not exist at handoff creation. It is not a retry of
`rh3-20260821-01-iso-link-quality` or any earlier evidence.

## Preflight

Run sequentially from repository root. Stop before hardware action if any
preflight command fails.

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix.junit.xml

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json

sha256sum --check <<'EOF'
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
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
  --run-id rh3-20260821-02-iso-link-quality-fix \
  --junit /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner exclusively owns discovery, flash, reset, serial capture, fresh pairing,
Bluetooth connection, streaming, cleanup, and evidence collection. Do not start
any other hardware, serial, Bluetooth, RF, source-build, test, or HIL command
while it runs. Do not interrupt except on direct user cancellation.

## Post-run evidence extraction

If runner created the run directory, inspect it read-only after exit status `0`,
`1`, or `130`. Verify all retained payloads first:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260821-02-iso-link-quality-fix &&
  sha256sum --check SHA256SUMS
)
```

Then report, from `result.json` and `summary.json` when present:

- outcome, first failed boundary/detail, cleanup failures, and image identities;
- `summary.receiver_tail[0].receiver.iso_link_quality.header_seen`;
- each ISO record, exactly two records with slots `0` and `1`, each handle and
  all seven nonnegative counters;
- `summary.receiver_streams` slot coverage and raw `receiver-console.bin`
  callback-status summaries for slots `0` and `1`;
- `receiver-status.txt` exact `bt iso quality` transcript and any strict
  warning/fault; source final records and external JUnit;
- `SHA256SUMS` result, `MANIFEST.md`, and `commands.jsonl` hardware ownership.

If link-quality output remains unavailable or malformed, retain exact errno and
all evidence. Do not alter parser, strict failure policy, source, controller, or
hardware, and do not retry.

## Stop and report

Stop after this one run under all outcomes. Preserve evidence. Report preflight,
runner exit, outcome/boundary/detail, cleanup, exact images, evidence hash
verification, both evidence planes, and relevant raw-log lines in timestamp
order. State explicitly that this result makes no acceptance, root-cause, audio,
analog, or release claim. Confirm runner was sole hardware owner and no
repository file changed or commit was made.
