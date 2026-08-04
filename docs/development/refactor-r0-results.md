# Refactor R0 results — canonical truth and gate inventory (ACCEPTED)

Status: **R0 ACCEPTED (2026-08-04).**  Truth reconciliation only — no
production firmware source, public firmware behavior, coverage numeric
baseline, BSim expected value, or hardware configuration changed.  Tested
implementation commit: **`53d42db`** (clean docs/metadata commit).

**Review fixes (2026-08-04).**  Three focused review rounds corrected the
original implementation in the ordered commits below; each round's G1 run
is preserved below as a prior review iteration and superseded by the next.

Round 1 (`e462aba` → `1ee8af7`):

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

Round 2 (`85bbcf3`):

5. Residual active 15-scenario claims closed: the `scripts/bsim-stage1-run.sh`
   header comment (now 16 scenarios, scenarios 1–9 twice incl.
   one-CIS-loss, 10–16 once), the `scripts/test-all.sh` BSim header line
   (canonical 16-scenario T4 matrix, not sink-only), the coverage-matrix
   weak-test fact 6, and the `src/bt_bap.c` reasons in `tests/test-matrix.json`
   and `tests/coverage-baseline.json` (reason-string-only baseline edit;
   population, per-file records, totals, generated commit, tool versions,
   and schema byte-identical).  Historical 15-scenario handoffs/evidence
   preserved.
6. Invalid-version test completion: looped regression proving a present
   empty-string or non-string integer `gcovr_version` fails with the
   invalid-version diagnostic (value, expected non-empty string,
   --write-baseline refresh instruction); runner suite 20 → 21 tests,
   null test kept.

Round 3 (`53d42db`, last focused review):

7. `docs/testing/t4-bap-bsim-matrix.md` reframed as a **historical T4
   acceptance snapshot** (accepted on `8542f1a`, 2026-08-01): a prominent
   status note marks every T4 number below as dated evidence and states the
   current post-T4 accepted runner is the 16-scenario / 25-simulation
   matrix in `scripts/bsim-stage1-run.sh` (scenarios 1–9 twice, 10–16
   once; `modea_one_cis_loss_10ms` added with the T8 Mode A assembler);
   the command section describes invoking the current script as the
   current 16-scenario command while the tables remain the historical T4
   15-scenario evidence; the runtime/gate-duration section is labeled
   historical T4.  No T4 hash/total/15-scenario/22-simulation/26-child
   number was rewritten.
8. `scripts/bsim-stage1-run.sh`: removed the duplicate stale pinned-hash
   header block (pre-review-fix "scenarios 1-8" baselining note); the
   current post-review-fix header and every array/MATRIX/hash/total/runtime
   line are byte-identical.
9. `docs/testing/coverage-matrix.md` Python inventory: coverage-runner
   tests corrected 20 → 21.

The `7ab4b39`, `1ee8af7`, and `85bbcf3` G1 runs are **superseded** by the
final G1 run on the exact corrected implementation commit `53d42db`
below; their recorded runs remain as prior review iterations.

## Commits

