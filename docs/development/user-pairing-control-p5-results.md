# P5 results — lifecycle, disconnect restart, and shell integration

Accepted: 2026-08-07.  Base commit `be18b60` (P4 accepted); handoff
commit `37e7398` (`docs: record P5 handoff — lifecycle and shell
integration`); implementation commit `b1885d2`; coverage migration
commit `94c2742`; acceptance commit (this document's commit).
Handoff: `docs/development/user-pairing-control-p5-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P5 completes the full-stack integration under `CONFIG_USER_PAIRING_INPUT`
while keeping every feature-off production build byte-for-byte
behavior-equivalent (nRF5340, nRF54L15, dongle):

- the pairing-mode controller (`src/pairing_mode.c`, P1) now owns the
  idle-disconnect advertising restart — NORMAL/IDLE and BONDING/IDLE
  disconnects with a real prior connection restart advertising exactly
  once through the injected `advertising_start` operation, stale or
  duplicate disconnects (`was_connected == false`) are a no-op, a
  restart failure is fatal through the dedicated
  `OP_IDLE_RESTART_ADVERTISING` context, and no mode/access/LED/
  generation state is mutated during an idle restart;
- `src/main.c` under the gate wires the accepted fatal boot order's
  final adapter to `pairing_control_start` (pairing_mode_init →
  user_pairing_io_init → bt_bap_pairing_notifications_enable →
  pairing_mode_start, returning the first exact errno; the notification
  gate never opens after an init failure; `pairing_cold_reboot(void
  *ctx)` ignores the context and calls `sys_reboot(SYS_REBOOT_COLD)`)
  and replaces the legacy disconnect-wait/restart loop with a passive
  forever sleep — callbacks → controller notifications own restart, and
  the feature-off branch remains the byte-for-byte legacy loop;
- `src/bt_shell.c` under the gate routes `bt unpair` through
  `pairing_mode_request_reset_sync(K_MSEC(CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS))`
  with exact feature-on text (`Pairing reset complete: bonds cleared;
  BONDING advertising active.` on success, `pairing_mode reset failed:
  <errno>` otherwise, exact result returned, `-ETIMEDOUT` printed as an
  error without claiming success or re-issuing a reset); the feature-off
  command keeps calling `bt_bap_pairing_reset()` with the exact
  historical output consumed by the BlueZ/WirePlumber gate fixtures;
- `Kconfig` gains `USER_PAIRING_SHELL_RESET_TIMEOUT_MS` (default 15000,
  range 1000–120000, `depends on USER_PAIRING_INPUT && SHELL`);
- `src/bt_bap.h` documents `bt_bap_pairing_reset()` as the LEGACY
  feature-off path; the full-stack main/shell/controller has zero
  references to it (structurally proven by the new shell suite, which
  does not link the symbol).

Production configs remain disabled on both boards (P6 enables the
feature only on nRF54L15), so all runtime behavior and every BSim pin
are unchanged.

## Files and public API

- `src/pairing_mode.c` — the DISCONNECTED branch of `step()` now
  captures `was_connected` before clearing status: wait-phase
  completions are unchanged, `!was_connected` is a no-op, and
  NORMAL/IDLE + BONDING/IDLE with a real prior connection call the
  injected `advertising_start()` exactly once with
  `OP_IDLE_RESTART_ADVERTISING` as the failure context.  No new public
  API; `enum pairing_op` gains the dedicated operation id for the fatal
  log.
- `src/main.c` — `pairing_control_start()` + `pairing_cold_reboot(void
  *ctx)` under `CONFIG_USER_PAIRING_INPUT`; `app_lifecycle_ops
  .advertising_start` selects `pairing_control_start` only under the
  gate (else the legacy `advertising_start` → `bt_bap_restart_advertising`
  adapter); passive `k_sleep(K_FOREVER)` main loop under the gate.
  Watchdog→BT→settings→volume→BAP→sink→platform→final-access-start
  order and the accepted `app_lifecycle` coordinator are untouched.
- `src/bt_shell.c` — two compile-time command bodies (feature-on
  synchronous controller reset, feature-off legacy reset), exact
  text/return contract per the handoff; `AUDIO_SHELL_TEST` seam
  unchanged.
- `Kconfig` — `USER_PAIRING_SHELL_RESET_TIMEOUT_MS`.
- `src/bt_bap.h` — legacy documentation only.

## Tests

- `tests/unit/pairing_mode` — direct production-source suite grew
  32 → 37 tests (the 32 existing tests are unchanged or trivially
  reformatted):
  - `test_normal_idle_disconnect_restarts_once` — NORMAL/IDLE real-peer
    disconnect restarts advertising exactly once, no mode/access/LED/
    generation mutation;
  - `test_bonding_idle_disconnect_restarts_once` — BONDING/IDLE
    real-peer disconnect restarts OPEN advertising exactly once;
  - `test_stale_disconnect_noop` — duplicate/stale disconnect with no
    prior connection never executes ops in any idle mode;
  - `test_idle_disconnect_restart_failure_reboots_once` — failed idle
    restart is fatal via the dedicated operation context: exactly one
    cold reboot, no access/generation mutation, only the best-effort
    fatal LED force, order failed-restart → fatal-LED → reboot;
  - `test_wait_phase_disconnect_no_double_start` — BONDING and RESET
    wait-phase completions each perform exactly one advertising start;
  - `test_duplicate_disconnect_harmless` re-scoped: first real
    disconnect restarts once, duplicate is a no-op.
- `tests/unit/bt_shell_pairing` — NEW feature-on shell suite (5 tests)
  compiling the real production `src/bt_shell.c` under
  `CONFIG_USER_PAIRING_INPUT` through the real Zephyr dummy backend +
  `shell_execute_cmd()`, with a fake link implementation of
  `pairing_mode_request_reset_sync()` recording the exact timeout
  argument; `bt_bap_pairing_reset()` is deliberately NOT linked (a
  structural guarantee the feature-on path never calls the legacy API).
  Proves: exact configured `USER_PAIRING_SHELL_RESET_TIMEOUT_MS`
  timeout argument, success text + result, `-ETIMEDOUT` error text with
  no second reset, another exact errno (`-EACCES`), one call only, no
  legacy API text, and wrapper-seam dispatch equivalence.
- `tests/unit/app_lifecycle` — unchanged, 13 tests pass (main wiring is
  compile/build verified).

Focused verification (all on native_sim/native/64):
`/tmp/user-pairing-p5-mode` (pairing_mode) — **37 PASS / 0 FAIL**;
`/tmp/user-pairing-p5-lifecycle` (app_lifecycle) — **13 PASS / 0 FAIL**;
`/tmp/user-pairing-p5-shell` (bt_shell_pairing) — **5 PASS / 0 FAIL**.

## Full-stack scratch build

The CONTROL-only scratch is insufficient to prove main/user-I/O wiring,
so a full-stack scratch target was compiled AND linked: the production
nRF54L15 sysbuild (`nrf54l15dk/nrf54l15/cpuapp` app + FLPR image) with
`CONFIG_USER_PAIRING_INPUT=y` + `CONFIG_USER_PAIRING_CONTROL=y` and a
scratch-only overlay supplying valid `user-button = &button0` /
`user-led = &led0` aliases and disabling the inherited DK
button1/button2/button3 children (the P6 contract: P1.08/P1.09 collide
with the XIAO UART20 bridge when gpio-keys is enabled).
`/tmp/user-pairing-p5-scratch` — exit 0, zero warnings.  Resolved
`zephyr.dts` shows both aliases; the app `.config` shows
`CONFIG_USER_PAIRING_CONTROL=y`, `CONFIG_USER_PAIRING_INPUT=y`,
`CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000`; the app map contains
`pairing_control_start`, `bt_bap_pairing_notifications_enable`,
`user_pairing_io_init`.  Scratch-only RAM trims
(`USER_PAIRING_WORKQ_STACK_SIZE=1024`, acceptance diagnostics off) were
needed to fit the 160 KB cpuapp SRAM budget — noted for P6, which owns
the real board memory budget.

## Coverage migration (same 36 files)

Candidate generated on the clean implementation commit `b1885d2` via
`scripts/test-coverage.sh --write-baseline /tmp/p5-baseline-candidate.json`
(`--output /tmp/p5-cov-candidate --clean-output`), inspected, and
committed byte-exact as `tests/coverage-baseline.json` (commit
`94c2742`).  Tool versions unchanged (gcovr 8.4 / gcov (GCC) 14.3.0).
**Population stays 36** — no new production source file (P5 only
changed two already-populated files):

| file | metric | committed | candidate |
|------|--------|-----------|-----------|
| `src/bt_shell.c` | lines | 6/6 | **11/11** |
| | branches | 2/2 | **4/4** |
| | functions | 1/1 | 1/1 |
| `src/pairing_mode.c` | lines | 344/405 | **354/414** |
| | branches | 160/226 | **171/236** |
| | functions | 39/39 | 39/39 |
| aggregate (36 files) | lines | 4650/5107 (91.1%) | **4665/5121 (91.1%)** |
| | branches | 2010/2808 (71.6%) | **2023/2820 (71.7%)** |
| | functions | 357/357 | **357/357** |

Every unchanged file stays at or above its committed record
(programmatic check: zero decreases), no population drift, zero-hit
enforcement clean (357/357 — every production function in the numeric
population executes; the new idle-restart branches and the feature-on
shell branch are covered by the new tests).  Provenance:
`docs/testing/coverage-matrix.md` "P5 baseline migration".

## Canonical gate (G1)

`./scripts/test-all.sh` on clean `94c2742` → **59 PASS / 0 FAIL /
59 TOTAL**, exit 0 (35 twister + 5 exec-only + 16 Python + coverage +
matrix + BSim Stage 1; log `/tmp/p5-gate.log`, elapsed 20m11s).  Coverage
child: baseline enforcement **0 errors** against the migrated baseline.
Matrix child: **0 errors, 0 notes**.  BSim Stage 1: all 17 scenarios
strict-checked; every existing pin byte-identical (mono 10 ms
`0x22AB5C0D`, mono 7.5 ms `0x01A3EB05`, Mode A/B 10 ms `0xBAE24F7E`,
Mode A/B 7.5 ms `0x2D95D15C`/`0xFF82CADB`, modea_one_cis_loss_10ms
`0x30D6BAF0`, release_without_disable/duplicate_release_10ms
`0xAEBD23A1`, disconnect_streaming/reconnect_second_stream_10ms
`0x8500C966` with the reconnect second segment equal to a fresh mono
oracle).  `git diff --check` clean.

## Builds and build contract

- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
  only the documented pre-existing NCS v3.3.0 diagnostics
  (PARTITION_MANAGER deprecation, SW Split experimental symbols incl.
  the `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice notice,
  FLPR-image `UART_CONSOLE` assigned-but-got) — zero new/actionable
  warnings and zero warnings from any P5-changed file.
