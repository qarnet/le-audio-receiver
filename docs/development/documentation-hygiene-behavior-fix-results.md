# Documentation-hygiene behavior-fix results

Base commit `c966dc2` (P8 closeout); handoff committed as `9a56a7f`.
Track: fix all confirmed executable defects found during the full-repository
documentation audit. Every finding was validated against current executable
code before editing; every behavior change carries focused public-boundary
regression tests. No production firmware, coverage baseline, BSim pin,
acceptance threshold, or hardware state was altered.

## 1. Coverage output deletion safety

**Old bug:** `scripts/test-coverage.sh` output-containment check was
reversed. `case "$REPO_ROOT" in "$OUTPUT_DIR"/*)` rejects the repo being
*inside* the output dir, not the output dir being *inside* the repo. A repo
child under `/tmp` or `$HOME` (the repo lives under `$HOME`) passed the
allowed-tree pattern and was `rm -rf`'d. `/tmp/../repo`-style traversal and
symlink aliases also bypassed the string-pattern checks.

**New behavior:** the output path is canonicalized (`os.path.realpath`,
symlinks + traversal resolved) before any deletion. Rejected: empty/root/
`$HOME`, relative paths, the repo root, any canonical path inside the repo,
any canonical path containing the repo (ancestor deletion), anything outside
`/tmp` or `$HOME`, and the working directory. The canonical path is used for
all subsequent mkdir/rm. `/tmp/...` output is preserved.

**Tests:** `tests/unit/test_coverage_runner/test_test_coverage_runner.py`
(+`RunnerOutputPathSafety`): repo child rejected before deletion (marker
preserved), traversal alias rejected, symlink alias of repo root and of repo
child rejected before deletion, repo ancestor rejected, external temp output
works. Real-path check also verified: `--output $REPO/child` → "refusing to
clean a path inside the repo root" with marker intact; full `--report-only`
run to `/tmp/opencode/cov-report` completes rc=0 and matches the committed
baseline exactly (36 files, 4665/5121 lines, 2023/2820 branches, 357/357
functions).

## 2. Dongle merge failure

**Old bug:** `scripts/bin/fw-build-dongle` ran `mergehex.py ... || true`
with stderr to `/dev/null`, then unconditionally printed "Merged hexes
written:" — a failed merge left a stale or missing `merged.hex` reported as
fresh.

**New behavior:** the stale `merged.hex` is removed first; a merge failure
exits nonzero with `mergehex.py` stderr preserved at `build/dongle/mergehex.err`
and no `merged.hex` left behind. "Merged hexes written" prints only after a
successful merge.

**Tests:** `tests/unit/fw_build_dongle/test_fw_build_dongle.py` (fake west +
fake mergehex + temp repo): successful merge produces the expected artifact
with the exact argv; merge failure returns nonzero, never reports success,
and removes stale targets; missing netcore hex fails before merge.

## 3. Dongle reset probe selection

**Old bug:** `scripts/bin/fw-reset-dongle` selected the probe from
`scripts/probe-serial.local` or `nrf-probes --find nrf53` — those identify
CMSIS-DAP receiver targets — then passed that serial to a J-Link OpenOCD
invocation.

**New behavior:** matches `fw-flash-dongle`: J-Link auto-detection by
default, or a validated `FW_DONGLE_JLINK_SERIAL` override. The receiver
CMSIS-DAP selectors are never consulted.

**Tests:** `tests/unit/fw_reset_dongle/test_fw_reset_dongle.py`: stale
`probe-serial.local` never lands in the openocd argv and `nrf-probes` is
never invoked; default J-Link argv exact; explicit serial override argv
exact; invalid serial rejected before openocd.

## 4. BlueZ/WirePlumber receiver identity filtering

**Old bug:** any BlueZ device/node satisfied receiver readiness: `bluetoothctl
devices` name substring, any bluez `device.api` object for the device,
any bluez node (MIDI, source) for the sink, MIDI fallback in `_find_sink_name`,
and name-substring only in the wpctl profile check.

**New behavior:** receiver identity is exact and address-bound —
`--receiver-address` (validated `XX:XX:XX:XX:XX:XX`) or the exact device
name (case-insensitive), cross-checked against the D-Bus object `Address`.
PipeWire device and sink discovery filter to the receiver's address
(`bluez5.address` or the `bluez_card./bluez_output.` name form); only
`Audio/Sink` media-class nodes count; `_find_sink_name` returns only the
receiver's Audio/Sink node; the wpctl profile must show the receiver's node.

