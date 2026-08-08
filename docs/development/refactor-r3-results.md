# R3 results — test-runner and hardware-gate consolidation

Accepted: 2026-08-04.  Start commit `ee31b05`; handoff commit `6b4198e`;
implementation commit `0c19fd5`; mid-gate test fix `c10c610`; docs commit
(this document's commit).  No production firmware, coverage baseline, or
BSim pins changed.

## Commits

1. `6b4198e` — `docs: record R3 handoff — suite/parser/hash consolidation`.
2. `0c19fd5` — `refactor: consolidate suite discovery, gate ownership, and
   BSim data` (22 files: inventory module, gate/coverage/checker refactors,
   gate-suite split, shared FLPR parser, hang-gate boundaries, BlueZ
   shared UUIDs, BSim versioned data, retirements, active docs).
3. `c10c610` — `test: capture bsim CLI expected-negative stderr in unit
   suite` (committed mid-gate; test-only stderr capture, re-verified
   52 PASS standalone; the canonical gate was re-run on this final commit
   for a clean acceptance record).
4. docs-only commit — `docs: accept R3 test-runner consolidation` (this
   document + plan/STATUS completion markers).

## One-to-one obsolete test ownership map (`tests/unit/gate/test_gate.py`)

The obsolete gate child (22 tests: 17 stall + 5 dongle) is retired.  Every
unique assertion moved; equivalent assertions merged (no raw function-count
retention):

| Obsolete test | Destination | Disposition |
|---|---|---|
| `test_dongle_flash_default_autodetect` | `tests/unit/fw_flash_dongle/test_fw_flash_dongle.py` | moved unchanged in behavioral intent |
| `test_dongle_flash_explicit_serial` | same | moved unchanged |
| `test_dongle_flash_invalid_serial` | same | moved unchanged |
| `test_dongle_flash_missing_artifacts` | same | moved unchanged |
| `test_dongle_flash_missing_dev_shell` | same | moved unchanged |
| `test_parse_offload_active` | `tests/unit/flpr_stall_gate/test_flpr_stall_gate.py` `TestRegexParsing.test_parse_offload_active` | migrated (unique input) |
| `test_parse_offload_fallback` | `...test_parse_offload_fallback` | migrated (unique input) |
| `test_parse_offload_max_exhaustion` | `...test_parse_offload_exhaustion` | merged (recovery_attempts==10 assertion added) |
| `test_parse_offload_faults` | `...test_parse_offload_faults` | migrated (unique input) |
| `test_parse_offload_zero_faults` | `...test_parse_offload_zero_faults` | migrated |
| `test_timed_stall_ack_regex` | `...test_stall_timed_ack` | already covered; not duplicated |
| `test_timed_stall_ack_multiple` | `...test_timed_stall_ack_multiple` | migrated |
| `test_gate_timeout_on_stale_state` | `TestGateMigratedRunnerBehavior.test_gate_timeout_on_stale_state` | migrated |
| `test_gate_exhaustion_error` | `TestGateExhaustion.test_exhaustion_detected` | already covered; not duplicated |
| `test_gate_success_path` | `TestGateSuccess.test_success_path` | already covered; not duplicated |
| `test_gate_no_fallback_fails` | `TestGateNoFallback.test_no_fallback_times_out` | already covered; not duplicated |
| `test_timed_ack_wrong_duration_rejected` | `...test_timed_ack_wrong_duration_rejected` | migrated |
| `test_timeout_full_pass_gate` | `...test_timeout_full_pass_gate` | migrated |
| `test_integrity_faults_rejected` | `...test_integrity_faults_rejected` | migrated (5 keys) |
| `test_gate_success_with_timeout_fault` | `TestGateMigratedRunnerBehavior.test_gate_success_with_timeout_fault` | migrated |
| `test_seq_fault_fails_gate` | `...test_seq_fault_fails_gate` | migrated |
| `test_only_integrity_faults_rejected` | `...test_integrity_faults_rejected` | merged (comprehensive all-faults line added) |

`tests/unit/gate/` deleted after migration.  Python child `gate` is replaced
by child `fw_flash_dongle` (label from the shared inventory module), keeping
the Python child count at 12.

## Focused results (before the full gate)

- Inventory CLI on the R3 tree: **twister 28 / exec-only 4 / python 12**
  (gate count 44 + coverage + matrix + BSim = 47).
- Python suites: fw_flash_dongle 5/5; flpr_stall_gate 30/30 (19 → 30 with
  migrated cases); flpr_hang_gate 21/21 (15 parser + 6 runner
  lifecycle/state/cleanup); bluez_wireplumber_gate 53/53;
  bluez_wireplumber_phase3_gate 89/89 (incl. shared-UUID source test);
  bsim_runner 52 PASS/0 FAIL (schema fixtures + production-pins proof);
  test_matrix 42/42 (incl. inventory CLI + checker-consistency fixtures);
  test_coverage_runner 24/24 (inventory-driven discovery fixtures).
- `bash -n scripts/test-all.sh scripts/test-coverage.sh
  scripts/bsim-stage1-run.sh` — clean.
- Matrix checker fixture suite: 0 error(s), 0 note(s) on the real repo and
  on all fixture repos.

## G1 results (clean final commit `c10c610`)

- `./scripts/test-all.sh` — **47 PASS / 0 FAIL / 47 TOTAL** (28 twister +
  4 exec-only + 12 Python + coverage + matrix + BSim), elapsed
  **16 m 11 s** (bash `time` builtin), exit 0, log
  `/tmp/r3-gate-final.log` (transient, not repo-retained).  Coverage child:
  baseline enforcement **0 error(s), PASS** against the committed
  `tests/coverage-baseline.json`; matrix child `0 error(s), 0 note(s)`;
  BSim Stage 1 **PASS — all scenarios strict-checked** (25 runs).
  Zero misleading FAIL lines in the retained log (bsim CLI expected-negative
  stderr captured by the suite).
- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — all PASS (exit 0).
  Warning inventory identical to the documented pre-existing NCS
  diagnostics (STATUS.md "Build warning diagnostics"): PARTITION_MANAGER
  deprecation (5340/dongle), experimental BT_LL_SW_SPLIT /
  BT_CTLR_PERIPHERAL_ISO / BT_CTLR_SET_HOST_FEATURE, SW Split
  CONN_ISO_LOW_LATENCY_POLICY choice, FLPR UART_CONSOLE assigned-value,
  FLPR/CPUAPP reserved-memory `simple_bus_reg` /
  `avoid_unnecessary_addr_size`, CMake "No SOURCES given" (watchdog).
  **Zero new/actionable warnings.**
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` — **76 assertions, 0 failed, BUILD CONTRACT
  PASSED**.
- BSim pins unchanged (no repinning): mono 10 ms `0x22AB5C0D`,
  mono 7.5 ms `0x01A3EB05`, Mode A/B 10 ms `0xBAE24F7E`,
  Mode A 7.5 ms `0x2D95D15C`, Mode B 7.5 ms `0xFF82CADB`,
  invalid-SDU `0x0C61918D`, one-CIS-loss `0x30D6BAF0`; reconnect equals
  fresh mono oracle.
- `git diff --check` — clean.  Worktree clean.

## Coverage population / tool versions

No production source changed, so the numeric population and baseline are
**unchanged**: population **26 files**, committed baseline at the R2
migration (`1343c35` → re-verified by the gate's enforcement child);
tool versions **gcovr 8.4 / gcov (GCC) 14.3.0** (recorded in the baseline;
enforcement re-verified PASS by the canonical gate).

## Deviations / blockers

None.  One process note: `c10c610` (test-only stderr capture for the new
bsim CLI precedence tests) was committed while the first gate run was in
flight; that gate had already passed 47/47 with the pre-fix test file
(52/0 standalone).  The canonical gate was re-run on the final commit for a
clean acceptance record.

## Non-scope items untouched

R4 shell separation; production C behavior/APIs; coverage baseline
migration; hardware flash/stream runs; BSim expected-value updates; either
BlueZ test suite; historical result claims.
