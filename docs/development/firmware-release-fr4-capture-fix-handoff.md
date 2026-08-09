# FR4 procedure fix handoff: fail-closed 7.5 ms capture

Date: 2026-08-09

## Goal

Make the target-specific 7.5 ms receiver-capture example fail closed and retain
both gate and reader output. No code, tests, hardware, or remote state changes.

Base correction: `b066ba6` (`docs: harden FR4 hardware procedure`).

## Exact fix

In `docs/development/firmware-release-fr4-procedure.md`, replace the current
nRF5340 7.5 ms shell example with an exact pattern that:

1. starts `read_acm.py` and records `READER_PID`;
2. waits up to 10 seconds for exact `Opened` evidence in reader log;
3. if no `Opened` evidence appears, terminates/reaps reader and exits nonzero
   before starting PipeWire gate;
4. redirects PipeWire gate stdout+stderr to target-specific
   `$RUN_DIR/logs/nrf5340-7p5-gate.log`;
5. captures gate exit status without skipping reader cleanup;
6. waits/reaps reader and captures reader exit status;
7. requires both statuses equal zero;
8. keeps receiver UART bytes in
   `$RUN_DIR/logs/nrf5340-7p5-receiver.log`;
9. states nRF54L15 uses same fail-closed pattern with `nrf54l15-7p5-*` names
   and its live CDC port.

Do not rely on a bounded loop falling through. Do not lose gate stdout/stderr.
Keep 30 s gate/60 s reader duration and every existing 7.5 ms assertion.

The status-capture form must remain correct when the caller has `set -e`:
initialize each status to zero, run the gate and `wait` as guarded commands
(`... || STATUS=$?`), then evaluate both statuses. A bare command followed by
`STATUS=$?` is insufficient because errexit can terminate before cleanup.

## Scope and verification

Touch exactly:

- `docs/development/firmware-release-fr4-capture-fix-handoff.md`
- `docs/development/firmware-release-fr4-procedure.md`

Run `git diff --check`, inspect status/full diff/log, stage exactly both files,
and commit once:

```text
docs: fail closed on FR4 serial capture
```

Do not amend, push, merge, open a PR, rerun tests/gate, run hardware, download
assets, or touch release/tag state. Return exact recap and clean status. Stop
and escalate on contradictory evidence.
