# R5 results — offload transaction decomposition

Accepted: 2026-08-05.  Start commit `0b29b14` (R4 docs acceptance; worktree
clean); handoff commit `f57016e`; tests+implementation commit `c27d88b`;
inventory-truth docs commit `9dd5108`; docs acceptance commit (this
document's commit).  No production C API, counter, transition, errno,
recovery scheduling, probation, RTT/cycle accounting, protocol value, or
output-mutation behavior changed; no BSim pins changed; nRF5340 `-ENOSYS`
stub byte-identical.

## Goal met

`audio_offload_process_asrc()` (nRF54L15 FLPR ASRC submit path) is
decomposed into private static stage helpers in `src/audio_offload.c`
(no physical file split) with:

- one transaction/capture struct (`struct asrc_txn`: caller args +
  captured state/generation/epoch + explicit submit-mutex ownership);
- stage helpers: argument/pre-state capture, pre-check + submit counting,
  submit-lock acquisition (8 ms deadline), post-mutex recheck, ring
  roundtrip, metadata/state validation, optional shadow verification,
  ONE shared fault finalizer, ONE success commit/linearization point,
  caller output copy after commit;
- the `ASRC_LIFECYCLE_CHECK` macro replaced by
  `asrc_lifecycle_ok_locked()` (same accounting) +
  `asrc_recheck_lifecycle()` (pure stale-only recheck for the two
  pre/post-consume boundaries).

## Helper ownership

| Helper | Owns | Behavior preserved |
|--------|------|--------------------|
| `asrc_validate_args` | arg validation | null/zero, exact 480, capacity ≥ 481 → `-EINVAL`; no counters |
| `asrc_precheck` | pre-check under `g_lock` | uninitialized/STOPPED → `-EAGAIN` no counters; capture; submit++ both; non-ACTIVE → fallback++ both, `-EAGAIN` (no `last_error`) |
| `asrc_acquire_submit` | submit mutex | `k_mutex_lock(K_MSEC(8))`; timeout → shared finalizer `-EBUSY` (busy_count via status category; stale if lifecycle changed while blocked); `owns_submit_lock` set once on success |
| `asrc_postmutex_recheck` | post-mutex state | non-ACTIVE → special non-recovery epilogue (fallback++ both, `last_error=-EAGAIN`, no stale, no recovery, release mutex) — kept out of the finalizer because its lifecycle is always "changed" but its epilogue must not stale-count; else re-capture |
| `asrc_ring_roundtrip` | produce/notify/wait/consume | exact order + blocking lifecycle rechecks; FULL→`-ENOSPC`, produce-other→`-EIO`, notify<0→exact errno, wait≠0→`-ETIMEDOUT`, EMPTY→`-ENOENT`, STALE→`-ESTALE`, consume-other→`-EIO`; pre/post-consume rechecks stale-only |
| `asrc_validate_metadata` | metadata/state validation | exact ordering: error transport (status<0, frames=0 → exact status), frame range, status zero, exact flags, seq, ppm echo, reserved, post-state import, step_base; state faults → asrc `state_fault_count` |
| `asrc_shadow_verify` | `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY` shadow | identical cpuapp ASRC re-run; any mismatch → shared finalizer `-EFAULT` + `verify_fault_count` |
| `asrc_fault_finalize` | ONE shared epilogue | lifecycle mismatch → stale++/fallback++/asrc fallback++/`-ESTALE`, state preserved, no recovery; else asrc fallback++ (+asrc category), `record_fault` ONCE, `recovery_try_schedule_unlock` ONCE; unlocks submit mutex exactly once when owned; always returns `-EAGAIN` |
| `asrc_commit` | success linearization point | final lifecycle guard (stale epilogue identical to finalizer's mismatch branch), success++ both, `last_error=0`, ASRC RTT/cycles, probation (100-success clear), `record_latency` |
| `asrc_copy_output` | caller copy | output/result copied only after commit; explicit field assignment |

## Return / counter invariant table (pinned by tests before extraction)

| Branch | ret | submit (both) | fallback (both) | extra | last_error | state | recovery |
|--------|-----|---------------|-----------------|-------|------------|-------|----------|
| invalid args | `-EINVAL` | 0 | 0 | — | unchanged | unchanged | no |
| uninitialized / STOPPED | `-EAGAIN` | 0 | 0 | — | unchanged | unchanged | no |
| pre-mutex non-ACTIVE | `-EAGAIN` | +1 | +1 | — | unchanged | unchanged | no |
| mutex timeout (busy) | `-EAGAIN` | +1 | +1 | busy +1 | `-EBUSY` | RECOVERING | one schedule |
| mutex timeout, lifecycle changed | `-EAGAIN` | +1 | +1 | stale +1 | `-ESTALE` | preserved | no |
| post-mutex non-ACTIVE | `-EAGAIN` | +1 | +1 | — | `-EAGAIN` | preserved | no |
| produce FULL | `-EAGAIN` | +1 | +1 | full +1 (both) | `-ENOSPC` | RECOVERING | one schedule |
| produce other | `-EAGAIN` | +1 | +1 | — | `-EIO` | RECOVERING | one schedule |
| notify < 0 | `-EAGAIN` | +1 | +1 | — | exact errno | RECOVERING | one schedule |
| wait ≠ 0 | `-EAGAIN` | +1 | +1 | timeout +1 (both) | `-ETIMEDOUT` | RECOVERING | one schedule |
| consume EMPTY | `-EAGAIN` | +1 | +1 | — | `-ENOENT` | RECOVERING | one schedule |
| consume STALE | `-EAGAIN` | +1 | +1 | stale +1 (both) | `-ESTALE` | RECOVERING | one schedule |
| consume other | `-EAGAIN` | +1 | +1 | — | `-EIO` | RECOVERING | one schedule |
| error transport (status<0, frames=0) | `-EAGAIN` | +1 | +1 | — | exact status | RECOVERING | one schedule |
| frame range | `-EAGAIN` | +1 | +1 | frame +1 (both) | `-EFAULT` | RECOVERING | one schedule |
| status ≠ 0 | `-EAGAIN` | +1 | +1 | — | `-EFAULT` | RECOVERING | one schedule |
| flags wrong | `-EAGAIN` | +1 | +1 | — | `-EFAULT` | RECOVERING | one schedule |
| seq mismatch | `-EAGAIN` | +1 | +1 | seq +1 (both) | `-EFAULT` | RECOVERING | one schedule |
| ppm echo mismatch | `-EAGAIN` | +1 | +1 | — | `-EFAULT` | RECOVERING | one schedule |
| reserved nonzero | `-EAGAIN` | +1 | +1 | asrc state +1 | `-EFAULT` | RECOVERING | one schedule |
| post-state import fail | `-EAGAIN` | +1 | +1 | asrc state +1 | `-EFAULT` | RECOVERING | one schedule |
| step_base mismatch | `-EAGAIN` | +1 | +1 | asrc state +1 | `-EFAULT` | RECOVERING | one schedule |
| shadow mismatch (verify) | `-EAGAIN` | +1 | +1 | asrc verify +1 | `-EFAULT` | RECOVERING | one schedule |
| commit stale | `-EAGAIN` | +1 | +1 | stale +1 | `-ESTALE` | preserved | no |
| success | `0` | +1 | 0 | success +1 (both), RTT/cycles/probation | `0` | unchanged | no |

Faults leave caller `output`/`result` byte-identical (asserted with
sentinels in every fault test).  One submit mutex unlock per acquired
path (`owns_submit_lock`), including the shared finalizer and the special
non-recovery branches.

## Tests added (before extraction, all green against the pre-R5 body first)

- `test_asrc_busy` (replaces placeholder): honest second-thread submit
  mutex contention, 8 ms deadline expiry → `-EAGAIN`, submit/fallback +1
  both, busy +1, `-EBUSY` + seq, RECOVERING/healthy false, one recovery
  schedule (via new CONFIG_ZTEST-scoped
  `audio_offload_test_is_recovery_scheduled()` accessor), output
  untouched; one recovery run → ACTIVE, attempts +1, flag cleared.
- `test_asrc_post_mutex_not_active`, `test_asrc_mutex_timeout_lifecycle_changed`,
  `test_asrc_commit_stale_race`: deterministic CONFIG_ZTEST stage hooks
  (`ASRC_TEST_STAGE_MUTEX_ACQUIRED` / `MUTEX_TIMEOUT` / `BEFORE_COMMIT`,
  GCOVR-excluded) pin the post-mutex special epilogue, the
  stale-during-mutex-timeout path (stale +1, busy NOT counted, no
  recovery), and the final commit stale race (stale +1, success NOT
  counted, output/result untouched, no recovery).
- `test_asrc_fault_table`: 16 recovery-eligible fault rows with exact
  category deltas, `last_error`/`last_error_seq`, RECOVERING/healthy,
  one recovery schedule, output/result untouched.
- `test_asrc_rtt_stats`: RTT min/max/sum/count deltas across varied
  successes.
- `verify_asrc_category` state-fault assertions on the reserved /
  post-state-import / step_base paths.
- `offload_asrc`: `test_produce_other`, `test_consume_other`,
  `test_processing_status_positive` (ret `-EAGAIN`, output/result
  untouched).

## Verify-enabled suite — `tests/unit/offload_asrc_verify` (new exec-only)

Compiles production `src/audio_offload.c` + `src/audio_asrc.c` +
`src/flpr_ring.c` with `CONFIG_SOC_NRF54L15=1`,
`CONFIG_AUDIO_OFFLOAD_ASRC=1`, `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=1`.
The mock computes the EXACT real CPU ASRC output + exported post-state
(identical computation to the shadow), so the exact-match path succeeds
bit-for-bit; corruption knobs inject controlled damage.  12 tests:

- exact match (output compared sample-for-sample against a reference
  `audio_asrc_process` run; `verify_fault_count` 0; success +1);
- sample mismatch, frame-count mismatch (in 1..481), post-state phase /
  prev_l / prev_valid mismatch → `verify_fault_count` +1, `-EFAULT`,
  RECOVERING, one shared-finalizer schedule, output/result untouched;
- sequence corruption → `seq_fault_count` +1 at the metadata boundary
  (not a verify fault);
- out-of-range ppm (echo passes, shadow `audio_asrc_process` fails) →
  verify fault;
- post-state import failure (step_base 0), reserved corruption,
  step_base mismatch → `state_fault_count` +1 at the metadata boundary
  (the reachable cpuapp/post-state import failure before shadow);
- stats across 10 verify-enabled successes: zero faults, RTT/cycles
  tracked.

Shadow pre-state import failure branch: preserved in code exactly, but
structurally unreachable in the unit tests because metadata validation
runs first and requires `cr.post_state.step_base == pre_state->step_base`
with a non-zero `step_base`, and cpuapp exports a valid pre_state —
documented coverage exclusion, never fabricated.  CRC validation is
transport-owned (inside the real `flpr_ring_mgr_consume_asrc_result()`)
and never reaches `process_asrc`; documented in the suite header.

## Coverage

No physical split, no baseline rewrite.  Population stays **29**;
tool versions unchanged (gcovr 8.4 / gcov (GCC) 14.3.0).  The
verify-enabled suite pulls the shadow code into merged
`src/audio_offload.c` totals; the decomposition removes the duplicated
fault epilogues.  All per-file and aggregate ratios improved over the
committed baseline, so per the migration rule and R5 handoff policy no
`--write-baseline` migration was performed:

| File | metric | baseline (committed) | current |
|------|--------|----------------------|---------|
| `audio_offload.c` | lines | 583/707 (82.5%) | **581/633 (91.8%)** |
| | branches | 223/375 (59.5%) | **236/353 (66.9%)** |
| | functions | 17/17 | **29/29** |
| aggregate (29 files) | lines | 3505/3946 (88.8%) | **3503/3872 (90.5%)** |
| | branches | 1467/2067 (71.0%) | **1480/2045 (72.4%)** |
| | functions | 209/209 | **221/221** |

All 29 `audio_offload.c` functions execute (zero-hit check clean); the
12 new static helpers are each exercised by the three direct suites.

## G1 results (clean `9dd5108`)

- `./scripts/test-all.sh` — **48 PASS / 0 FAIL / 48 TOTAL** (28 twister +
  5 exec-only + 12 Python + coverage + matrix + BSim), elapsed **16 m
  33 s** (bash `time` builtin), exit 0, log `/tmp/r5-gate2.log`
  (transient).  Coverage child: numeric population **29**, baseline
  enforcement **0 error(s), PASS**; matrix child: **0 error(s),
  0 note(s)**; BSim Stage 1 **PASS — all scenarios strict-checked**
  (pins unchanged: mono 10 ms `0x22AB5C0D`, modea/modeb 10 ms
  `0xBAE24F7E`, 7.5 ms `0x01A3EB05`/`0x2D95D15C`/`0xFF82CADB`, reconnect
  = fresh mono oracle).
- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — all PASS (exit 0),
  **zero compiler warnings** in all three logs.  CMake diagnostics are the
  documented pre-existing NCS set (PARTITION_MANAGER deprecation,
  `__ASSERT()` informational, experimental SW Split notices,
  `CONN_ISO_LOW_LATENCY_POLICY`, nRF54L15 `simple_bus_reg`, watchdog
  "No SOURCES given", FLPR UART_CONSOLE assigned-value) — zero
  new/actionable.
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` — **76 assertions, 0 failed, BUILD CONTRACT
  PASSED**.
- `git diff --check` clean; worktree clean.

## Hardware (nRF54L15; verify-enabled row then production restore)

Planned non-destructive flash only; no mass erase / recovery.  Evidence:
`/tmp/r5-hw/` (`MANIFEST.md` + `SHA256SUMS`; raw logs for boot, both
streams, both hang gates, flashes).  Probe resolved at flash time via
`nrf-probes`: **XIAO CMSIS-DAP serial `8EE9B3FF`, DPIDR `0x6ba02477`,
PART `0x00054b15`, VARIANT `AAC0`** (matches R4 evidence).  Central per
the AGENTS central-only rule: hci0 over `/dev/ttyACM2` @ 1 000 000 baud
H4, `btmgmt info` verified **addr C0:AA:BB:CC:DD:EE** and **current
settings: powered le secure-conn cis-central**; receiver peer pinned via
`--peer-addr DB:A6:0C:05:A2:AA`.

1. **Verify-enabled build**: `fw-build-54l15
   -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y` — exit 0; resolved app config
   `CONFIG_AUDIO_OFFLOAD_ASRC=y` + `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y`;
   zero compiler warnings.  Boot clean: `BLE ready`, `settings_load()
   OK`, `Identity: DB:A6:0C:05:A2:AA (random)`, `FLPR READY`, `offload
   init OK (rings ready)`, `Advertising as "LE Audio Receiver"`.
2. **Mode A 120 s** (`bap_central.py --peer-addr … --duration 120`):
   central 12000 frames @ 100.0 fps; receiver `Stream[0] summary:
   SDUs=11323 decoded=24062 plc=1416 decode_err=0 i2s_underrun=0
   stream_reset=0`, `Stream[1] decode_err=0 i2s_underrun=0
   stream_reset=0`; `flpr offload` → **submit=12031 success=12031
   fallback=0 busy=0**, faults **timeout=0 full=0 stale=0 seq=0 frame=0
   crc=0 payload=0**, recovery attempts=0, ASRC faults **state=0
   verify=0**, RTT ~1883 cyc, FLPR cycles ~1022 — the shadow verified
   every one of 12031 blocks with **zero mismatches**.
3. **Mode B 120 s** (`--stereo … --duration 120`, single ASE
   chan_count=2, SDU 240): central 12000 frames; receiver `Stream[0]
   summary: SDUs=11664 decoded=24062 plc=734 decode_err=0
   i2s_underrun=0 stream_reset=0`; `flpr offload` → **submit=12031
   success=12031 fallback=0**, all faults 0 incl. **verify=0**, state
   faults 0.
4. **flpr hang gate Mode A 120 s**: **16/16 checks, RESULT PASSED**
   (ack_received, asrc_fallback_triggered, asrc_state_zero,
   asrc_verify_zero, crc_zero, epoch_changed, exhaustion_zero,
   frame_count_plausible, frame_zero, probation_cleared,
   recovery_attempts_eq_1, relapses_zero, runtime_fails_zero,
   runtime_restarts_eq_1, seq_zero, state_active); final
   submit=12025 success=11980 fallback=45, state ACTIVE.
5. **flpr hang gate Mode B 120 s** (`--stereo`): **16/16 checks,
   RESULT PASSED**; final submit=12031 success=11985 fallback=46, state
   ACTIVE.  `asrc_verify_zero` PASS on both — zero shadow mismatches
   even across the injected fault/recovery cycle.
6. **Production restore**: `fw-build-54l15` (no extra args) — resolved
   app config `CONFIG_AUDIO_OFFLOAD_ASRC=y` with
   `# CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY is not set`; flashed; boot clean
   (`BLE ready`, `settings_load() OK`, `FLPR READY`, `offload init OK`,
   `Advertising`).

Stream summaries show only designed concealment (plc 1416/734 — the
nRF54L15 RF environment; T8 baseline accepted plc 0–7110 across runs)
with **zero decode errors, zero I2S underruns, zero stream resets, zero
verify/state/seq/frame/crc faults**.

## Deviations / blockers

None blocking.  Process notes:

- The dongle/receiver pairing needed the sanctioned stale-bond reset
  (`bt unpair` on the receiver + `bluetoothctl remove` on the central)
  once per fresh-connect run — the documented R4 workflow, not a
  firmware defect.  The first Mode B hang-gate attempt hit
  AuthenticationFailed from the Mode A gate's fresh bond and was re-run
  clean after the reset.
- `fw-build-54l15 -- -DCONFIG…` doubles the `--` (the wrapper already
  passes `--` before extra cmake args) — the verify-enabled build used
  `fw-build-54l15 -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y` (one `--`).
- The Mode A console capture via `scripts/read_acm.py` collided with the
  serial-mcp connection holding `/dev/ttyACM0`; the full receiver
  transcripts were recovered from the serial-mcp ring buffer and
  preserved as `receiver_modea.log` / `receiver_modeb.log` in the
  evidence dir.  No evidence lost.
- First canonical gate attempt failed only because the inventory-truth
  docs edits (README/coverage-matrix) dirtied the worktree required by
  baseline enforcement; they were committed separately (`9dd5108`) and
  the gate re-ran clean on the exact commit.

## Non-scope items untouched

R6–R10; 360-frame FLPR offload; deadline/protocol/counter-rename changes;
recovery-policy changes; nRF5340 behavior; BSim repin.
