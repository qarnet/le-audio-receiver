# R4 handoff — shell and acceptance-harness separation

Start commit: `3a83c5b` (R3 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before implementation; the implementation must match it and the
results document (`docs/development/refactor-r4-results.md`) must record
what actually happened.

## Goal

Split shell command ownership mechanically without command/output/return
behavior changes.  Normal audio diagnostics must not compile the FLPR
acceptance harness by accident.

## Scope (exact decisions)

### 1) `src/audio_shell.c` retains only audio diagnostics

- Commands: `audio status`, `audio reset-stats`, `audio stop`,
  `audio perf`, `audio perf-reset`.
- Data/helpers: `RESAMPLER_NAME`, `perf_path_names`, the `audio_cmds`
  static subcommand set, and the `audio` root registration.
- `AUDIO_SHELL_TEST` seams: `audio_shell_test_cmd_status`,
  `audio_shell_test_cmd_reset_stats`, `audio_shell_test_cmd_stop`,
  `audio_shell_test_cmd_perf`, `audio_shell_test_cmd_perf_reset`.
- Preserve byte-exact output, help strings, mandatory/optional arg
  counts, and return values.
- `bt unpair` moves out (section 2); all FLPR commands move out
  (sections 3–4).

### 2) New `src/bt_shell.c` — `bt unpair` (both targets when CONFIG_SHELL)

- `cmd_bt_unpair` (static), the `bt_cmds` static subcommand set, the
  `bt` root registration ("Bluetooth test commands."), and the
  existing-named seam `audio_shell_test_cmd_bt_unpair`.
- Behavior byte-identical to today: `bt_bap_pairing_reset()` result
  propagates; success/error text unchanged.

### 3) New `src/flpr_shell.c` — production FLPR diagnostics

Compiled when `CONFIG_SHELL && CONFIG_SOC_NRF54L15`.  Owns:

- `flpr status` (`cmd_flpr_status`), `flpr offload`
  (`cmd_offload_status`), `flpr runtime` (`cmd_flpr_runtime_status`),
  `flpr restart` (`cmd_flpr_restart`).
- Helpers `offload_state_str`, `flpr_runtime_state_str`,
  `flpr_runtime_stage_str`.
- Seams: `audio_shell_test_cmd_flpr_status`,
  `audio_shell_test_cmd_offload_status`,
  `audio_shell_test_cmd_flpr_runtime_status`,
  `audio_shell_test_cmd_flpr_restart`.

Registration (verified NCS v3.3.0 cross-TU section macros, zephyr
`include/zephyr/shell/shell.h` 507–561):

```c
SHELL_SUBCMD_SET_CREATE(flpr_cmds, (flpr));
SHELL_CMD_REGISTER(flpr, &flpr_cmds, "FLPR co-processor commands.", NULL);

SHELL_SUBCMD_ADD((flpr), status, NULL, "FLPR handshake/health status.",
		 cmd_flpr_status, 1, 0);
SHELL_SUBCMD_ADD((flpr), offload, NULL,
		 "Audio offload status (Phase 6 Stage 2).", cmd_offload_status, 1, 0);
SHELL_SUBCMD_ADD((flpr), runtime, NULL,
		 "FLPR runtime restart manager status.", cmd_flpr_runtime_status, 1, 0);
SHELL_SUBCMD_ADD((flpr), restart, NULL,
		 "Restart FLPR co-processor. [timeout_ms default 10000].",
		 cmd_flpr_restart, 1, 1);
```

This TU performs the **sole** `SHELL_CMD_REGISTER(flpr, ...)`.

### 4) New `src/flpr_acceptance_shell.c` — acceptance harness

Compiled only when `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`.  Owns:

- `flpr stress` (`cmd_flpr_stress`), `flpr hang` (`cmd_flpr_hang`),
  and every `flpr ring *` command: `status`, `init`, `reset`, `test`,
  `acceptance`, `stall`, `stall_flpr`, `stall_flpr_ms`.
- `acceptance_buf[FLPR_RING_PAYLOAD_CAPACITY_BYTES]`, `acceptance_rs`,
  `ACCEPT_STATUS()`/`ACCEPT_BUF()` macros, and the ring-status seam
  `audio_shell_test_cmd_flpr_ring_status`.

Registration:

```c
SHELL_SUBCMD_SET_CREATE(flpr_ring_cmds, (flpr, ring));

SHELL_SUBCMD_ADD((flpr, ring), status, NULL, "PCM ring status.",
		 cmd_flpr_ring_status, 1, 0);
SHELL_SUBCMD_ADD((flpr, ring), init, NULL, "Initialize PCM rings.",
		 cmd_flpr_ring_init, 1, 0);
SHELL_SUBCMD_ADD((flpr, ring), reset, NULL,
		 "Reset PCM rings with new epoch.", cmd_flpr_ring_reset, 1, 0);
SHELL_SUBCMD_ADD((flpr, ring), test, NULL,
		 "Ring test: <blocks> [rate_blk_per_s]", cmd_flpr_ring_test, 1, 2);
SHELL_SUBCMD_ADD((flpr, ring), acceptance, NULL,
		 "Run full acceptance: loopback, stalls, stale, empty.",
		 cmd_flpr_ring_acceptance, 1, 1);
SHELL_SUBCMD_ADD((flpr, ring), stall, NULL,
		 "CPU-side producer stall: on|off. Blocks produce_block as FULL.",
		 cmd_flpr_ring_stall_producer, 1, 1);
SHELL_SUBCMD_ADD((flpr, ring), stall_flpr, NULL,
		 "FLPR-side stall: <bits> (0x01=cons_input 0x02=prod_output 0=clear).",
		 cmd_flpr_ring_stall_flpr, 1, 1);
SHELL_SUBCMD_ADD((flpr, ring), stall_flpr_ms, NULL,
		 "FLPR timed stall: <bits> <duration_ms> (auto-clear after duration).",
		 cmd_flpr_ring_stall_flpr_ms, 2, 1);

/* parent `ring` entry and root-level acceptance commands */
SHELL_SUBCMD_ADD((flpr), ring, &flpr_ring_cmds,
		 "PCM ring transport commands.", NULL, 0, 0);
SHELL_SUBCMD_ADD((flpr), stress, NULL,
		 "Stress test N ping/pong (default 100k, max 1M).", cmd_flpr_stress, 1, 1);
SHELL_SUBCMD_ADD((flpr), hang, NULL,
		 "Inject FLPR hang (test-only). Sends FAULT_HANG, waits 500ms for ACK. "
		 "FLPR ACKs then disables IRQs and spins — halts ring+heartbeat. "
		 "Recovery via heartbeat supervisor + runtime restart.",
		 cmd_flpr_hang, 1, 0);
```

- Section-based additions work cross-TU (verified against NCS v3.3.0
  `SHELL_SUBCMD_SET_CREATE`/`SHELL_SUBCMD_ADD` in kernel_shell.c,
  net_shell.c, thread.c, and the shell_module sample); do **not** expose
  handlers or add a shared header.  The nested set and the parent `ring`
  addition stay in this TU.
- Preserve every command name, help string, mandatory/optional arg
  count, format string, branch, errno, and behavior exactly.

### 5) Root Kconfig

```kconfig
config AUDIO_ACCEPTANCE_DIAGNOSTICS
	bool "FLPR acceptance diagnostics shell commands"
	default n
	depends on SOC_NRF54L15 && SHELL
	help
	  Gate the FLPR acceptance-harness shell commands (flpr ring *,
	  flpr stress, flpr hang) behind an explicit diagnostics option so
	  normal audio diagnostics never compile the acceptance harness.
```

R8's FLPR-image `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` remains separate and
out of scope.

### 6) CMake + board config

- `src/audio_shell.c` and `src/bt_shell.c` under `CONFIG_SHELL`
  (`zephyr_sources_ifdef`).
- `src/flpr_shell.c` under `CONFIG_SHELL AND CONFIG_SOC_NRF54L15` via
  `if(...) target_sources(app PRIVATE ...)` (zephyr_sources_ifdef takes
  one condition).
- `src/flpr_acceptance_shell.c` under `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`.
- Enable `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y` only in
  `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.  Not in shared `prj.conf`,
  not in the nRF5340 board conf.

### 7) No behavior/API changes

- Do not modify `bt_bap`, `flpr_ring_mgr`, `flpr_handshake`, runtime, or
  offload behavior/APIs.
- Long-running acceptance state remains static BSS and
  shell-thread-owned.

## Test ownership

- `tests/unit/audio_shell` and `tests/unit/audio_shell_noperf` CMake
  compile production `src/bt_shell.c` too (seam names preserved).
- `tests/unit/audio_shell_nrf54` CMake compiles all four production TUs
  and defines `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=1`; keep the public
  shell execution tests.  No new suite dirs unless mechanically required.
- All per-file `AUDIO_SHELL_TEST` seam blocks carry
  `GCOVR_EXCL_START`/`GCOVR_EXCL_STOP`.
- Existing C shell tests and Python flpr_hang/stall fixtures pass
  unchanged in expected output.  Add focused tests only for cross-TU
  registration and config-off absence if current coverage cannot prove
  them.

## Matrix/coverage migration

- `tests/test-matrix.json`: replace the single `src/audio_shell.c`
  ownership with exact entries for `src/audio_shell.c`,
  `src/bt_shell.c`, `src/flpr_shell.c`,
  `src/flpr_acceptance_shell.c` with concrete suites/evidence/outcomes/
  transitions as the checker schema requires.  Hardware scripts belong on
  acceptance/diagnostic owners where exact output is consumed.
- Coverage split migration (refactor-plan.md rule): old audio_shell
  record 302/519 lines, 124/274 branches, 23/23 functions.  Generate a
  report-only candidate on the clean implementation commit, aggregate the
  four replacement files, prove no ratio decrease, and explain
  compiler/config effects if totals differ.  Population expected 29.
- Candidate baseline via `--write-baseline /tmp/...` only; inspect, then
  copy the exact candidate into `tests/coverage-baseline.json` in a
  dedicated migration commit.  Update `docs/testing/coverage-matrix.md`
  with split provenance, old aggregate, new aggregate, exact tool
  versions, and the commit.  Canonical enforcement must pass on the
  baseline commit.

## Focused verification (G0)

- Twister: `audio_shell`, `audio_shell_noperf`, `audio_shell_nrf54`.
- Python: `flpr_hang_gate`, `flpr_stall_gate`; BlueZ phase3 parser
  fixtures only if bt/audio output is touched.
- Matrix checker + coverage report-only/migration.
- `git diff --check`; warning-free under repo policy.

## Full G1

- `./scripts/test-all.sh` => 47 PASS / 0 FAIL / 47 TOTAL, coverage
  population 29 after accepted migration, matrix clean, BSim unchanged.
- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all pass;
  inspect warnings and classify only documented unavoidable diagnostics.
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` => 76/76 (no acceptance-config assertion
  added; contract total unchanged).

## Hardware smoke (nRF54L15 only; planned flashing authorized; no mass
erase/recovery)

