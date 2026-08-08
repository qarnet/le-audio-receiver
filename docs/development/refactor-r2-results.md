# R2 results — dead API and historical-source cleanup

Accepted: 2026-08-04.  Start commit `6ad11d0`, code commit `1343c35`,
baseline commit `b8052b2`, docs commit (this document's commit).

## Commits

1. `1343c35` — `refactor: remove dead APIs and isolate historical actuator`
   (29 files; source/test/matrix/active-doc changes; old baseline retained).
2. `b8052b2` — `coverage: migrate baseline after R2 dead-code cleanup`
   (2 files; `tests/coverage-baseline.json` + migration table).
3. docs-only commit — `docs: accept R2 dead API cleanup` (this document).

## Deleted symbols and caller proof

Repository-wide search at `6ad11d0` (pre-deletion) showed zero production
callers for every deleted symbol:

| Symbol/source | Production callers | Remaining uses before R2 (all removed unless noted) |
|---|---|---:|
| `audio_clock_actuator_consume_sample_adjustment()` | 0 | production header decl + `apll.c`/`none.c` impls, APLL/NONE/nohfclk test asserts, 2 matrix outcomes, active docs.  Survives ONLY as the test-local historical symbol under `tests/unit/actuator_sample_adjust_historical/src/` |
| `flpr_ring_mgr_set_consume_cb()` / `flpr_ring_consume_cb_t` | 0 | no-op impl, header decl + typedef, direct no-op test, 2 copied mocks, 1 matrix outcome.  Remaining doc mentions are dated evidence (`t1-flpr-production-tests.md`) or the plan record |
| `audio_rate_converter_nearest_stereo()` | 0 | decl/impl, 5 dedicated tests, 1 matrix outcome |
| `audio_offload_is_stopped()` | 0 | header decl, nRF54 impl, non-nRF stub, 3-test suite, 2 matrix outcomes.  Remaining doc mentions are dated evidence (`phase6-stage4a-runtime-restart-results.md`) or the plan record |
| `DEFAULT_VOL` | 0 | macro definition only in `src/audio_volume.c` |
| `src/audio_clock_actuator_sample_adjust.c` | 0 production builds | moved to `tests/unit/actuator_sample_adjust_historical/src/audio_clock_actuator_sample_adjust_historical.c` (git rename), test-local header added |

Post-R2 stale-reference search: only allowed occurrences remain (historical
test-local consume symbol, dated evidence documents, plan-of-record).

## Focused suite counts (native_sim/native/64, `west build -t run`)

| Suite | Before | After | Delta |
|-------|-------:|------:|-------|
| actuator_apll | 8 | 8 | renamed `test_repeated_calls_and_consume` → `test_repeated_calls` (consume asserts removed) |
| actuator_apll_nohfclk | 1 | 1 | consume assert removed |
| actuator_none | 1 | 1 | consume asserts removed |
| actuator_sample_adjust_historical | 7 | 7 | unchanged (source moved, behavior identical) |
| flpr_ring_mgr | 67 | 66 | `test_set_consume_cb_noop` removed |
| rate_convert | 10 | 5 | five `nearest_stereo` tests removed |
| audio_offload (exec) | 58 | 55 | `audio_offload_isstopped` suite (3) removed; `test_is_healthy` gains `get_status(NULL)` probe (coverage rule step 4) — test count net −3 |
| offload_asrc (exec) | 28 | 28 | mock `set_consume_cb` removed |
| volume | 12 | 12 | `DEFAULT_VOL` macro removal, no test change |

All 9 focused suites PASS / 0 FAIL.  Python `test_matrix` (34) and
`test_coverage_runner` (21) PASS.  Matrix checker `0 error(s), 0 note(s)`
without and with `--coverage-json`.

## Coverage migration (old `1a5842d` → new `1343c35`)

Tool versions unchanged: **gcovr 8.4 / gcov (GCC) 14.3.0**.  Population 26
(both).  Exclusion list 4 → 3: `src/audio_clock_actuator_sample_adjust.c`
dropped (moved to test-local location; it was already excluded from numeric
coverage, so the population count is unchanged).

Aggregate:

| Metric | Old | New |
|--------|-----|-----|
| lines | 3281/3722 (88.2%) | 3505/3946 (88.8%) |
| branches | 1433/2041 (70.2%) | 1467/2067 (71.0%) |
| functions | 205/205 (100.0%) | 209/209 (100.0%) |

R2-affected surviving files (covered/total, old → new):

| File | lines | branches | functions |
|------|-------|----------|-----------|
| audio_clock_actuator_apll.c | 17/17 → 15/15 | 4/4 → 4/4 | 4/4 → 3/3 |
| audio_clock_actuator_none.c | 8/8 → 6/6 | 0/0 → 0/0 | 4/4 → 3/3 |
| audio_rate_convert.c | 30/32 → 10/10 | 14/16 → 0/0 | 3/3 → 2/2 |
| flpr_ring_mgr.c | 559/599 → 694/741 | 214/312 → 230/326 | 24/24 → 29/29 |
| audio_offload.c | 588/714 → 583/707 | 223/377 → 223/375 | 18/18 → 17/17 |
| audio_volume.c | 36/36 → 36/36 | 24/32 → 24/32 | 5/5 → 5/5 |

Denominator explanations: every removed line/branch/function belonged to a
deleted zero-caller API and was fully covered.  `audio_offload.c` lines
would otherwise have dropped below the old ratio (588/714 → 582/707), so per
refactor-plan coverage-migration rule step 4 the surviving
`audio_offload_get_status(NULL)` null-guard line is now covered by a probe
in `test_is_healthy` (583/707 ≥ 588/714).  No covered live behavior was
deleted to improve a percentage.  `flpr_ring_mgr.c`, `audio_i2s.c`,
`flpr_handshake.c`, `stream_lifecycle.c`, `audio_timing_nrf54.c` increases
are R1-era test additions (accepted after `1a5842d` with enforcement passing
because current ≥ baseline) absorbed at this migration — not R2 changes.

## G1 results (clean `b8052b2`)

- `./scripts/test-all.sh` — **47 PASS / 0 FAIL / 47 TOTAL** (28 twister +
  4 exec-only + 12 Python + coverage + matrix + BSim), elapsed ~16 m 22 s,
  exit 0.  Canonical child count unchanged (47).
- `./scripts/test-coverage.sh --output /tmp/r2-coverage --clean-output` —
  baseline enforcement **0 error(s)**, PASS; numeric 3505/3946 L,
  1467/2067 B, 209/209 F, population 26.
- `fw-build-5340` — PASS (exit 0).  `fw-build-54l15` — PASS (exit 0).
  `fw-build-dongle` — PASS (exit 0).  Builds 3/3.
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` — **76 assertions, 0 failed, BUILD CONTRACT
  PASSED**.
- BSim Stage 1: all hashes unchanged — mono 10 ms `0x22AB5C0D`,
  mono 7.5 ms `0x01A3EB05`, Mode A/B 10 ms `0xBAE24F7E`, Mode A 7.5 ms
  `0x2D95D15C`, Mode B 7.5 ms `0xFF82CADB`, invalid-SDU `0x0C61918D`,
  one-CIS-loss `0x30D6BAF0`; reconnect equals fresh mono oracle.
- Warnings: zero new/actionable warnings in gate or builds.  All build-log
  warnings are documented pre-existing NCS diagnostics (STATUS.md
  "Build warning diagnostics": PARTITION_MANAGER deprecation, experimental
  BT_LL_SW_SPLIT / BT_CTLR_PERIPHERAL_ISO, FLPR UART_CONSOLE=y→n
  assigned-value, FLPR/CPUAPP reserved-memory `simple_bus_reg`,
  `avoid_unnecessary_addr_size`, CMake "No SOURCES given" watchdog).
- `git diff --check` — clean.  Worktree clean.

No G2 required: deletion proof plus G1 production builds establish that no
live production call path changed.

## Final status

R2 ACCEPTED.  No deviations from the handoff; no blockers.  Non-scope items
(R3+ runner consolidation, 360-frame FLPR, BSim repinning, hardware runs)
untouched.
