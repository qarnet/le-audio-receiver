# P7 results — software and build acceptance

Accepted: 2026-08-07.  Base commit `c6b338b` (P6 accepted plus review
fix); handoff commit `7e44d61` (`docs: record P7 handoff — software
and build acceptance`); acceptance commit (this document's commit).
Handoff: `docs/development/user-pairing-control-p7-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P7 independently reruns and records the complete software/build
acceptance on the exact production code now that P1–P6 are integrated
and nRF54L15 is feature-enabled.  No production code, API, DT, Kconfig,
test logic, baseline, or BSim pin changed — P7 is evidence-only.

## 1. Focused direct suites (real production sources)

All suites built and executed on `native_sim/native/64` exactly as the
canonical gate runs them; the build-contract Python suite runs
directly.  Zero warnings from any changed/paired file.

| Suite | Result |
|---|---|
| `tests/unit/pairing_mode` | **37 PASS / 0 FAIL** |
| `tests/unit/user_pairing_io` | **21 PASS / 0 FAIL** |
| `tests/unit/bt_pairing_policy` | **23 PASS / 0 FAIL** |
| `tests/unit/bt_bap_pairing_adapter` | **37 PASS / 0 FAIL** |
| `tests/unit/bt_shell_pairing` | **5 PASS / 0 FAIL** |
| `tests/unit/app_lifecycle` | **13 PASS / 0 FAIL** |
| `tests/unit/build_contract` (Python) | **51 tests** (`def test_` count) — "95 assertions, 0 failed / BUILD CONTRACT PASSED", exit 0 |

Build dirs: `/tmp/p7-pairing-mode`, `/tmp/p7-user-pairing-io`,
`/tmp/p7-bt-pairing-policy`, `/tmp/p7-bt-bap-adapter`,
`/tmp/p7-bt-shell-pairing`, `/tmp/p7-app-lifecycle` (transient).

## 2. Explicit coverage report-only and baseline enforcement

`scripts/test-coverage.sh --report-only --output /tmp/p7-cov-report
--clean-output` (fresh run of all 40 native C suites with
`CONFIG_COVERAGE=y`):

- **numeric population 36 files** (exact — no population drift vs the
  committed baseline);
- every production function hit: **numeric functions 357/357 (100.0%)**,
  and a per-file scan of the numeric population reports **zero
  zero-hit files**;
- numeric totals: lines 4665/5121 (91.1%), branches 2023/2820 (71.7%),
  functions 357/357 (100.0%);
- **baseline comparison vs the committed `tests/coverage-baseline.json`:
  zero errors** — no unchanged-file decrease, no overall or per-file
  ratio weakening, population sets identical (36 ↔ 36, no missing, no
  extra);
- gcovr 8.4 / gcov (GCC) 14.3.0 (run-manifest records the exact
  versions).

The canonical gate's own coverage child re-ran the same enforcement on
the clean commit and reported **baseline enforcement: 0 error(s) /
PASS** — the authoritative enforcement run.

## 3. Matrix checker

`python3 scripts/check-test-matrix.py --repo-root . --coverage-json
/tmp/p7-cov-report/coverage.json` → **0 error(s), 0 note(s)**, exit 0
(zero-hit function enforcement, public API inventory, outcome ledger).
The canonical gate's matrix child independently reported the same
**0 errors, 0 notes**.

## 4. Canonical gate

`./scripts/test-all.sh` on the clean handoff commit `7e44d61`
(worktree clean, `/tmp/p7-gate.log` transient): **59 PASS / 0 FAIL /
59 TOTAL**, exit 0.

Exact child composition (order as executed):

- **35 twister** C suites (testcase.yaml): actuator_apll,
  actuator_apll_nohfclk, actuator_none, actuator_sample_adjust_historical,
  app_lifecycle, asrc, audio_i2s, audio_i2s_identity, audio_shell,
  audio_shell_noperf, audio_shell_nrf54, audio_stream_session,
  bt_bap_pairing_adapter, bt_pairing_policy, bt_shell_pairing, decode,
  drift, flpr_acceptance, flpr_acceptance_flpr, flpr_handshake,
  flpr_protocol, flpr_ring_mgr, flpr_runtime, iso_seq, lifecycle, modea,
  pairing_mode, perf, rate_convert, stats, timing, timing_none,
  timing_nrf54, user_pairing_io, volume;
- **5 exec-only** C suites: audio_offload, flpr_audio_process, flpr_ring,
  offload_asrc, offload_asrc_verify;
- **16 Python** suites: bap_central_device, bap_central_endpoint,
  bap_central_policy, bap_central_security, bap_central_session,
  bap_central_writer, bsim_runner, build_contract, flpr_hang_gate,
  flpr_stall_gate, fw_flash_dongle, hci_raw_connect, test_coverage_runner,
  test_matrix, bluez_wireplumber_gate, bluez_wireplumber_phase3_gate;
- **coverage**: native suites + baseline — population 36, **0 errors**,
  PASS;
- **matrix**: manifest + coverage.json — **0 errors, 0 notes**;
- **bsim: stage1**.

### Complete BSim Stage 1 pins / scenario count

All **17 scenarios** strict-checked; scenarios 1–9 run twice and 10–17
once (**26 runs**); every pinned hash **byte-identical** to
`tests/bsim/stage1-scenarios.json`:

| Scenario | Runs | full | left | right |
|---|---|---|---|---|
| mono_10ms | 2 | `0x22AB5C0D` | `0x32777D65` | `0x32777D65` |
| mono_7p5ms | 2 | `0x01A3EB05` | `0x30F0308C` | `0x30F0308C` |
| modea_10ms | 2 | `0xBAE24F7E` | `0x32777D65` | `0xD3EE3722` |
| modea_7p5ms | 2 | `0x2D95D15C` | `0xE1D60E7B` | `0xA219B61E` |
| modea_reverse_start_10ms | 2 | `0xBAE24F7E` | `0x32777D65` | `0xD3EE3722` |
| modeb_10ms | 2 | `0xBAE24F7E` | `0x32777D65` | `0xD3EE3722` |
| modeb_7p5ms | 2 | `0xFF82CADB` | `0x30F0308C` | `0x129591EE` |
| invalid_sdu_resume_10ms | 2 | `0x0C61918D` | `0x7FFE087A` | `0x7FFE087A` |
| modea_one_cis_loss_10ms | 2 | `0x30D6BAF0` | `0x32777D65` | `0x9859F1D8` |
| modea_first_stop_10ms | 1 | `0x5A025240` | `0xE89CBFDA` | `0xCB0E8DA4` |
| release_without_disable_10ms | 1 | `0xAEBD23A1` | `0x76261278` | `0x76261278` |
| disconnect_streaming_10ms | 1 | `0x8500C966` | `0x5E9679E1` | `0x5E9679E1` |
| reconnect_second_stream_10ms | 1 | `0x8500C966` | `0x5E9679E1` | `0x5E9679E1` |
| unsupported_source_direction | 1 | `0x00000000` | `0x00000000` | `0x00000000` |
| no_free_sink_slot | 1 | `0x00000000` | `0x00000000` | `0x00000000` |
| invalid_codec_fields | 1 | `0x00000000` | `0x00000000` | `0x00000000` |
| duplicate_release_10ms | 1 | `0xAEBD23A1` | `0x76261278` | `0x76261278` |

`=== STAGE1 (T4 matrix) PASS — all scenarios strict-checked ===`.

## 5. Fresh builds (exact commit `7e44d61`)

| Build | Command | Exit | Artifacts |
|---|---|---|---|
| nRF5340 | `fw-build-5340` | 0 | merged.hex / merged_CPUNET.hex; app FLASH 375364 B (36.37%), RAM 145256 B; net FLASH 146780 B, RAM 40512 B |
| nRF54L15 | `fw-build-54l15` | 0 | app FLASH 531668 B (36.36%), RAM 161036 B (98.29%); FLPR RAM 43632 B (66.58%) |
| Dongle | `fw-build-dongle` | 0 | merged.hex (112820 B), merged_CPUNET.hex, hci_uart + hci_ipc hexes |

## 6. Build contract — 95/95 against fresh artifacts

`python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
--nrf54l15 build/nrf54l15` → **95 assertions, 0 failed, BUILD CONTRACT
PASSED**, exit 0.  Same invocation the P6 acceptance recorded (the
Python suite's 51 tests use fixtures; the direct CLI run on the fresh
real artifacts is the authoritative check).

## 7. nRF54L15 resolved config / DT / map audits

All from the fresh `build/nrf54l15/le-audio-receiver` artifacts
(resolved, not overlay source).

**Full pairing stack + INPUT enabled** (resolved `.config`):
`CONFIG_USER_PAIRING_CONTROL=y`, `CONFIG_USER_PAIRING_INPUT=y`,
`CONFIG_INPUT=y` (mandatory dependency), `CONFIG_INPUT_GPIO_KEYS=y`,
`CONFIG_USER_PAIRING_DEBOUNCE_MS=30`, `CONFIG_USER_PAIRING_BOND_HOLD_MS
=3000`, `CONFIG_USER_PAIRING_RESET_HOLD_MS=8000`,
`CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1024`,
`CONFIG_USER_PAIRING_WORKQ_PRIORITY=5`,
`CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000`,
`CONFIG_HEAP_MEM_POOL_SIZE=0`.

**Exact P0.00/P2.00 aliases / flags / debounce** (resolved
`zephyr.dts`): `user-button = &button0`; `button0` gpios
`<&gpio0 0 0x11>` = `GPIO_ACTIVE_LOW|GPIO_PULL_UP`, `zephyr,code =
<0xb>` = `INPUT_KEY_0`, gpio-keys parent `debounce-interval-ms = <0x1e>`
(30); `user-led = &led0`; `led0` gpios `<&gpio2 0 0x1>` =
`GPIO_ACTIVE_LOW`.

**Inherited controls removed/disabled** (resolved `zephyr.dts`):
`button1`/`button2`/`button3` all `status = "disabled"`; `led1`/
`led2`/`led3` nodes absent (`/delete-node/`); gpio-keys enumerates only
the one status-okay child, so UART20 P1.08/P1.09 can never be claimed.
Pin-overlap audit: P0.00's only claimant is `button0`; P2.00's two
references are `led0` (enabled) and `mx25r64` `reset-gpios` — the
mx25r64 node is `status = "disabled"` (overlay line 190), so it claims
nothing.

**RAM used/free and heap/workqueue config** (resolved `zephyr.map` +
linker report): RAM region `0x20000000` length `0x28000` (163840 B);
`_image_ram_end = 0x2002750c` → used **161036 B**, free **2804 B**
(0xAF4) to the shared-memory boundary `0x20028000`; heap `0`;
pairing work-queue stack 1024 (resolved `g_pairing_stack` symbol,
`CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1024`); `sys_work_q_stack`
present.  `net_buf_heap_cb` **not linked** (0 occurrences — the
system-heap consumer is not compiled with heap 0); shell history uses
its own dedicated `CONFIG_SHELL_HISTORY_BUFFER` heap (allowed).  All
byte counts identical to the P6 acceptance.

**Expected full-stack symbols linked** (resolved ELF,
`arm-zephyr-eabi-nm`): `pairing_control_start`, `user_pairing_io_init`,
`user_pairing_io_led_set`, `bt_bap_pairing_notifications_enable`,
`pairing_mode_init`, `pairing_mode_request_bonding`,
`pairing_mode_request_reset`, `pairing_mode_request_reset_sync`, the
gpio-keys driver (`gpio_keys_init`/`gpio_keys_interrupt`), and the
registered input callback (`_input_callback__user_button_cb`) — all
present.

## 8. nRF5340 feature-off resolved proof

Resolved `build/nrf5340/le-audio-receiver/zephyr/.config`:
`# CONFIG_USER_PAIRING_CONTROL is not set`; `CONFIG_USER_PAIRING_INPUT`
absent; `CONFIG_INPUT` not set.  Resolved nRF5340 ELF: the full-stack
pairing symbols (`pairing_control_start`, `user_pairing_io_init`,
`pairing_mode_request_reset_sync`) are **absent**; the legacy feature-off
`bt_bap_pairing_reset` symbol **is** linked — the nRF5340 production
path is unchanged feature-off behavior.

## 9. Warning diff / classification — zero new/actionable

Fresh build logs (`/tmp/p7-build-5340.log`,
`/tmp/p7-build-54l15.log`, `/tmp/p7-build-dongle.log`) and the
canonical gate log (`/tmp/p7-gate.log`) contain **zero compiler
warnings** (0 `[-W...]` in all three build logs) and only the
documented pre-existing NCS v3.3.0 diagnostics:

- PARTITION_MANAGER / PARTITION_MANAGER_ENABLED deprecation notices
  (5340, dongle) + the sysbuild partition-manager WARNING box;
- SW Split experimental-symbol notices (`BT_LL_SW_SPLIT`,
  `BT_CTLR_PERIPHERAL_ISO`, `BT_CTLR_SET_HOST_FEATURE`) and the
  `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice notice (5340);
- FLPR-image `UART_CONSOLE` assigned-but-got (54l15, pre-existing
  FLPR build config);
- watchdog "No SOURCES given" (54l15 — wdt30/wdt31 disabled in DT with
  SDC active; `CONFIG_WATCHDOG=y` creates an empty library; documented,
  not fixable without an unsupported DT node);
- `__ASSERT()` informational notices;
- pre-existing dtc structural notes on `/soc/reserved-memory`,
  `/soc/rram-controller@5004b000/rram@165000`, and
  `/soc/memory@20028000` (nodes untouched by the pairing feature);
- native_sim "Using a test - not safe - entropy source" (test-build
  informational; appears in the gate log only, in test suites).

The canonical gate log's three actionable-looking instances are each
present 1× and are the **exact pre-existing test-build diagnostics
already documented in P3/P4/P5**: `tests/unit/audio_shell_nrf54/src/
fake_flpr_deps.c:189` `[-Wenum-int-mismatch]` (unchanged test file),
and the `BT_CONN_TX_MAX=7` / `BT_ISO_TX_BUF_COUNT=6` assigned-but-got
warnings from `tests/unit/audio_stream_session/prj.conf` (a native
suite without Bluetooth — unchanged test file).  **Zero new/actionable
warnings; zero warnings from any pairing-feature file.**

`git diff --check` clean.

## Commits

1. `7e44d61` — P7 handoff.
2. This acceptance commit (P7 results + STATUS P6/P7 sections).

No production/test/baseline/BSim-pin changes; no coverage migration
(no production C file changed; population 36).

## Deviations and notes

- **STATUS lacked the P6 section** — P6 acceptance (commit `48f6028`)
  added only the results doc.  P7's scope explicitly allows STATUS
  edits, so this closeout adds both the P6 and P7 STATUS sections;
  no historical doc is amended.
- **RAM percentage correction**: P6 results doc records
  `RAM: 161036 B / 160 KB, 96.52%`, but 161036/163840 =
  **98.29%** (the linker/CMake-reported value; byte counts and the
  2804 B free margin are identical to P6).  The P6 acceptance doc is
  not amended (accepted historical evidence); the corrected percentage
  is recorded here and in STATUS.
- The canonical gate's four `FAIL:` grep matches are the
  bluez_wireplumber_gate failure-injection negative-path test names
  (e.g. "Cannot start wireplumber: exec failed") — expected output of
  the passing suite, not gate failures.
- No hardware tests (P8), no nRF5340 enablement, no advertising payload
  differentiation, no audio/FLPR/shared-memory/pin changes — all per
  handoff scope.

## Next-phase grounding

P8 executes the hardware acceptance matrix on the XIAO (button
thresholds, LED patterns, pairing/security, `bt unpair`, streaming
after transitions) and validates the SRAM budget and the 1024-byte
pairing work-queue stack at runtime; AGENTS/README/design updates
follow only after the hardware pass.