- Build contract **79/79** (`python3 scripts/check-build-contract.py
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` → 79 assertions,
  0 failed, BUILD CONTRACT PASSED).
- The three warning instances inside the canonical gate log
  (`fake_flpr_deps.c` enum/int mismatch, BT_CONN_TX_MAX and
  BT_ISO_TX_BUF_COUNT assigned-but-got in a native suite build without
  Bluetooth) are each present 1× in the P3 and P4 gate logs too —
  pre-existing test-build diagnostics in unchanged files, not
  introduced by P5.

## Deviations and notes

- **No amend of the handoff commit** (`37e7398` untouched) and no amend
  of the coverage commit (policy); the provenance section lives in this
  acceptance commit as in P4.
- The P5 handoff's "Feature-off branch remains byte-for-byte" is
  verified at the source level: the legacy main loop and the legacy
  shell branch are compiled verbatim under `#else` (a `git diff` shows
  only the `#if`/`#else`/`#endif` guards around unchanged code), and
  both feature-off suites (audio_shell etc.) pass unchanged.
- The idle-restart fall-through case (a disconnect with a prior
  connection while not in a wait phase and not IDLE) is unreachable in
  the current state machine (RESETTING exists only in
  WAIT_DISCONNECT_FOR_RESET / RESET_FEEDBACK phases); it is not
  separately tested and is recorded in the baseline denominator.
- No P6 board enablement/overlay edits, no advertising payload
  differentiation, no audio/BAP stream lifecycle changes, no hardware
  tests (P8) — all per the handoff scope.
- The pre-existing stale timing comment in
  `tests/unit/pairing_mode/CMakeLists.txt` (an earlier iteration's
  values) was corrected to the actual compile definitions while the
  suite was being updated.

## Next-phase grounding

P6 owns the real XIAO mapping (`user-button` P0.00 / `user-led` P2.00
per the plan's devicetree portability contract, disabled inherited DK
buttons), the nRF54L15 board-conf enablement of
`CONFIG_USER_PAIRING_CONTROL` + `CONFIG_USER_PAIRING_INPUT`, and the
cpuapp SRAM budget (the scratch build needed
`USER_PAIRING_WORKQ_STACK_SIZE=1024` plus acceptance-diagnostics-off to
fit 160 KB); P8 runs the hardware acceptance matrix.  The full-stack
scratch overlay/conf used here (`/tmp/opencode/p5-scratch.{overlay,conf}`)
are deliberately uncommitted.
