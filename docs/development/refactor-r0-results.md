# Refactor R0 results — canonical truth and gate inventory (ACCEPTED)

Status: **R0 ACCEPTED (2026-08-04).**  Truth reconciliation only — no
production firmware source, public firmware behavior, coverage numeric
baseline, BSim expected value, or hardware configuration changed.  Tested
implementation commit: **`1ee8af7`** (clean docs/metadata commit).

**Review fix (2026-08-04).**  Focused review of the original implementation
found four items, fixed in the ordered commits `e462aba` → `1ee8af7`:

1. Coverage baseline version enforcement bypassed a present-null recorded
   version (only entirely-omitted legacy fields may skip the check; a
   present version must be a non-empty string).  Fixed in `e462aba` with a
   present-null fake-tool regression (runner suite 19 → 20 tests).
2. The active BSim matrix is **16 scenarios** (first nine run twice,
   remaining seven once; `modea_one_cis_loss_10ms` is the ninth repeated
   scenario), not 15.  All current claims corrected to 16 / first-nine;
   runner matrix, hashes, totals, and behavior untouched.
3. The old Stage-1 sink-only scope and its `0xFE0D4245` hashes are now
   explicitly historical/superseded in STATUS, with the current accepted
   gate stated as the 16-scenario T4 matrix (reconnect, Mode A/B,
   malformed/error/rejection, lifecycle, one-CIS-loss); official upstream
   smoke remains PARTIAL and is not production acceptance.
4. Desktop BZ1–BZ4 rename finished in STATUS/design active prose, and the
   360-frame witnesses corrected (audio_i2s `test_offload_reject_360_input_falls_back`
   pins the exact 360-frame fallback; audio_offload `test_asrc_invalid_frames`
   pins general non-480 rejection with current concrete input 240; flpr_ring
   MAX_INPUT asserts pin the 480 contract).

The original `7ab4b39` G1 run below is **superseded** by the review-fix G1
run on the exact corrected implementation commit `1ee8af7`.

## Commits

| Commit | Message | Contents |
|--------|---------|----------|
| `2de5e33` | `chore: enforce recorded coverage tool versions and refresh gate metadata` | `scripts/test-coverage.sh` (baseline-mode gcovr/gcov version enforcement + re-enter-dev-shell guidance), `tests/unit/test_coverage_runner/test_test_coverage_runner.py` (14 → 19 tests), `scripts/test-all.sh` header (25 → 28 twister), `tests/test-matrix.json` (monitor.sh → read_acm.py, hardware-baseline evidence, dongle smoke removal) |
| `7ab4b39` | `docs: reconcile active documentation with accepted T8 state (R0)` | original R0 implementation commit (superseded as tested hash by the review fix, still part of history) |
| `d7b6873` | `docs: record R0 acceptance evidence and results` | original evidence commit (superseded by the review-fix evidence below) |
| `e462aba` | `fix: reject invalid recorded coverage tool versions` | `scripts/test-coverage.sh` (present-null/non-string/empty recorded version fails baseline mode with refresh instruction; omitted legacy fields stay accepted; valid-string equality unchanged), `tests/unit/test_coverage_runner/test_test_coverage_runner.py` (19 → 20 tests, present-null regression) |
| `1ee8af7` | `docs: correct BSim matrix count, BZ labels, and 360-frame witnesses (R0 review fix)` | **corrected R0 implementation commit (tested)** — `AGENTS.md`, `README.md`, `STATUS.md`, `docs/design.md`, `docs/development/refactor-r0-handoff.md`, `docs/development/workstation-transfer-status.md`, `docs/testing/coverage-matrix.md` |
| evidence | `docs: record R0 review-fix acceptance evidence and results` | this updated file; plan R0 remains ACCEPTED |

Anchors unchanged: exact production code `971e6a4`, coverage baseline
`1a5842d` (26 files: 3281/3722 lines, 1433/2041 branches, 205/205
functions), build contract 76/76, canonical gate 47 children, T8 hardware
evidence `docs/testing/pre-refactor-hardware-baseline.md`, tools gcovr 8.4 /
gcov (GCC) 14.3.0.

## Focused verification (before each commit, on the working tree)

All on `thomas-workstation`, repo root, NCS v3.3.0 dev shell:

```text
bash -n scripts/test-all.sh                                     PASS
bash -n scripts/test-coverage.sh                                PASS
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
    Ran 19 tests in ~8.1 s — OK (14 prior + 5 new version-enforcement)
python3 tests/unit/test_matrix/test_check_test_matrix.py
    Ran 34 tests in ~0.08 s — OK
python3 scripts/check-test-matrix.py --repo-root "$PWD"        0 error(s), 0 note(s)
python3 -m json.tool tests/test-matrix.json >/dev/null         OK
git diff --check                                               clean
machine-derived counts: 28 twister + 4 exec-only + 12 Python + 3 = 47
```

Review-fix focused run (before `e462aba`/`1ee8af7`):

