# R7 results — stream teardown transition owner

Accepted: 2026-08-05 (G3 hardware completed 2026-08-06 with the E83 rows
that a transient dongle/RF degradation had delayed — see the G3 section;
acceptance-correction commit = this document's commit).  Start commit
`dd4c0a8` (R6 docs acceptance; worktree clean); handoff commit
`6534454`; tests commit `ce15992`; implementation commit `400c51d`;
BSim pin commit `276b621`; matrix witness fix `c4b2839`; gate-comment
fix `3473127`; docs acceptance commit `8bc69a2`; final-gate record
`bcecff2`.  No production BAP behavior, BSim hash/count, callback order,
stream behavior, counter, log/observer event, timing update, or ASCS
stack ownership changed; no existing BSim pins changed (one deliberate
new scenario pin added).

## Goal met

One explicit private teardown transition owner now exists in
`src/bt_bap.c`: `teardown_transition(event, slot)` + the private
`teardown_close_path(forced)` primitive.  Every stop/disable/disabled/
release/disconnect/shell-stop composition runs through it; thin
callbacks only translate into events.  Universal order (no lock spans
Bluetooth/decode/offload/I2S):

```
close lifecycle gate + sink push admission under lifecycle_lock
  -> release lock
  -> session RX lease drain (audio_stream_session_rx_close)
  -> sink push drain/stop (audio_sink_stop)
  -> offload stop (audio_offload_stream_stop)
  -> state reset
```

The approved R7 delta is implemented: the normal BT-close path now stops
the sink BEFORE the offload (previously offload-before-sink), for both
normal and forced paths.

- First global close wins: the close primitive's first-edge return
  (`stream_lifecycle_audio_path_close()` / `force_close()`) gates all
  one-time side effects (generation bump, sink stop, offload stop,
  gate-close observer); duplicate close events are no-ops.
- Each app slot releases once independently: `RELEASE(slot)` checks
  `audio_stream_session_configured(slot)` first (duplicate release of an
  already-cleaned slot is an observable no-op — no observer, no close,
  no stats, no stream touch), then runs the global close, then
  `session_release(slot)` + `lifecycle_sink_release(slot)` +
  `cleanup_release(slot)`.  The second Mode A slot cleans while the gate
  is already closed.
- ASCS stream objects untouched: `struct bt_bap_stream` / conn / ep /
  codec_cfg / iso are never cleared by the coordinator (ASCS detach owns
  them, as before).
- Event policy: `CLOSE` (stop/stopped) and `FORCED` (shell) retain
  stats; `DISABLED` resets stats after the exact summary snapshot/log;
  `DISCONNECT` resets stats once and returns the advertising-wake bool
  (the connection callback only handles conn unref / `default_conn`).
  `DISABLE` (lc3_disable) is serialized under `lifecycle_lock`.
- `stream_started` keeps the accepted R1/R6 transactional open: sink-open
  failure rolls the gate back without inventing sink/offload stops; the
  stale-open recheck closes session admission and stops offload (the
  open transaction's own rollback).

## Direct tests before implementation (committed on R6 code)

`tests/unit/lifecycle` 28 → 33 tests: `test_mode_a_release_both_slots_
cleans_occupancy_no_reopen`, `test_release_open_gate_both_slots`,
`test_duplicate_release_same_slot_idempotent`,
`test_force_close_then_release_either_order`,
`test_mixed_close_release_reset_then_reconfigure`.

`tests/unit/audio_stream_session` 29 → 35 tests:
`test_release_twice_no_configured_change`,
`test_independent_per_slot_cleanup`,
`test_admission_stays_closed_after_release_reset_until_rx_open`,
`test_release_then_reset_then_reconfigure_fresh`,
`test_duplicate_rx_close_no_deadlock`,
`test_modea_first_slot_release_then_second_slot_cleanup`.

Both suites pass on the R6 code unchanged (33/33 and 35/35) and on the
final R7 code (same counts).  All assertions are public-boundary
observable behavior; no private-field/helper-call tests.

## BSim

Stage 1 grew from 16 to **17 scenarios** (`duplicate_release_10ms`),
still one gate child (49 TOTAL unchanged).  All existing 16 scenarios
and every existing hash/count are byte-identical; the strict parser
asserts exact R7 teardown counts on the receiver PASS record
(`obs_gate_c == 1` first-close-wins, `obs_rel` exact per-slot cleanup,
`obs_rel_ss` release-driven-edge only, `obs_disc` disconnect cleanup),
and the receiver `scenario_observer_ok` conditions were tightened where
the old PASS fired before the full teardown sequence
(`modea_first_stop`: waits for both slot cleanups, obs_rel==2,
obs_rel_ss==0; `invalid_codec_fields`: waits for obs_rel==2;
`no_free_sink_slot`: obs_rel==3).

`duplicate_release_10ms` proves the duplicate same-slot release at the
honest transport boundary: the client library accepts the second
`bt_bap_stream_release()` locally (mid-teardown, ep not yet detached)
and sends a second Release PDU; the ASCS server (already RELEASING)
rejects it with `INVALID_ASE_STATE` and no application release callback
fires — the cleanup observer count stays exactly one.  The scenario then
reconfigures the released endpoint (slot reuse) and releases again
(cleanup two), then disconnects.  Parser asserts `obs_rel == 2`,
`obs_rel_ss == 1`, `obs_gate_c == 1`, `cfgrsps == 2`, `relrsps == 3`
(first + rejected duplicate + reuse), `sends0 >= 20`, `after == 0`; a
narrow per-scenario allowlist admits exactly the server's expected
`bt_ascs: Invalid operation in state: releasing` line (scenario 17
only — every other warning/error is still a fault, proven by the runner
fixtures).  The deliberate new pin `known.total = 56` comes from two
identical R7 baseline runs (pushes1=48, trans1=8, total1=56 both times).

`tests/unit/bsim_runner` 36 → 55 reports (fixtures updated to the exact
strengthened contract values; new duplicate-release fixture incl. the
over-cleanup rejection and the allowlist exactness checks).

## Matrix / coverage

`tests/test-matrix.json`: new witnesses for the R7 lifecycle/session
transitions; the bt_bap reason now records the 17-scenario T4+R7 matrix
and the exact R7 teardown observer assertions.  `bt_bap.c` remains
integration-only/excluded.  **No coverage baseline migration** — the
coverage child enforced the unchanged population-30 baseline with zero
drift (added tests only improve lifecycle/session ratios).

## G1 (canonical)

`./scripts/test-all.sh` → **49 PASS / 0 FAIL / 49 TOTAL**, exit 0.
Run 1 on clean `3473127` (log `/tmp/opencode/r7-gate2.log`,
2026-08-05 20:34) and the definitive final run on the clean docs
acceptance commit `8bc69a2` (log `/tmp/opencode/r7-gate3.log`, exit 0,
elapsed 18 m 11 s).  Coverage child: population 30, baseline
enforcement 0 error(s) — no migration needed.  Matrix child: 0
error(s), 0 note(s).  BSim Stage 1: all 17 scenarios strict-checked;
existing pins byte-identical; new `duplicate_release_10ms` total=56
pin enforced.  `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all
exit 0 with zero new/actionable compiler warnings (only the documented
NCS v3.3.0 CMake/deprecation diagnostics).  Build contract
**76 assertions, 0 failed, PASSED**.  `git diff --check` clean.

## G3 hardware (2026-08-05)

Planned non-destructive flash only; no mass erase / recovery.  Evidence:
`/tmp/r7-hw/` (`MANIFEST.md` + `SHA256SUMS`; raw logs for boot, every
row, central runs, offload counters, probes).  Central per AGENTS:
hci0 over /dev/ttyACM2 @ 1 000 000 baud H4, addr C0:AA:BB:CC:DD:EE,
settings powered le secure-conn cis-central.  Probes (nrf-probes):
XIAO CMSIS-DAP 8EE9B3FF DPIDR 0x6ba02477 PART 0x00054b15 VARIANT AAC0;
Pico CMSIS-DAP E6635C08CB1F502B DPIDR 0x6ba02477 PART 0x00005340
VARIANT QKAA — identical to the pre-refactor baseline.  Fresh rows used
`bt unpair` (receiver) + `bluetoothctl remove` (central); the bonded row
preserved the bond (`--preserve-bond`, Pair() skipped).  Production
images left flashed on both targets (the R7 builds are the production
images).  No audibility claim.

### nRF54L15 — 3/3 PASS

| Row | Receiver result | Offload |
|-----|-----------------|---------|
| Mode A fresh 120 s | Stream[0] SDUs=11323 decoded=24062 plc=1416; Stream[1] SDUs=11340; **decode_err=0 i2s_underrun=0 stream_reset=0** | submit=12031 success=12031 **fallback=0** busy=0; faults timeout/full/stale/seq/frame/crc/payload=0; verify=0 |
| Mode B fresh 120 s | Stream[0] SDUs=10469 decoded=24050 plc=3112; **all zeros** | submit=12025 success=12025 **fallback=0**; all faults 0 |
| Mode A bonded reconnect 120 s | Stream[0] SDUs=9712 decoded=24078 plc=4655; Stream[1] SDUs=9725; **all zeros** | submit=12039 success=12039 **fallback=0**; all faults 0 |

plc (1416–4655) reflects the session RF environment, inside the T8
accepted range 0–7110; decode_err/i2s_underrun/stream_reset zero on
every row.  The bonded row exercised the R7 DISCONNECT teardown on
hardware (mid-stream ACL disconnect → advertising restart → bonded
reconnect); the fresh rows exercised the R7 DISABLED/RELEASE teardowns
at stream end.

### nRF5340 / E83 — 4/4 PASS (zero decode/i2s/reset faults)

| Row | Central frames | Receiver result | Warnings |
|-----|---------------|-----------------|----------|
| Mode A fresh 120 s | 12000 @ 100.0 fps | Stream[0] SDUs=11090 decoded=22182 plc=2; Stream[1] SDUs=11100; **decode_err=0 i2s_underrun=0 stream_reset=0** | **zero** `ISO seq gap` / `seq discontinuity` / `i2s_nrfx` / `Cannot write` lines |
| Mode B fresh 120 s | 12000 @ 100.0 fps | Stream[0] SDUs=11660 decoded=23320 plc=0; **all zeros** | **zero** warning lines |
| Mode B bonded reconnect 120 s | 12000 @ 100.0 fps (--preserve-bond, Pair() skipped) | Stream[0] SDUs=11665 decoded=23330 plc=0; **all zeros** | **zero** warning lines |
| APLL evidence (60 s bonded Mode B, mid-stream `audio status`) | 6000 @ 100.0 fps | Stream[0] SDUs=5836 decoded=11672 plc=0; **all zeros** | **zero** warning lines; mid-stream: Frames decoded 6262, I2S underruns 0, Stream resets 0, **Drift state ACTIVE, Drift ppm -500**, Resampler identity |

The E83 Mode B counts match the accepted R6 baseline almost exactly
(Mode B fresh 11660/23320 vs R6 11660/23320; bonded 11665/23330 vs R6
11656/23312).  APLL evidence matches the R6 APLL row exactly
(Drift ACTIVE, ppm -500).

### E83 Mode B link degradation — transient, resolved (no firmware change)

Between ~22:30 and ~02:05 the dongle→E83 Mode B link delivered 75–90 %
of the CIS events (a 24 % silent drop at the receiver's controller: zero
ISO seq gaps, zero PLC, i2s_nrfx underruns from the thinned stream).
Differential diagnosis proved it environmental, not an R7 defect:
(1) the R7 change is teardown-only, the streaming path is untouched;
(2) the same R7 image on the Xiao ran clean at 65–89 % delivery (the
Xiao's ASRC absorbs the jitter); (3) Mode A on the E83 stayed 88–92 %
(two-CIS redundancy masks single-sided loss); (4) btmon proved the host
transmitted every ISO frame (3026/3026 ISO Data TX) — the loss was
between the dongle's controller and the E83's radio; (5) ~55 attempts
with every safe recovery (dongle reflash, E83 reflash, E83 OpenOCD
reset, dongle power-cycle, fresh btattach, BlueZ restart, bond cleanup,
20-min idle) showed rare good windows.  The environment recovered after
02:05 and the three remaining rows (Mode B fresh, bonded reconnect Mode
B, APLL) ran clean on consecutive attempts with the standard ritual
(E83 OpenOCD reset + dongle power-cycle + `bt unpair` + `bluetoothctl
remove`).  This matches the R6 session's documented transient dongle/RF
degradation; no firmware change was made and no test criterion was
weakened.

## Deviations / notes

- The handoff's duplicate-release mechanism guess (local `-EINVAL`) was
  corrected during implementation to the verified wire behavior
  (server-side `INVALID_ASE_STATE` rejection of the second PDU); the
  handoff document was updated in the implementation commit and the
  scenario pins the wire behavior.
- A transient dongle/RF degradation delayed the E83 Mode B / bonded /
  APLL G3 rows (~22:30–02:05, 75–90 % CIS delivery despite reflashes and
  every safe recovery); the environment recovered and all rows passed on
  consecutive attempts (see the G3 section).  No firmware change, no
  test criterion weakened.
- No coverage baseline migration was needed (coverage child passed with
  zero drift on population 30).

## Non-scope items untouched

R8–R10; mixed-duration support; stack-ownership changes; codec/protocol
changes; 360-frame FLPR offload; destructive hardware recovery.
