# Phase 4a Retest Handoff — Finish the Phase, Do Not Stop Before Hardware Test

Status: mandatory continuation after commits `6c6ffdf` and `654e6e3`

## Goal

Finish Phase 4a. Correct remaining async-Acquire defects, execute the corrected
hci0 hardware test, capture serial + btmon + logic evidence, fix every ordinary
defect found, and do not return until Phase 4a passes or a genuine external
blocker needs unavailable hardware/credentials/major design input.

“Code ready for retest” is not completion. The retest itself is the task.

## Fix before running

### Async D-Bus callback signature

dbus-python async `reply_handler` receives one positional argument per D-Bus
out value. `MediaTransport1.Acquire()` returns `(UnixFd, read_mtu, write_mtu)`,
not one tuple argument. Current code:

```python
reply_handler=lambda r, ...: _on_acquire_ok(r, ...)
```

is likely to raise `TypeError` on success. Change callback/helper shape to
accept `fd_ufd, read_mtu, write_mtu` separately, then the captured path/channel
allocation defaults.

### All-or-nothing multi-ASE Acquire

- If any required Acquire fails, do not stream a partial Mode A setup.
- Close every fd already acquired, release transports where possible, and exit
  with exact error evidence.
- If Acquire times out, close acquired fds before exit.
- Remove `traceback.print_exc()` from async error handler unless an active
  exception exists; print the D-Bus error name/message directly.

### Hygiene/docs

- Add final newline to `boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf`.
- Update Phase 4a results changed-file table to include `prj.conf` + nRF5340
  board conf WDT move from `654e6e3`.
- Working tree must finish clean.

## Execute — mandatory

1. Validate Python and builds after the callback fix:

   ```bash
   nix flake check --no-build
   nix develop --command python3 -m py_compile scripts/bap_central.py
   fw-build-54l15
   fw-build-5340
   ```

   Scan full output for warnings; fix them.

2. Start serial-mcp on Xiao `/dev/ttyACM0` @ 115200 before reset/flash.
   Reflash latest nRF54L15 if current firmware build/config uncertainty exists.
   Clean boot required.

3. Send through serial-mcp:

   ```text
   audio reset-stats
   audio status
   ```

4. Start hci0 btmon capture. Preserve binary under `/tmp` and decode relevant
   events to text for the results document.

5. Run corrected hci0 mono test for 30 seconds. Do not stop after code changes:

   ```bash
   nix develop --command python3 scripts/bap_central.py \
     --adapter hci0 --duration 30 --freq 1000 \
     2>&1 | tee /tmp/phase4a-retest-bap-mono.log
   ```

6. While active, capture 0.5–1.0 seconds with fx2lafw at 24 MHz. Measure BCK,
   LRCK, ratio, DIN activity, and 3V3.

7. Query post-stream `audio status`. Preserve serial output including Config,
   QoS, Enable, Start, Stream started, decode counters, warnings/errors.

8. If mono passes, run `--stereo` Mode B for 30 seconds and repeat serial/logic
   evidence and stats.

9. If corrected hci0 still fails:
   - inspect exact script error, bluetoothd/DBus state, btmon HCI sequence, and
     receiver serial;
   - fix ordinary script/endpoint/QoS/control-flow bugs and retry;
   - use hci1 only after evidence proves hci0 failure is outside script;
   - do not erase settings, alter persistent state, or hardcode probe serials;
   - stop for user input only if next action requires unavailable hardware,
     credentials, destructive action, or a major design decision.

## Results update

Rewrite `docs/development/phase4a-results.md` with corrected retest evidence.
Do not preserve stale “retest needed” status after running. State each acceptance
criterion PASS/FAIL with exact evidence. Never infer controller incapability from
an application-level error alone.

If Phase 4a passes, mark it accepted and state Phase 4b GRTC+DPPI remains next
and mandatory. If a genuine external blocker remains, mark phase blocked and
identify the exact user decision/resource needed.

## Commit

- Create a new commit; do not amend previous commits.
- Include this handoff, code fixes, results update, and any scoped diagnostics.
- Do not push.
- Return actual hardware evidence, tests, commit hash/message, and blocker only
  if it meets the blocker definition above.