| Commit | Message | Contents |
|--------|---------|----------|
| `2de5e33` | `chore: enforce recorded coverage tool versions and refresh gate metadata` | `scripts/test-coverage.sh` (baseline-mode gcovr/gcov version enforcement + re-enter-dev-shell guidance), `tests/unit/test_coverage_runner/test_test_coverage_runner.py` (14 → 19 tests), `scripts/test-all.sh` header (25 → 28 twister), `tests/test-matrix.json` (monitor.sh → read_acm.py, hardware-baseline evidence, dongle smoke removal) |
| `7ab4b39` | `docs: reconcile active documentation with accepted T8 state (R0)` | original R0 implementation commit (superseded as tested hash by the review fix, still part of history) |
| `d7b6873` | `docs: record R0 acceptance evidence and results` | original evidence commit (superseded by the review-fix evidence below) |
| `e462aba` | `fix: reject invalid recorded coverage tool versions` | `scripts/test-coverage.sh` (present-null/non-string/empty recorded version fails baseline mode with refresh instruction; omitted legacy fields stay accepted; valid-string equality unchanged), `tests/unit/test_coverage_runner/test_test_coverage_runner.py` (19 → 20 tests, present-null regression) |
| `1ee8af7` | `docs: correct BSim matrix count, BZ labels, and 360-frame witnesses (R0 review fix)` | review-fix round-1 implementation commit (superseded as tested hash by the final review fix, still part of history) |
| `85bbcf3` | `fix: complete 16-scenario matrix truth and invalid-version coverage (final R0 review fix)` | review-fix round-2 implementation commit (superseded as tested hash by the last review fix, still part of history) |
| `53d42db` | `fix: frame T4 matrix doc as historical snapshot and finish runner metadata truth` | **corrected R0 implementation commit (tested)** — `docs/testing/t4-bap-bsim-matrix.md` historical-snapshot framing (T4 numbers preserved), `scripts/bsim-stage1-run.sh` duplicate stale header removed, `docs/testing/coverage-matrix.md` runner count 21 |
| evidence | `docs: record R0 closure acceptance evidence and results` | this updated file; plan R0 remains ACCEPTED |

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

Final focused run (before `85bbcf3`):

```text
bash -n scripts/test-all.sh                                PASS
bash -n scripts/test-coverage.sh                           PASS
bash -n scripts/bsim-stage1-run.sh                         PASS
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
    Ran 21 tests in ~10.1 s — OK (20 prior + looped empty/non-string
    present-version regression; null test kept)
python3 tests/unit/test_matrix/test_check_test_matrix.py
    Ran 34 tests in ~0.08 s — OK
python3 scripts/check-test-matrix.py --repo-root "$PWD"    0 error(s), 0 note(s)
python3 -m json.tool tests/test-matrix.json >/dev/null    OK
python3 -m json.tool tests/coverage-baseline.json >/dev/null  OK
git diff --check                                          clean
targeted grep: no active 15-scenario / sink-only claims in the runner
header, test-all.sh BSim line, coverage-matrix weak-test fact 6, or the
bt_bap.c reasons in test-matrix.json / coverage-baseline.json; historical
15-scenario handoffs/evidence preserved; baseline diff is the single
bt_bap.c reason string
```

Closure focused run (before `53d42db`):

```text
bash -n scripts/bsim-stage1-run.sh                         PASS
bash -n scripts/test-all.sh                                PASS
bash -n scripts/test-coverage.sh                           PASS
python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
    Ran 21 tests in ~10.1 s — OK
python3 tests/unit/test_matrix/test_check_test_matrix.py
    Ran 34 tests in ~0.08 s — OK
python3 scripts/check-test-matrix.py --repo-root "$PWD"    0 error(s), 0 note(s)
python3 -m json.tool tests/test-matrix.json >/dev/null    OK
python3 -m json.tool tests/coverage-baseline.json >/dev/null  OK
git diff --check                                          clean
targeted search: no active 15-scenario / 1-8-twice / sink-only claims in
the runner, test-all.sh, coverage-matrix, manifest, baseline, or other
active docs; t4-bap-bsim-matrix.md carries prominent historical-T4 framing
while preserving its dated 15-scenario / 22-simulation / 26-child numbers;
runner duplicate stale header removed; coverage-matrix runner count 21
```

## G1 — canonical software/build gate on the exact corrected implementation commit `1ee8af7` (superseded)

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

**Superseded by the final G1 run on `85bbcf3` below.**

## G1 — canonical software/build gate on the exact final implementation commit `85bbcf3` (superseded)

Run from the repo root in the NCS v3.3.0 dev shell, worktree clean, on
`thomas-workstation`, 2026-08-04.  Runtimes measured with the bash `time`
builtin (`TIMEFORMAT='elapsed_real_seconds %R'`; `/usr/bin/time` not
installed).

