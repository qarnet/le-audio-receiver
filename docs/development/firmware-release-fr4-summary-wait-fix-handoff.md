# FR4 receiver-summary wait fix handoff

Date: 2026-08-11

## Goal

Fix the acceptance gate's premature receiver-log parse after stock
PipeWire playback. `pw-play` exits when PCM submission finishes, but the host
stack keeps the BAP stream enabled for about 5.5 seconds before ASCS Disable.
Firmware emits `Stream[n] summary` during Disable. The gate currently parses
the log immediately after `pw-play` returns, so a valid late summary is
deterministically absent from its snapshot.

This fix must expose final receiver evidence, including faults. It does not
make the current exact draft pass: the same teardown window produces a genuine
`i2s_nrfx: Next buffers not supplied on time` error, which remains a mandatory
failure under repository policy.

## Evidence

Retained run:
`/tmp/opencode/fr4-replacement-v0.1.0-94WQMu/`.

- `logs/row5-new-blocker.txt`: summary timing and I2S warning classification.
- `logs/nrf5340-7p5-r3-gate.log`: parse happened with `Summary log seen:
  False` immediately after `pw-play` success.
- `logs/nrf5340-7p5-r3-receiver.log`: summary appeared about 5.5 seconds
  after playback end, immediately after Disable.
- Three runs reproduced one I2S deadline error before Disable. That error must
  remain visible and fatal after this gate fix.

## Decided implementation

1. Add a bounded post-playback wait in `scripts/bluez-wireplumber-gate.py`
   before `parse_receiver_log()`.
2. Wait for a complete `Stream[<index>] summary:` marker in `self.log_path`.
   Check existing content immediately, then poll at a short fixed interval.
   Use `time.monotonic()` and a fixed 15-second ceiling, enough for measured
   5.5-second host teardown while still failing closed.
3. File absence/read errors during polling do not become success. At timeout,
   append explicit evidence and continue into the existing strict parser so
   its normal missing-summary failure remains authoritative.
4. Record whether summary was observed and elapsed wait. Do not disconnect the
   device, alter host teardown, suppress I2S errors, or change parser fault
   criteria.
5. Add focused behavioral tests in
   `scripts/test_bluez_wireplumber_gate.py` for:
   - summary already present: immediate success;
   - summary appears during bounded wait: success;
   - no summary by deadline: false, then existing parse behavior remains able
     to fail closed;
   - `run()` invokes wait after successful playback and before parse.

## Scope

In scope: this handoff, `scripts/bluez-wireplumber-gate.py`, and
`scripts/test_bluez_wireplumber_gate.py`.

Out of scope: firmware, I2S behavior, host disconnect timing, parser warning
criteria, release assets/version/tag, hardware execution, result claims, and
friendly-name matching.

## Verification

```bash
python3 scripts/test_bluez_wireplumber_gate.py
python3 scripts/test_inventory.py --count
git diff --check
```

Expected inventory remains 62. Inspect status/diff/log; stage only three
intended files. Commit `fix: wait for receiver summary before gate parsing`.
Do not amend, push, merge, touch release state, or resume hardware in this
implementation phase. Return files, behavior, tests, commit, clean status,
blockers, and deviations.