- Identify probe dynamically via `nrf-probes`; use `fw-flash-54l15`.
- Capture console before reset/flash per AGENTS.
- Set up the autonomous hci0 central exactly per the AGENTS central-only
  rule; verify `powered le secure-conn cis-central` and the dongle
  address `C0:AA:BB:CC:DD:EE`; use the receiver's direct peer address
  from the boot log if scan ambiguity.
- Prove moved diagnostic and acceptance command groups on the production
  build: `flpr status`, `flpr offload`, `flpr runtime`, `flpr ring
  status`, plus focused `flpr hang` gate and `flpr ring stall*` gate
  runs with a bounded duration sufficient for threshold/recovery (Mode A
  minimum; Mode B only if required).
- Raw logs/evidence under a unique `/tmp/r4-*` directory; preserve exact
  commands and manifest hashes in the results.
- Pass: command paths exist, parser fixtures match runtime, hang
  ACK/recovery/ACTIVE/probation/fault checks pass, stall recovery passes,
  no actionable UART warnings/errors.  No audible-sound question needed.

## Out of scope

R5 decomposition; production C API/behavior changes; command
renames/output changes; FLPR-image acceptance gating (R8); deleting
underlying acceptance APIs; full hardware 4-stream matrix; destructive
recovery/mass erase; BSim repin.

## Escalation conditions

Escalate instead of guessing if: two attempts fail; cross-TU shell macros
contradict verified NCS behavior; output compatibility requires behavior
change; coverage aggregate regresses unexplained; warnings cannot be
classified; BSim pins differ; hardware unavailable after non-destructive
diagnosis; destructive recovery needed; tests need weakening; scope
expansion or architecture decision arises.  Preserve the worktree, do not
commit knowingly failing partial work, report exact blocker/evidence/
status/one question.