| Command | Result | Elapsed (s) |
|---|---|---|
| `./scripts/test-all.sh` | **`Gate complete: 47 PASS / 0 FAIL / 47 TOTAL`**, `PASS`, exit 0 | 974.906 |
| `./scripts/test-coverage.sh --output /tmp/r0-final-coverage --clean-output` | exit 0, 26-file population, baseline enforcement 0 errors | 385.052 |
| `fw-build-5340` | PASS, exit 0, zero compiler warnings | 19.888 |
| `fw-build-54l15` | PASS, exit 0, zero compiler warnings | 18.948 |
| `fw-build-dongle` | PASS, exit 0, zero compiler warnings | 29.099 |
| `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` | **`76 assertions, 0 failed`**, `BUILD CONTRACT PASSED`, exit 0 | — |
| `git diff --check` | clean | — |

Logs (transient, not repository-retained): `/tmp/r0-final-g1-testall.log`,
`/tmp/r0-final-g1-coverage.log`, `/tmp/r0-final-g1-build-{5340,54l15,dongle}.log`;
reports at `/tmp/r0-final-coverage/`.

**Superseded by the closure G1 run on `53d42db` below.**

## G1 — canonical software/build gate on the exact final implementation commit `53d42db` (accepted evidence)

Run from the repo root in the NCS v3.3.0 dev shell, worktree clean, on
`thomas-workstation`, 2026-08-04.  Runtimes measured with the bash `time`
builtin (`TIMEFORMAT='elapsed_real_seconds %R'`; `/usr/bin/time` not
installed).

| Command | Result | Elapsed (s) |
|---|---|---|
| `./scripts/test-all.sh` | **`Gate complete: 47 PASS / 0 FAIL / 47 TOTAL`**, `PASS`, exit 0 | 972.509 |
| `./scripts/test-coverage.sh --output /tmp/r0-closure-coverage --clean-output` | exit 0, 26-file population, baseline enforcement 0 errors | 386.472 |
| `fw-build-5340` | PASS, exit 0, zero compiler warnings | 19.875 |
| `fw-build-54l15` | PASS, exit 0, zero compiler warnings | 19.094 |
| `fw-build-dongle` | PASS, exit 0, zero compiler warnings | 28.845 |
| `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` | **`76 assertions, 0 failed`**, `BUILD CONTRACT PASSED`, exit 0 | — |
| `git diff --check` | clean | — |

Logs (transient, not repository-retained): `/tmp/r0-closure-g1-testall.log`,
`/tmp/r0-closure-g1-coverage.log`, `/tmp/r0-closure-g1-build-{5340,54l15,dongle}.log`;
reports at `/tmp/r0-closure-coverage/`.

The three earlier G1 runs — original `7ab4b39` (47/47 in 970.553 s,
coverage exact in 384.879 s), round-1 `1ee8af7` (47/47 in 971.190 s,
coverage exact in 385.461 s), round-2 `85bbcf3` (47/47 in 974.906 s,
coverage exact in 385.052 s; builds 3/3 and contract 76/76 each) — are
preserved as prior review iterations and **superseded** by this closure
run on `53d42db`, which is the accepted evidence.

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
  `53d42db`, `dirty: False` — the version-enforcement checks passed.
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
  recorded version (+1 runner test, 20 total); review-fix `85bbcf3`
  adds the looped empty/non-string present-version regression (+1 runner
  test, 21 total); `test-all.sh` header inventory corrected to 28 twister
  suites and the BSim header line to the canonical 16-scenario T4 matrix
  (runtime discovery untouched, no hardcoded 47 check); `test-matrix.json`
  evidence paths updated (read_acm.py, pre-refactor hardware baseline,
  dongle smoke removed from hardware acceptance); closure `53d42db`
  removes the runner's duplicate stale pinned-hash header and corrects the
  coverage-matrix runner count to 21.
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
  plan; review fixes corrected the active BSim matrix count to 16 scenarios
  (first nine twice) everywhere incl. the runner/test-all comments and the
  bt_bap.c manifest/baseline reasons, marked the old Stage-1 sink-only
  scope/hashes historical/superseded, and (closure `53d42db`) reframed
  `docs/testing/t4-bap-bsim-matrix.md` as a historical T4 acceptance
  snapshot with the current 16-scenario/25-simulation runner stated while
  preserving every dated T4 number.

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
because evidence was added — the final implementation commit `53d42db`
is the tested exact hash).
