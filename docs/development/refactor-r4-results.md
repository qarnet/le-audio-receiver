# R4 results — shell and acceptance-harness separation

Accepted: 2026-08-05.  Start commit `3a83c5b` (R3 docs acceptance; worktree
clean); handoff commit `128c4bc`; implementation commit `39c318a`; coverage
migration commit `b82ab81`; docs commit (this document's commit).  No
production C API, command name, help string, arg count, output, or return
behavior changed; no BSim pins changed.

## Commits

1. `128c4bc` — `docs: record R4 handoff — shell/acceptance-harness split`
   (`docs/development/refactor-r4-handoff.md`).
2. `39c318a` — `refactor: split shell command ownership by subsystem`
   (four shell TUs, `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` Kconfig + board
   conf, test CMake updates incl. the config-off absence test,
   `tests/test-matrix.json` four-owner records).
3. `b82ab81` — `coverage: migrate baseline after R4 shell split`
   (population 26 → 29, candidate copied byte-exact from
   `--write-baseline` output generated on clean `39c318a`;
   `docs/testing/coverage-matrix.md` R4 migration section).
4. docs-only commit — `docs: accept R4 shell/acceptance-harness split`
   (this document + plan/STATUS/README/workstation-transfer markers).

## What changed

- `src/audio_shell.c` retains only `audio status/reset-stats/stop/perf/
  perf-reset`, `RESAMPLER_NAME`, `perf_path_names`, the `audio_cmds` set
  and registration, and its five `AUDIO_SHELL_TEST` seams.
- New `src/bt_shell.c` (`CONFIG_SHELL`, both targets): `bt unpair`, the
  `bt` registration, and the existing-named seam
  `audio_shell_test_cmd_bt_unpair`.
- New `src/flpr_shell.c` (`CONFIG_SHELL && CONFIG_SOC_NRF54L15`):
  `flpr status/offload/runtime/restart`, the string helpers, the sole
  `SHELL_CMD_REGISTER(flpr, ...)`, and its four seams, registered with
  NCS v3.3.0 cross-TU section macros (`SHELL_SUBCMD_SET_CREATE` /
  `SHELL_SUBCMD_ADD`, verified against `zephyr/include/zephyr/shell/
  shell.h` 507–561 and the kernel_shell/net_shell/shell_module users).
- New `src/flpr_acceptance_shell.c` (only
  `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`): `flpr stress`, `flpr hang`, all
  `flpr ring *` commands, `acceptance_buf`/`acceptance_rs`/macros, and the
  ring-status seam.  Owns the nested `flpr_ring_cmds` set and the parent
  `ring` addition; adds `ring`/`stress`/`hang` to the root set from a
  different TU (cross-TU section registration proven on native_sim and on
  ARM).
- Root Kconfig `AUDIO_ACCEPTANCE_DIAGNOSTICS` (`bool`, `default n`,
  `depends on SOC_NRF54L15 && SHELL`); enabled only in
  `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.  nRF5340 and any non-
  acceptance nRF54L15 build never compile the acceptance TU.
- Tests: `audio_shell`/`audio_shell_noperf` compile `bt_shell.c`;
  `audio_shell_nrf54` compiles all four TUs with
  `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=1`; new
  `test_flpr_acceptance_absent_config_off` in `audio_shell` proves the
  acceptance commands do not resolve when the TU is not compiled.  All
  seam blocks remain `GCOVR_EXCL_START`/`STOP`.

## Focused results (G0)

- `audio_shell`: **14 PASS / 0 FAIL** (13 prior + config-off absence).
- `audio_shell_noperf`: **10 PASS / 0 FAIL**.
- `audio_shell_nrf54`: **42 PASS / 0 FAIL** (public shell execution of all
  four TUs through the real dummy-backend registry — cross-TU section
  registration proven).
- `flpr_hang_gate` Python: **21 PASS / 0 FAIL**; `flpr_stall_gate` Python:
  **30 PASS / 0 FAIL** (fixtures unchanged).
- Matrix checker on the R4 tree: **0 error(s), 0 note(s)**.
- Coverage report-only: aggregate of the four replacement files equals the
  old `audio_shell.c` record exactly (see migration table below).
- `git diff --check` clean; zero compiler warnings in the focused builds.

## Coverage migration (population 26 → 29)

Generated on clean `39c318a` via
`scripts/test-coverage.sh --write-baseline /tmp/r4-baseline-candidate.json`
(gcovr 8.4 / gcov (GCC) 14.3.0), inspected, then copied byte-exact into
`tests/coverage-baseline.json` in `b82ab81`.  Full provenance and the
old→new tables: `docs/testing/coverage-matrix.md` ("R4 baseline
migration").

| File | lines | branches | functions |
|------|-------|----------|-----------|
| `audio_shell.c` (old monolithic) | 302/519 | 124/274 | 23/23 |
| `audio_shell.c` (audio only) | 51/51 | 12/12 | 5/5 |
| `bt_shell.c` | 6/6 | 2/2 | 1/1 |
| `flpr_shell.c` | 95/134 | 34/56 | 7/7 |
| `flpr_acceptance_shell.c` | 150/328 | 76/204 | 10/10 |
| **Sum of the four replacements** | **302/519** | **124/274** | **23/23** |

Aggregate ratio change: **none** (exactly equal).  Totals unchanged at
3505/3946 lines, 1467/2067 branches, 209/209 functions.  Per-file ratio
movement is pure compiler/config attribution (the audio-only TU became
100% covered; the acceptance gates that need physical FLPR transport
remain hardware-evidence-covered lines).  Zero-hit functions remain
forbidden and none exist (23/23 executed).

## G1 results (clean baseline commit `b82ab81`)

- `./scripts/test-all.sh` — **47 PASS / 0 FAIL / 47 TOTAL** (28 twister +
  4 exec-only + 12 Python + coverage + matrix + BSim), elapsed **16 m 40 s**
  (bash `time` builtin), exit 0, log `/tmp/r4-gate-final.log` (transient,
  not repo-retained).  Coverage child: **numeric population files: 29,
  baseline enforcement: 0 error(s), PASS** against the committed
  `tests/coverage-baseline.json`.  Matrix child: **0 error(s), 0 note(s)**.
  BSim Stage 1: **PASS — all scenarios strict-checked** (pins unchanged).
- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — all PASS (exit 0).
  Warning inventory identical to the documented pre-existing NCS
  diagnostics (STATUS.md "Build warning diagnostics"): PARTITION_MANAGER
  deprecation (5340/dongle), experimental BT_LL_SW_SPLIT /
  BT_CTLR_SET_HOST_FEATURE / BT_CTLR_PERIPHERAL_ISO, SW Split
  CONN_ISO_LOW_LATENCY_POLICY choice, nRF54L15 reserved-memory
  `simple_bus_reg` / `avoid_unnecessary_addr_size`, FLPR UART_CONSOLE
  assigned-value, CMake "No SOURCES given" (watchdog), `__ASSERT()`
  informational.  **Zero new/actionable warnings.**
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` — **76 assertions, 0 failed, BUILD CONTRACT
  PASSED**.
- `git diff --check` — clean.  Worktree clean.

## Hardware smoke (nRF54L15 only; no mass erase / recovery)

Evidence: `/tmp/r4-hw-evidence/` (raw logs + `MANIFEST.md` +
`SHA256SUMS`):

```
87eb1ade8b51d2c28246ddbfe96c456daedfe8b7e8ccae153f41c0e0e7a4aa81  bap-central-stall.log
e05362fe9501bc19b1315c586ec50fa3689dc8aaa0807cbb37d9cabe69bb82a0  flpr_hang_gate_mono_120s.log
f6cf7622056196e185332bda2c8d2bf717448570d5170189c88f40935fc19a03  hang-gate-console.log
4e6336dd7c3d0a3e3a4b6de4076035884772271eef38f5cddcb1208625f79543  stall-gate.log
```

Probe resolved at flash time via `nrf-probes` (XIAO CMSIS-DAP
`8EE9B3FF`, DPIDR `0x6ba02477`, PART `0x00054b15`, VARIANT `AAC0`);
`fw-flash-54l15` verified 518140 bytes + FLPR image 32330 bytes.  Console
captured before/after flash per AGENTS; boot clean: `BLE ready`,
`settings_load() OK`, `Identity: DB:A6:0C:05:A2:AA (random)`, `FLPR READY
(epoch=2663605661, count=1, new)`, rings at 481-frame capacity, `offload
init OK (rings ready)`, `Advertising as "LE Audio Receiver"`.

Central per the AGENTS central-only rule: hci0 over `/dev/ttyACM2` @
1,000,000 baud H4; `btmgmt info` verified **addr C0:AA:BB:CC:DD:EE** and
**current settings: powered le secure-conn cis-central**; receiver peer
pinned via `--peer-addr DB:A6:0C:05:A2:AA`.

Command-path proof on the production build (moved groups):
- `flpr status` → `--- FLPR handshake ---` Ready/ACKed/Healthy yes, zero
  errors.
- `flpr ring status` → `--- FLPR PCM rings ---` Initialized yes
  (acceptance TU).
- `flpr offload` → `--- Audio offload ---` + ASRC section.
- `flpr runtime` → `--- FLPR runtime ---` incl. DMCONTROL/INITPC/CPURUN.
- `flpr` (bare) → help lists all seven subcommands (hang, offload,
  restart, ring, runtime, status, stress) — cross-TU section registration
  proven on ARM.

Gates:
- **Hang gate PASSED** (`flpr hang`, Mode A, 120 s):
  `nix-shell -p python3Packages.pyserial --run "python3
  scripts/flpr_hang_gate.py --port /dev/ttyACM0 --duration 120
  --peer-addr DB:A6:0C:05:A2:AA"` — Injection → ACK **150 ms**,
  Injection → ACTIVE **852 ms**, all 16 checks PASS (ack_received,
  asrc_fallback_triggered, asrc_state_zero, asrc_verify_zero, crc_zero,
  epoch_changed, exhaustion_zero, frame_count_plausible, frame_zero,
  probation_cleared, recovery_attempts_eq_1, relapses_zero,
  runtime_fails_zero, runtime_restarts_eq_1, seq_zero, state_active);
  12000 frames in 120.00 s (100.0 fps); final counters submit=12031
  success=11985 fallback=46, recovery attempts=1 fail=0, probation
  cleared=1, runtime restarts=1 fails=0.
- **Stall gate PASSED** (`flpr ring stall_flpr_ms`, timed 60 ms):
  bap_central Mode A 150 s launched alongside (`run_stall_gate.py`
  orchestrator); `flpr_stall_gate.py --timeout 60` — GATE PASSED,
  injection → ACK **103 ms**, baseline_success=523; observed recovery
  chain in the raw log: ACTIVE → stall ACK `bits=0x01 duration=60 ms` →
  RECOVERING (`Last err : -116`) → epoch change → ACTIVE with
  probation active (success climbing 530 → 600), recovery attempts=2
  fail=0, relapses=0, exhaustion=0.

Pairing note (pre-existing workflow, no firmware defect): the first hang-
gate attempt failed with `AuthenticationFailed` from a stale receiver-side
bond after the central removed its own cache; resolved with the moved
`bt unpair` command on the receiver (bond-clearing, open pairing) +
`bluetoothctl remove` on the central — the accepted no-mass-erase path.
`bt unpair` therefore received real-hardware validation during this phase.

UART warning disposition: zero `i2s_nrfx` underruns, zero decode errors,
zero stream resets, zero asserts in all gate logs; stream summaries show
`decode_err=0 i2s_underrun=0 stream_reset=0` (plc=1418 during the stall
injection, as designed).  The only `<wrn>` lines are the designed
recovery-escalation diagnostics (`RING_RESET_ACK timeout (100 ms)` /
`short ring reset failed (-116), escalating to runtime restart`) whose
outcomes the gates' recovery predicates validate.  Not actionable.

## Deviations / blockers

None blocking.  Process notes:

- The first two hang-gate runs failed on the stale-bond pairing issue
  described above (hardware-session state, not firmware); fixed with the
  sanctioned `bt unpair` path and re-run clean.
- Editor-side: the Write/Edit tooling runs clang-format on save, which
  rewrites the hyphenated shell syntax tokens `reset-stats`/`perf-reset`
  in `SHELL_CMD_ARG(...)` as `reset - stats`/`perf - reset` (breaking the
  command names); the committed file restores the byte-exact tokens and
  the diff was verified.  This is a formatting-tool behavior, not a repo
  policy change; the committed source matches the original tokens exactly.
- `src/flpr_shell.c` is added via `if(CONFIG_SHELL AND
  CONFIG_SOC_NRF54L15) target_sources(app ...)` while the other shell TUs
  use `zephyr_sources_ifdef` — per the handoff decision
  (`zephyr_sources_ifdef` accepts a single condition).  Both paths link
  into the final image; all four TUs are verified present in the nRF54L15
  build.

## Non-scope items untouched

R5 decomposition; production C API/behavior changes; command
renames/output changes; FLPR-image acceptance gating (R8); deleting
underlying acceptance APIs; full hardware 4-stream matrix; destructive
recovery/mass erase; BSim repin.
