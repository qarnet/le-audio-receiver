# FR4 wpctl node-name fix handoff

Date: 2026-08-11

## Goal

Fix the deterministic FR4 7.5 ms acceptance-tool failure discovered while
testing exact draft `368351363`. `poll_pipewire_objects()` correctly binds
PipeWire card and sink objects to the live receiver address through `pw-dump`,
but its independent profile-exposure check invokes plain `wpctl status` and
searches that output for the underscore-form address. PipeWire/WirePlumber
1.6.6 renders friendly descriptions in plain output; the address-bearing node
name appears with `wpctl status -n`. The current check therefore cannot pass on
the accepted host stack even when the exact receiver sink exists.

## Evidence

Retained run:
`/tmp/opencode/fr4-replacement-v0.1.0-94WQMu/`.

- `logs/7p5-gate-blocker.txt`: two deterministic failures.
- `logs/nrf5340-7p5-gate.log`: `found_device=True`, `found_sink=True`,
  `found_profile=False`; plain output contains `LE Audio Receiver` only.
- Live `wpctl status -n` contained
  `bluez_output.E8_54_F0_E0_D9_42.1`.
- `pw-dump` independently identified exact receiver card and Audio/Sink node.
- Existing unit success fixture already models address-bearing node-name
  output but does not assert the command that requests that form.

## Decided implementation

1. In `scripts/bluez-wireplumber-gate.py`, change only the profile-exposure
   poll command from `wpctl status` to `wpctl status -n` (argv
   `['wpctl', 'status', '-n']`). Keep exact underscore receiver-address
   matching. Do not add friendly-name fallback and do not weaken the existing
   address-bound `pw-dump` device/sink checks.
2. Update comments/evidence label as needed so retained timeout output says
   node-name form was requested; no unrelated refactor.
3. In `scripts/test_bluez_wireplumber_gate.py`, add regression coverage that
   fails before this fix and proves `_run` receives exact
   `['wpctl', 'status', '-n']`. Preserve existing tests proving exact target
   passes and a different receiver address fails.

## Scope

In scope: the two files above plus this handoff.

Out of scope: firmware source/config, release assets/metadata, version/tag,
PipeWire/WirePlumber configuration, friendly-name fallback, receiver criteria,
hardware flashing, and result/acceptance claims.

This host-tool fix changes no released firmware bytes. Exact draft assets
already passed identity/provenance/flash/boot gates and nRF5340 rows 1-4.

## Verification

Run:

```bash
python3 scripts/test_bluez_wireplumber_gate.py
python3 scripts/test_inventory.py --count
git diff --check
```

Expected inventory remains 62. Inspect status/diff/log; stage only this
handoff, gate script, and its test. Commit normal human-authored message
`fix: request PipeWire node names for address matching`. Do not push, amend,
merge, touch release state, or resume hardware in this implementation phase.

Return files changed, behavior proved, exact tests/results, commit hash/message,
clean status, blockers, and deviations.