```text
bash -n scripts/test-coverage.sh                                PASS
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
    Ran 20 tests in ~8.8 s — OK (19 prior + 1 present-null regression)
python3 tests/unit/test_matrix/test_check_test_matrix.py
    Ran 34 tests in ~0.07 s — OK
python3 scripts/check-test-matrix.py --repo-root "$PWD"        0 error(s), 0 note(s)
python3 -m json.tool tests/test-matrix.json >/dev/null         OK
git diff --check                                               clean
targeted grep (that review's seven docs files): no active
15-scenario / first-eight / 0xFE0D4245-as-current claims;
16-scenario, first-nine, BZ1–BZ4, and corrected 360 witnesses present
```

**Correction (final focused review, 2026-08-04):** the grep above covered
only the seven docs files of that review.  A final focused review found
residual active 15-scenario claims in the runner header comment
(`scripts/bsim-stage1-run.sh`), the `scripts/test-all.sh` BSim header line
(still called sink-only), the coverage-matrix weak-test fact 6, the
`tests/test-matrix.json` `src/bt_bap.c` reason, and the
`tests/coverage-baseline.json` `src/bt_bap.c` exclusion reason.  All are
corrected to the truthful 16-scenario / first-nine-twice wording
(one-CIS-loss included) in the final review-fix implementation commit; the
coverage-baseline edit is reason-string-only with every other field
byte-identical.  The runner suite grows to 21 tests with a looped
empty/non-string present-version regression.

## G1 — canonical software/build gate on the exact corrected implementation commit `1ee8af7`

Run from the repo root in the NCS v3.3.0 dev shell, worktree clean, on
`thomas-workstation`, 2026-08-04.  Runtimes measured with the bash `time`
builtin (`TIMEFORMAT='elapsed_real_seconds %R'`; `/usr/bin/time` not
installed).

| Command | Result | Elapsed (s) |
|---|---|---|
| `./scripts/test-all.sh` | **`Gate complete: 47 PASS / 0 FAIL / 47 TOTAL`**, `PASS`, exit 0 | 971.190 |
| `./scripts/test-coverage.sh --output /tmp/r0-review-coverage --clean-output` | exit 0, 26-file population, baseline enforcement 0 errors | 385.461 |
| `fw-build-5340` | PASS, exit 0, zero compiler warnings | 19.872 |
| `fw-build-54l15` | PASS, exit 0, zero compiler warnings | 18.934 |
| `fw-build-dongle` | PASS, exit 0, zero compiler warnings | 29.107 |
| `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` | **`76 assertions, 0 failed`**, `BUILD CONTRACT PASSED`, exit 0 | — |
| `git diff --check` | clean | — |

Logs (transient, not repository-retained): `/tmp/r0-rf-g1-testall.log`,
`/tmp/r0-rf-g1-coverage.log`, `/tmp/r0-rf-g1-build-{5340,54l15,dongle}.log`;
reports at `/tmp/r0-review-coverage/`.

Superseded original run: the first implementation commit `7ab4b39` passed
the identical G1 on 2026-08-04 (47 PASS / 0 FAIL / 47 TOTAL in 970.553 s,
coverage exact in 384.879 s, builds 3/3, contract 76/76) — recorded here as
superseded by the review-fix run above, which is the accepted evidence.

### Exact 47-child composition

Children `[1]`–`[28]` twister C suites, `[29]`–`[32]` exec-only C suites,
`[33]`–`[44]` 12 Python suites, `[45]` coverage, `[46]` matrix, `[47]` BSim.

- 28 twister: actuator_apll, actuator_apll_nohfclk, actuator_none,
  actuator_sample_adjust_historical, app_lifecycle, asrc, audio_i2s,
  audio_i2s_identity, audio_shell, audio_shell_noperf, audio_shell_nrf54,
  bt_pairing_policy, decode, drift, flpr_handshake, flpr_protocol,
  flpr_ring_mgr, flpr_runtime, iso_seq, lifecycle, modea, perf,
  rate_convert, stats, timing, timing_none, timing_nrf54, volume.
- 4 exec-only: audio_offload, flpr_audio_process, flpr_ring, offload_asrc.
- 12 Python: gate, flpr_stall_gate, flpr_hang_gate, bluez_wp_gate,
  bluez_wp_phase3_gate, bsim_runner, build_contract, hci_raw_connect,
  bap_central_policy, bap_central_writer, test_matrix, test_coverage_runner.
- coverage + matrix + bsim: stage1.

All 47 children PASS, zero FAIL.

### Coverage totals (standalone run, exact baseline match)

- Numeric population: **26 files**.
- Lines **3281/3722 (88.2%)**, branches **1433/2041 (70.2%)**, functions
  **205/205 (100.0%)** — identical to the committed `tests/coverage-baseline.json`
  at `1a5842d`.
- `baseline enforcement: 0 error(s)`; `baseline enforcement PASS`.
- Run-manifest records `gcovr_version: gcovr 8.4`,
  `gcov_version: gcov (GCC) 14.3.0` (matching the baseline), source commit
  `1ee8af7`, `dirty: False` — the version-enforcement checks passed.
