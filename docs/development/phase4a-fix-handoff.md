# Phase 4a Fix Handoff — Correct BlueZ Transport Ordering and Retest hci0

Status: required review fixes after commit `6c6ffdf`

## Why this fix loop exists

Phase 4a is not accepted. Commit `6c6ffdf` fixed Xiao boot noise and reached
ASE Config/QoS/Enable, but no stream started. Review found the test driver
blocks inside BlueZ's `MediaEndpoint1.SetConfiguration()` callback while
calling `MediaTransport1.Acquire()`. That ordering can prevent BlueZ from
completing the BAP configuration callback and creating the CIS, producing the
same `Acquire(): Input/output error` that the results attributed to the central.

The controller-failure conclusion is therefore unsupported until the driver is
fixed and hci0 is retested.

## Goal

Fix the BAP source control flow, correct unsupported conclusions/evidence, keep
the repository clean, and rerun Phase 4a on hci0. Continue until Phase 4a
acceptance criteria pass or a genuinely external hardware/BlueZ blocker remains
after the corrected test.

## Required fixes

### 1. Return from `SetConfiguration()` immediately

Current bug (`scripts/bap_central.py`):

```python
def SetConfiguration(...):
    ...
    self._acquire_transport(...)  # blocking Acquire inside callback
```

BlueZ sends `SetConfiguration` asynchronously and advances its BAP stream only
after the callback reply. Calling blocking `Acquire(timeout=30000)` from inside
that callback creates a protocol/reentrancy deadlock (BlueZ BAP request timeout
is roughly 3 s).

Required control flow:

1. `SetConfiguration()` parses and stores a pending transport record containing
   path, channel allocation, and properties. It sets config-ready state and
   returns immediately. No Acquire, no wait, no nested call back into BlueZ.
2. Main flow services GLib until all expected `SetConfiguration` callbacks have
   arrived (retain the current short second-ASE grace period for Mode A).
3. Main flow starts `MediaTransport1.Acquire()` **outside** the endpoint callback.
4. Prefer asynchronous dbus-python calls (`reply_handler` / `error_handler`) so
   GLib continues servicing endpoint callbacks and transport property changes.
   Blocking Acquire is acceptable only if proven not to starve GLib; async is
   safer and preferred.
5. Wait for all required Acquire replies with a bounded timeout. Convert each
   returned UnixFd using `.take()` where required and populate the final acquired
   transport list only after success.
6. Only enter the LC3 write loop after required fd(s) exist.
7. Cleanup pending/acquired records correctly on `ClearConfiguration`, endpoint
   release, timeout, Ctrl-C, and partial multi-ASE failure.

Do not merely wrap the existing nested call in `GLib.idle_add` and then block
the GLib thread for 30 seconds. Keep the event loop serviceable.

### 2. Preserve hci0 as primary and retest before blaming hardware

- Run hci0 only first.
- Capture complete script stdout/stderr, receiver serial, and btmon text/binary.
- Evidence must show whether BlueZ emits `LE CIS Established`, whether receiver
  logs `Start` / `Stream started`, and whether Acquire succeeds after callback
  return.
- Do not use hci1 unless corrected hci0 flow still fails with evidence that the
  failure is outside the script.
- Do not claim RTL8761BU lacks CIS support without corrected-flow evidence and a
  cited authoritative source. Community anecdotes are not proof.

### 3. Correct hci1 claims

`docs/development/phase4a-results.md` currently says the all-zero public address
was rejected by SDC. Evidence does not prove that:

- a static random address was configured;
- receiver saw a random over-air address (`28:82:0E:EE:A4:97`);
- disconnect reason `0x05` is Authentication Failure, not unacceptable BD_ADDR.

Rewrite this as an unresolved authentication failure. Do not repeat destructive
bond/storage operations. Do not change hci1 address/privacy state during this
fix unless hci0 is definitively eliminated and the handoff's fallback gate is
met.

### 4. Correct process deviations and repository hygiene

- Do not erase ZMS/settings again. The previous erase was destructive and did
  not prove stale bonds caused the failure.
- Never hardcode probe serials. Use `nrf-probes --find nrf54l` in commands and
  report raw identity evidence.
- Delete current `scripts/__pycache__/` and add Python cache patterns to
  `.gitignore` (`__pycache__/`, `*.py[cod]`) so verification cannot dirty the
  tree again.
- Update `docs/development/phase4a-results.md`:
  - remove “R-4.2 confirmed” unless corrected retest proves it;
  - remove “kernel ISO socket creation fails” unless raw evidence proves it;
  - distinguish verified evidence from inference;
  - add corrected-flow commands/log excerpts/results;
  - retain the valid clean-boot and receiver negotiation evidence.
- Update the handoff/result status accurately. Phase 4a stays open until all
  acceptance criteria pass.

## Retest sequence

1. Verify clean baseline and controller identity:

   ```bash
   git status --short
   nrf-probes
   btmgmt -i hci0 info
   ```

2. Rebuild/flash latest nRF54L15 only if source/overlay changed or current image
   is uncertain. Start serial-mcp before reset; boot must remain warning-free.

3. Reset audio counters through serial-mcp:

   ```text
   audio reset-stats
   audio status
   ```

4. Start btmon capture, preserving binary and a text decode in `/tmp`:

   ```bash
   sudo btmon -i hci0 -w /tmp/phase4a-fix-hci0.btsnoop
   ```

5. Run corrected mono test:

   ```bash
   nix develop --command python3 scripts/bap_central.py \
     --adapter hci0 --duration 30 --freq 1000 \
     2>&1 | tee /tmp/phase4a-fix-bap-mono.log
   ```

6. Once streaming begins, capture 0.5–1 s at 24 MHz with sigrok and measure:
   BCK ≈ 3.072 MHz, LRCK ≈ 48 kHz, exact ratio 64, DIN toggling, 3V3 high.

7. Query post-stream `audio status`; frames decoded must climb, decode errors
   remain zero, no steady-state underruns/resets.

8. If mono passes, run `--stereo` Mode B and repeat evidence/stats.

## Validation gates

```bash
nix flake check --no-build
nix develop --command python3 -m py_compile scripts/bap_central.py
fw-build-54l15
fw-build-5340
git diff --check
git status --short
```

Scan complete build/boot output for warnings. Return-code-only checks are not
sufficient.

## Acceptance criteria

Same Phase 4a criteria remain mandatory:

1. Clean nRF54L15 build/flash/boot.
2. BAP transport acquired and LC3 SDUs written for requested duration.
3. Receiver logs Config/QoS/Enable/Start and `Stream started`.
4. Frames decoded climb; decode errors zero.
5. Logic capture proves BCK/LRCK/DIN at correct rates.
6. No steady-state I2S underrun/slab-full/reset storm.
7. Results contain exact corrected-flow evidence and no unsupported conclusions.
8. Working tree clean; no cache/build artifacts tracked or left untracked.

If corrected hci0 still fails for a proven external reason, document exact raw
evidence and stop only if resolving it requires unavailable hardware, credentials,
or a major design decision. Ordinary script/test defects must be fixed, not
reported as phase completion.

## Commit requirements

- Create a **new** commit; do not amend `6c6ffdf`.
- Include this fix handoff, corrected code/docs, `.gitignore`, and scoped results.
- Stage only intended Phase 4a fix-loop files.
- Do not push, merge, force-push, open a PR, or add attribution.
- Return files changed, exact tests, serial/logic evidence, controller outcome,
  new commit hash/message, blockers/deviations, and recommended Phase 4b follow-up.
