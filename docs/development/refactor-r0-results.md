# Refactor R0 results — canonical truth and gate inventory (ACCEPTED)

Status: **R0 ACCEPTED (2026-08-04).**  Truth reconciliation only — no
production firmware source, public firmware behavior, coverage numeric
baseline, BSim expected value, or hardware configuration changed.  Tested
implementation commit: **`7ab4b39`** (clean docs/metadata commit).

## Commits

| Commit | Message | Contents |
|--------|---------|----------|
| `2de5e33` | `chore: enforce recorded coverage tool versions and refresh gate metadata` | `scripts/test-coverage.sh` (baseline-mode gcovr/gcov version enforcement + re-enter-dev-shell guidance), `tests/unit/test_coverage_runner/test_test_coverage_runner.py` (14 → 19 tests), `scripts/test-all.sh` header (25 → 28 twister), `tests/test-matrix.json` (monitor.sh → read_acm.py, hardware-baseline evidence, dongle smoke removal) |
| `7ab4b39` | `docs: reconcile active documentation with accepted T8 state (R0)` | `AGENTS.md`, `README.md`, `STATUS.md`, `docs/design.md`, `docs/development/bluez-wireplumber-interoperability-plan.md`, `docs/development/pre-refactor-testing-plan.md`, `docs/development/refactor-plan.md` (G1 coverage command), `docs/development/refactor-r0-handoff.md` (phase record), `docs/testing/behavior-contract.md`, `docs/testing/coverage-matrix.md`, `docs/testing/pre-refactor-hardware-baseline.md` |
| evidence | `docs: record R0 acceptance evidence and results` | this file, plus the R0 ACCEPTED mark and results link in `docs/development/refactor-plan.md` |

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

## G1 — canonical software/build gate on the exact implementation commit `7ab4b39`

Run from the repo root in the NCS v3.3.0 dev shell, worktree clean, on
`thomas-workstation`, 2026-08-04.  Runtimes measured with the bash `time`
builtin (`TIMEFORMAT='elapsed_real_seconds %R'`; `/usr/bin/time` not
installed).

| Command | Result | Elapsed (s) |
|---|---|---|
| `./scripts/test-all.sh` | **`Gate complete: 47 PASS / 0 FAIL / 47 TOTAL`**, `PASS`, exit 0 | 970.553 |
| `./scripts/test-coverage.sh --output /tmp/r0-coverage --clean-output` | exit 0, 26-file population, baseline enforcement 0 errors | 384.879 |
| `fw-build-5340` | PASS, exit 0, zero compiler warnings | 19.839 |
| `fw-build-54l15` | PASS, exit 0, zero compiler warnings | 19.234 |
| `fw-build-dongle` | PASS, exit 0, zero compiler warnings | 28.777 |
| `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` | **`76 assertions, 0 failed`**, `BUILD CONTRACT PASSED`, exit 0 | — |
| `git diff --check` | clean | — |

Logs (transient, not repository-retained): `/tmp/r0-g1-testall.log`,
`/tmp/r0-g1-coverage.log`, `/tmp/r0-g1-build-{5340,54l15,dongle}.log`;
reports at `/tmp/r0-coverage/`.

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
  `7ab4b39`, `dirty: False` — the new version-enforcement checks passed.
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
  `test-all.sh` header inventory corrected to 28 twister suites (runtime
  discovery untouched, no hardcoded 47 check); `test-matrix.json` evidence
  paths updated (read_acm.py, pre-refactor hardware baseline, dongle smoke
  removed from hardware acceptance).
- **Documentation**: plan-of-record moved to `refactor-plan.md` (design.md
  historical); T0–T8 marked COMPLETE/ACCEPTED; BZ1–BZ4 rename of the desktop
  BlueZ/WirePlumber track; 47-child suite inventory; behavior-contract and
  coverage-matrix versions T8 2026-08-04; duplicate CODEC-013 removed;
  CV-001 updated to the `1a5842d` 26-file baseline; 7.5 ms FLPR offload
  limitation recorded as a known behavior question with direct witnesses;
  undefined "prior T9 failing-hardware provenance" wording replaced with the
  precise prior 8–30% RF-loss hardware sessions; G1 coverage command
  corrected in the plan.

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
because evidence was added — the implementation commit `7ab4b39` is the
tested exact hash).
