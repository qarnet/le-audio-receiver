# FR4 tooling review-fix handoff: safe evidence and flash verification

Date: 2026-08-09

## Goal

Fix five review defects in the committed FR4 procedure/tests before any
hardware execution. Production mono behavior remains unchanged. Do not run
hardware, download assets, or touch remote release state.

Base implementation: `5607db4` (`feat: add FR4 mono acceptance tooling`).

## Required fixes

### 1. Protocol-independent exact tag 404

In `docs/development/firmware-release-fr4-procedure.md`, replace the hardcoded
`grep -q "HTTP/1.1 404"` check. Installed GitHub CLI 2.97.0 emitted:

```text
HTTP/2.0 404 Not Found
```

Keep the private captured response, but parse only the first HTTP status line
with stdlib Python using an anchored expression equivalent to
`^HTTP/\S+\s+(\d{3})\b`. Require exactly one parsed status and exact code 404.
Any missing/malformed status, success, auth/network/rate-limit error, or other
code fails. Do not print response body. Do not use grep/jq over JSON or hardcode
HTTP protocol version.

### 2. Verify both nRF5340 images after write

In the explicit no-recovery nRF5340 flash sequence, add:

- `verify_image $APP_HEX` immediately after app `flash write_image erase` and
  before UICR handling;
- `verify_image $NET_HEX` immediately after net `flash write_image erase` and
  before net UICR handling.

State that both exact extracted images must report verification success. Keep
the no-recovery sequence and all safety boundaries unchanged.

### 3. Deterministic fresh/bonded row ordering

Revise the per-target matrix into exact stateful order:

1. Before each of fresh mono, fresh Mode A, and fresh Mode B, invoke production
   `bt unpair` on the receiver and require target-correct exact success text
   (nRF5340 feature-off legacy output; nRF54L15 feature-on BONDING output).
   Default `bap_central.py` fresh strategy owns central Device1 removal and
   re-pairing. Any reset failure blocks the row.
2. Run preserved-bond Mode B immediately after successful fresh Mode B, with
   no intervening receiver or central bond deletion.
3. Run exact-address 7.5 ms PipeWire gate against that retained valid bond.

Explain why this ordering prevents NORMAL/BONDED_ONLY from rejecting a
supposedly fresh central while still proving persisted-bond reconnect.

### 4. Target-specific 7.5 ms logs with concurrent capture

Replace shared names `wireplumber-receiver.log` and generic metadata output.
For each target use separate directories/files, e.g.
`$RUN_DIR/logs/nrf5340-7p5-receiver.log`,
`$RUN_DIR/metadata/nrf5340-7p5/`, and corresponding nRF54L15 names.

Show exact target-specific pattern:

1. start `scripts/read_acm.py` on live target console before gate;
2. retain PID and wait until reader opened;
3. run `bluez-wireplumber-gate.py --receiver-address <live>` with that exact
   live receiver log and target output directory;
4. wait for reader and preserve both gate and receiver outputs;
5. never overwrite one target's evidence with the other target.

Use placeholders for live port/address but exact shell ownership/order. Keep
current 30 s minimum and 7.5 ms assertions.

### 5. Safe real fd in mono test

In
`tests/unit/bap_central_endpoint/test_bap_central_endpoint.py`, change
`test_mono_clear_acquired_restores_capacity` to allocate a real `os.pipe()`
descriptor, place one owned end in endpoint transport record, call
ClearConfiguration, and prove the endpoint-owned fd is closed exactly once.
Close the other pipe end in `finally`; clean up owned end in `finally` only if
the test failed before production closed it. Never use literal fd 5.

Keep all other mono tests and production files unchanged.

### Release metadata clarification

The GitHub release JSON has no standalone repository-name field. Clarify the
procedure: repository identity is bound by querying the exact endpoint
`repos/qarnet/le-audio-receiver/releases/367572702`; parsed JSON must validate
exact `url` (or exact owner/repo URL prefix), release ID, tag, draft,
prerelease, target, and asset set. Do not demand a nonexistent JSON property.

## Scope

Touch exactly:

- `docs/development/firmware-release-fr4-review-fix-handoff.md`
- `docs/development/firmware-release-fr4-procedure.md`
- `tests/unit/bap_central_endpoint/test_bap_central_endpoint.py`

Do not edit production scripts, firmware, build/release workflow, VERSION,
public docs, plan/status/results, inventory, or coverage baseline.

## Verification and commit

Run:

```bash
python3 tests/unit/bap_central_endpoint/test_bap_central_endpoint.py
python3 tests/unit/bap_central_session/test_bap_central_session.py
python3 -m py_compile scripts/bap_central.py scripts/bap_central_endpoint.py
git diff --check
```

Inspect status, full diff, and recent log. Stage exactly the three files above.
Commit once:

```text
docs: harden FR4 hardware procedure
```

Do not amend `5607db4`, push, merge, open a PR, run full gate again, run
hardware, download assets, or modify remote release/tag state. Return exact
files, fixes, focused results, commit hash/message, blockers, deviations, and
next step. Stop and escalate on contradictory evidence or scope expansion.