**Tests:** `scripts/test_bluez_wireplumber_gate.py` (+19): address-filtered
devices, sink-only nodes, MIDI/source/wrong-address never match, exact-name
and address-based find_receiver, wrong address rejected, address validation,
poll success/failures (MIDI-only, unrelated device, wrong address, missing
profile), sink lookup MIDI/wrong-address rejection.

## 5. FLPR hang/stall gates — implemented documented acceptance

**Old bug:** the hang gate's header claimed "Resumed success until end
(success+fallback >= expected ~duration*100)" and "I2S/decode/push faults
zero (from audio status)" but neither check existed (`RE_AUDIO_FAULTS` was
defined and unused); `asrc_fallback_triggered` was computed but not required.
The stall gate sent final status commands without parsing them and never
checked audio fault fields.

**New behavior:** shared `parse_audio_faults` in `flpr_status.py` parses the
real `audio status`/`audio perf` fields (Decode errors, I2S underruns, Stream
resets, Push failures) with last-occurrence semantics — absent or malformed
fields are missing evidence, never zero. Hang gate: `resumed_success_until_end`
requires final success+fallback >= 85% of duration*100 (explicit tolerance
grounded in the 100 fps cadence), `asrc_fallback_triggered` is required, all
four audio fault fields must be present and zero, and the final status batch
(`flpr offload/status/runtime`, `audio status`, `audio perf`) is drained until
quiet and parsed. Stall gate: the buffer is cleared before the final batch,
commands are drained until quiet (bounded), the final offload status is
parsed into `final_status`, and the audio fault fields must be present and
zero.

**Tests:** both suites extended (hang 36, stall 39): positive, each-fault
negative, malformed, missing, partial-output (perf absent), and stale-summary
(mid-stream faulty block discarded by the buffer clear; last-occurrence
semantics in the shared parser).

## 6. Raw-HCI retry/cancellation contract

**Old bug:** every command-status failure was retried forever; at the global
deadline the in-flight attempt was cancelled only when the per-attempt
timeout had elapsed, so a young attempt was left connecting after the socket
closed; a cancelled-at-deadline attempt was always reported as
`cancel_timeout` even when the cancel was acknowledged.

**New behavior:** fatal command-status errors (0x01 unknown command, 0x03
hardware failure, 0x11 unsupported feature/param, 0x12 invalid params, 0x1e
invalid LL params — BT Core Spec Vol 2 Part D 1.3, Zephyr `hci_types.h`
names) end the session with `fatal_status`; all other statuses stay
retryable. At the global deadline `force_cancel()` cancels any in-flight
attempt regardless of the per-attempt timeout, feeds the cancel ack through
the state machine (so an acked cancel is not reported as `cancel_timeout`),
and the socket closes only after the bounded cancel wait.

**Tests:** `tests/unit/hci_raw_connect/test_hci_raw_connect.py` (+10):
each fatal code ends the session, transient statuses keep it alive and retry
succeeds, force-cancel in `in_progress`/`pending_status`, idle/connected
no-op, cancel-ack completes cancellation.

## 7. Raw helper termination guarantee

**Old bug:** `RawHciConnect.terminate()` did SIGTERM + one 3 s wait; on
timeout the exception was swallowed (verbose-only print) and the helper
survived.

**New behavior:** SIGTERM + bounded wait, then SIGKILL + bounded wait; a
process still unreapable after both prints `[error] ... did not exit after
SIGKILL` on stderr regardless of verbose; `[cleanup] Raw-HCI helper
terminated` is printed only when the process was actually reaped; an
already-exited helper (ProcessLookupError) is treated as cleanly reaped.

**Tests:** `tests/unit/bap_central_security/test_bap_central_security.py`
(+5) and the `bap_central_session` cleanup-owner test updated: cooperative
terminate, terminate-timeout→kill reaps, SIGKILL-timeout surfaces failure
without a cleanup claim, already-exited helper clean, silent failure still
surfaces on stderr.

## 8. Official BSim smoke acceptance