- The gate's own coverage child (enforcement against the same committed
  baseline) also PASSed with identical numbers.

### Builds and build contract

- All three pristine production builds pass (nRF5340 sysbuild dual-core,
  nRF54L15 + FLPR, dongle hci_ipc) with **zero compiler warnings**.
- Build contract: **76 assertions, 0 failed, BUILD CONTRACT PASSED, exit 0**
  on the fresh `build/nrf5340` and `build/nrf54l15` trees.

### Warning disposition (all classified, none actionable)

Every build/gate diagnostic was matched against the documented
non-actionable NCS v3.3.0 set (STATUS.md "Build warning diagnostics" +
hardware-baseline warning scan):

- nRF5340: `PARTITION_MANAGER`/`_ENABLED` deprecations,
  `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice gap, `SB_CONFIG_PARTITION_MANAGER`
  sysbuild notice, experimental `BT_LL_SW_SPLIT`/`BT_CTLR_SET_HOST_FEATURE`/
  `BT_CTLR_PERIPHERAL_ISO`, `__ASSERT()` informational, and the controller
  `CONFIG_BT_CTLR_ADVANCED_FEATURES=y` default-value-change CMake notice —
  the last is pre-existing: the retained T8-era build log
  `/tmp/opencode/b5340-final.log` (Aug 3, accepted `971e6a4` build) contains
  the same single occurrence, and R0 changed no firmware build input.
- nRF54L15: `UART_CONSOLE` assigned-n-got-y, `simple_bus_reg` /
  `avoid_unnecessary_addr_size` DT warnings, `__ASSERT()` informational,
  `drivers__watchdog` "No SOURCES given".
- dongle: `PARTITION_MANAGER` deprecations + `SB_CONFIG_PARTITION_MANAGER`
  notices.
- Gate log `<wrn>`/`<err>` lines: all inside deliberately-passing
  failure-injection unit suites (flpr_handshake IPC errors, audio_volume VCP
  state errors, bluez_wp_gate negative-path fixtures); zero compiler
  warnings, zero Kconfig assigned-value warnings.

### BSim hash/count disposition (unchanged)

Stage 1 passed with every pinned value byte-identical: mono 10 ms full
`0x22AB5C0D` L==R `0x32777D65`; mono 7.5 ms `0x01A3EB05`; Mode A/B 10 ms
`0xBAE24F7E` (L `0x32777D65`, R `0xD3EE3722`); Mode A 7.5 ms `0x2D95D15C`;
Mode B 7.5 ms `0xFF82CADB`; invalid-SDU-resume `0x0C61918D`;
one-CIS-loss `0x30D6BAF0`; reconnect second segment equals the fresh mono
10 ms oracle.  Scenario totals unchanged.  No hash or count was repinned.

## What R0 changed

- **Tooling/metadata**: coverage runner now enforces recorded gcovr/gcov
  versions in baseline mode (absent fields still accepted, `--write-baseline`
  records, `--report-only` never enforces); 5 new fake-tool runner tests;
  review-fix `e462aba` additionally rejects a present null/non-string/empty
  recorded version (+1 runner test, 20 total); `test-all.sh` header
  inventory corrected to 28 twister suites (runtime discovery untouched, no
  hardcoded 47 check); `test-matrix.json` evidence paths updated
  (read_acm.py, pre-refactor hardware baseline, dongle smoke removed from
  hardware acceptance).
- **Documentation**: plan-of-record moved to `refactor-plan.md` (design.md
  historical); T0–T8 marked COMPLETE/ACCEPTED; BZ1–BZ4 rename of the desktop
  BlueZ/WirePlumber track (completed in the review fix in STATUS/design
  active prose); 47-child suite inventory; behavior-contract and
  coverage-matrix versions T8 2026-08-04; duplicate CODEC-013 removed;
  CV-001 updated to the `1a5842d` 26-file baseline; 7.5 ms FLPR offload
  limitation recorded as a known behavior question with the exact witnesses
  (audio_i2s 360 fallback, audio_offload non-480 rejection with 240 input,
  flpr_ring 480 contract, source constant); undefined "prior T9
  failing-hardware provenance" wording replaced with the precise prior
  8–30% RF-loss hardware sessions; G1 coverage command corrected in the
  plan; review fix corrected the active BSim matrix count to 16 scenarios
  (first nine twice) and marked the old Stage-1 sink-only scope/hashes
  historical/superseded.

## Deviations and blockers

None.  No escalation conditions were hit: live tool versions match the
committed baseline, suite count is 28/4/12/47, no replacement evidence path
failed the matrix checker, no full-gate hash/count changed, no build output
gained a warning or contract drift, and no documentation evidence conflicted
with repository truth.  No hardware was used; no baseline, hash, or
assertion was regenerated or weakened.

## Evidence-only re-checks on the evidence commit

`git diff --check`, the matrix checker, and the coverage-runner / matrix
Python suites re-run on the evidence commit (G1 was not rerun solely
because evidence was added — the corrected implementation commit `1ee8af7`
is the tested exact hash).