**Old bug:** `scripts/bsim-official-smoke.sh` claimed "We check that streaming
completed (100 SDUs sent/received)" but parsed no logs; the nonzero exit from
the known teardown disable-race was called accepted without evidence.

**New behavior:** device logs are captured per-process and parsed by the new
`scripts/bsim_official_smoke_parse.py`: >=100 valid RX SDUs must be proven
(exact `Incoming audio on stream [valid|rx]` marker from `bap_stream_rx.c`).
With progress proven, a nonzero exit is accepted only when the logs carry the
known `ISO receive lost` teardown race (`bap_stream_rx.c:104`); missing,
malformed, or short progress and any unrelated failure exit nonzero with the
reason.

**Tests:** `tests/unit/bsim_official_smoke/test_bsim_official_smoke_parse.py`
(19): parser (progress extraction, teardown marker, empty, malformed, noise),
evaluate (success, teardown-after-sufficient, teardown-too-early, unrelated
failure, missing/malformed progress, exact min-SDU boundary, below-boundary),
and CLI check with real temp log files.

## Prose-only corrections (same implementation commits)

- `bluez-wireplumber-phase3-gate.py`: serial close/reopen description now
  matches the one deliberate reopen after the receiver reset; stale
  `main.c:164-172` advertising refs corrected to `main.c:267/283/294`;
  `_atexit_cleanup`/`cleanup` no longer claim atexit covers SIGKILL or
  "every exit path".
- `bap_central_security.py`: dangling `(pre-split lines 1179-1188)` dropped.
- `fw-flash-54l15`: removed the unused `MERGED_HEX` variable and the
  partition-manager fallback prose — one supported path (two sysbuild hex
  images).
- `test_bluez_wireplumber_gate.py`: the multiple-summary test renamed to
  `test_multiple_summaries_max_counts_last_fault_counters` with a fixture
  where max != last, matching the parser's maxima + last-fault-counter
  semantics.
- `tests/bsim/stage1-scenarios.json`: the `duplicate_release_10ms` note now
  matches the transport-visible behavior the parser pins — the second
  Release PDU is sent and rejected by the ASCS server with INVALID_ASE_STATE
  (`Invalid operation in state: releasing`), no app callback, cleanup exactly
  once per first-time cleanup.

## Verification

- Focused suites (all green): test_coverage_runner 30, fw_build_dongle 5,
  fw_reset_dongle 4, fw_flash_dongle 5, bluez_wireplumber_gate 72,
  bluez_wireplumber_phase3_gate 89, flpr_hang_gate 36, flpr_stall_gate 39,
  hci_raw_connect 42, bap_central_security 54, bap_central_session 31,
  bsim_official_smoke 19, bsim_runner (stage1) 55 PASS / 0 FAIL.
- `python3 -m py_compile` on every changed module; `bash -n` on every changed
  script; `git diff --check` clean. shellcheck not available in this shell.
- Full report-only coverage run rc=0, matching the committed baseline
  exactly (no migration).
- Canonical `./scripts/test-all.sh` after both commits: see below.
- Build contract: 95 assertions, 0 failed (standalone, both before and after
  the changes — it uses self-contained fixtures).
- No fresh production builds needed: no build-helper change affects
  production firmware artifacts (the changed helpers affect the dongle
  build/flash and the coverage runner only).

### Canonical gate

`./scripts/test-all.sh` on the final tree: 62 TOTAL — 59 PASS + 3 FAIL
pre-commit, where the 3 were (a) `python: bap_central_session` — fixed by
the follow-up message update, (b) `coverage` — the standard dirty-worktree
guard (baseline enforcement requires a clean commit), and (c) `matrix` —
cascade of (b). Re-run after committing (clean tree):

```
=== STAGE1 (T4 matrix) PASS — all scenarios strict-checked ===
  PASS: bsim: stage1
```

(T4 stage1 matrix fully re-verified: all 17 scenarios, pinned hashes intact,
including `duplicate_release_10ms` with its byte-identical pins.)

## Deviations

- None from the handoff's required fixes. BSim binaries were present locally,
  so the official smoke script itself was not executed (it needs the full
  compile+run; its parser and CLI are fixture-tested instead — same
  non-hardware boundary the handoff requested).
- `shellcheck` is not installed in this environment; `bash -n` syntax checks
  pass for all changed shell scripts.
