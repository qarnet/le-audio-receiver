# P6 results — XIAO controls, production enablement, and build contract

Accepted: 2026-08-07.  Base commit `04b5010` (P5 accepted); handoff
commit `b246447` (`docs: record P6 handoff — XIAO controls and build
contract`); implementation commit `62b8727`; acceptance commit (this
document's commit).  Handoff:
`docs/development/user-pairing-control-p6-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P6 maps the real Seeed XIAO nRF54L15 user controls through the project
aliases, removes the inherited DK GPIO claims, enables the full pairing
control stack **only** on nRF54L15, solves the cpuapp SRAM budget while
keeping `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y`, and extends the
resolved-artifact build contract to **95 assertions** (79 → 95).

## Exact XIAO control mapping (resolved, not just overlay source)

From the resolved `build/nrf54l15/le-audio-receiver/zephyr/zephyr.dts`
(project overlay provenance shown by the resolved tree):

| Control | Alias | Node | Resolved GPIO | Flags / code | Source |
|---|---|---|---|---|---|
| User button | `user-button` | `button0` (`/buttons/button_0`) | `&gpio0 0` (P0.00) | `0x11` = `GPIO_ACTIVE_LOW \| GPIO_PULL_UP`, `zephyr,code = <0xb>` = `INPUT_KEY_0` | overlay `&button0` override; code inherited from DK (`INPUT_KEY_0`) per handoff |
| User LED | `user-led` | `led0` (`/leds/led_0`) | `&gpio2 0` (P2.00) | `0x1` = `GPIO_ACTIVE_LOW` | overlay `&led0` override |

- `gpio-keys` parent `debounce-interval-ms = <30>` (0x1e, explicit).
- Inherited DK `button1` (P1.09) / `button2` (P1.08) / `button3` (P0.04)
  are `status = "disabled"` — gpio-keys enumerates only status-okay
  children, so the UART20 bridge pins P1.08/P1.09 can never be claimed.
- Inherited DK `led1` (P1.10) / `led2` (P2.07) / `led3` (P1.14) are
  `/delete-node/`d and their dangling `led1`/`led2`/`led3` aliases are
  `/delete-property/`d — gpio-leds uses all children regardless of
  status, and a deleted node label referenced by an alias is a dtc hard
  error (`undefined node label 'led1'`).
- The gpio-leds node now has exactly one child (`led0`); the gpio-keys
  node has exactly one status-okay child (`button0`).
- Pin-overlap audit on P0.00 and P2.00: the only enabled claimants are
  `button0` (P0.00) and `led0` (P2.00).  The other P2.00 reference in
  the tree is `mx25r64` `reset-gpios` — that node is `status =
  "disabled"`, so it claims nothing.  UART20 P1.8/P1.9, I2S20 P1.4–P1.7,
  RF switch P2.3/P2.5, TIMER20, shared SRAM, and oscillator settings are
  untouched.  `sw0` and `mcuboot-button0` now also resolve to the
  physical XIAO button (by design; P2 uses only `user-button`).

## Config changes and rationale (nRF54L15 board conf only)

Added to `boards/nrf54l15dk_nrf54l15_cpuapp.conf`:

```text
CONFIG_USER_PAIRING_CONTROL=y
CONFIG_USER_PAIRING_INPUT=y
CONFIG_INPUT=y
CONFIG_USER_PAIRING_DEBOUNCE_MS=30
CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=15000
CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1024
CONFIG_HEAP_MEM_POOL_SIZE=0        # was 4096
```

- `USER_PAIRING_CONTROL`/`USER_PAIRING_INPUT`: full stack enablement per
  handoff.
- `CONFIG_INPUT=y` — **documented deviation from the handoff's literal
  four-symbol list**: `INPUT` is a Zephyr `menuconfig` with default `n`
  that nothing selects (the gpio-keys driver `INPUT_GPIO_KEYS` only
  `select GPIO`).  Without it Kconfig emits `USER_PAIRING_INPUT ...
  was assigned the value 'y' but got the value 'n'` (unsatisfied
  dependency `INPUT (=n)`) and the handoff's own `INPUT=y` build-contract
  assertion can never pass.  The P2 test suite already required
  `CONFIG_INPUT=y` in its prj.conf; this is the production equivalent.
- `DEBOUNCE_MS=30`, `SHELL_RESET_TIMEOUT_MS=15000`: handoff exact values.
- `WORKQ_STACK_SIZE=1024`: the smallest **build-defensible** value
  (Kconfig range minimum; the P5 full-stack scratch required 1024 to
  **link** — the scratch was never run on hardware; runtime sufficiency
  is pending P8 validation).
- `HEAP_MEM_POOL_SIZE` 4096 → **0** — the handoff-sanctioned SRAM
  candidate, applied only after map/source proof (see below).
- No production timing defaults changed (bond/reset/LED thresholds stay
  at their Kconfig defaults).  nRF5340 board conf/prj unchanged:
  resolved nRF5340 `.config` shows `# CONFIG_USER_PAIRING_CONTROL is
  not set` and no `USER_PAIRING_INPUT` — feature-off proven.

## SRAM budget — before/after/margin

Feature-off baseline (P5-era nRF54L15, heap 4096, no pairing):
`_image_ram_end = 0x2002785c`, used 162396 B, free **1956 B**
(0x7A4) of the 160 KiB region ending at the shared-memory boundary
0x20028000.

Full-stack after P6 (pairing on, diagnostics on, heap 0):
`_image_ram_end = 0x2002750c`, used 161036 B, free **2804 B**
(0xAF4) — **the full-stack margin is larger than the accepted
feature-off margin** (2804 > 1956).  Linker report: `RAM: 161036 B /
160 KB, 96.52%` (FLASH 531668 B / 1428 KB, 36.36%).

How the budget was solved (map/config evidence, no guessing):
1. Full production config (workq 1024, diagnostics on, heap 4096)
   linked at 162268 B / 99.04% — 1572 B free, i.e. "a few bytes".
2. System-heap consumer audit (`arm-zephyr-eabi-nm` + `zephyr.map`):
   the **only** k_malloc/k_heap_alloc/k_free references in the whole
   image are (a) `zephyr/lib/net_buf/buf.c` — its heap data allocator
   (`net_buf_heap_cb`) is compiled only under `K_HEAP_MEM_POOL_SIZE > 0`
   and is referenced by **no** net_buf pool in the build (all BT pools
   are fixed-size), and (b) shell history — which uses its own
   dedicated `K_HEAP_DEFINE` (`CONFIG_SHELL_HISTORY_BUFFER`), not the
   system heap.  `kheap__system_heap` has zero consumers; no
   `HEAP_MEM_POOL_ADD_SIZE_*` options are set.  With the heap at 0 the
   net_buf heap path is not even compiled.
3. Every remaining >256 B static consumer was classified as a used
   production pool with justified sizing: `s` (audio_stream_session
   41068 B), I2S slab 30784 B (audio buffers — protected), `sdc_mempool`
   12481 B, HCI RX data pool 5016 B (BT counts — protected),
   `rx_thread_stack` 4096 B, log ring `buf32` 4096 B
   (`LOG_BUFFER_SIZE` — used, deferred-mode ring; halving it would
   degrade log evidence), `z_main_stack` 3584 B (Cracen RNG requirement,
   `PSA_NEED_CRACEN_CTR_DRBG_DRIVER`), the 5 × 1924 B acceptance/offload
   buffers (diagnostics + FLPR offload scratch — protected), and the
   thread stacks (pairing 1024, input 1024, MPSL 1024, ICMsg 1280, ...).
   WFA_QT's 5200-byte stack config compiles nothing
   (`WFA_QT_CONTROL_APP=n` → module `return()`s) — Kconfig symbol only,
   zero RAM.
4. Conclusion: the heap was the only removable allocation; reducing a
   *used* pool (log ring, shell history, any stack, any BT count) would
   degrade a protected capability and is deliberately not done.  The
   2804 B margin exceeds the accepted feature-off margin, and P8
   validates runtime (stacks, pairing, LED) on hardware.

## Build contract — 79 → 95 assertions

`scripts/check-build-contract.py` (resolved `.config` + `zephyr.dts`,
no source-text matching) grows 79 → **95** (0 failed, exit 0):

nRF54L15 (`54l15-037` … `54l15-050`):
- config: CONTROL=y, INPUT=y, **CONFIG_INPUT=y** (the mandatory
  `USER_PAIRING_INPUT` subsystem dependency — a config without it would
  Kconfig-downgrade INPUT to `n` and never compile the adapter),
  debounce 30, shell timeout 15000, chosen workqueue stack 1024
  (build-minimum; runtime pending P8), chosen heap 0;
- dts: `user-button` alias → `button0`; button0 parent compatible
  gpio-keys with debounce 30; button0 = `<&gpio0 0
  (GPIO_ACTIVE_LOW|GPIO_PULL_UP)>` with `zephyr,code = <INPUT_KEY_0>`;
  button1/2/3 disabled; `user-led` alias → `led0`; led0 = `<&gpio2 0
  GPIO_ACTIVE_LOW>`; led1/led2/led3 absent.

nRF5340 (`5340-030`, `5340-031`): CONTROL not enabled, INPUT not
enabled.  All existing UART/I2S/RF/shared-memory/diagnostic assertions
remain and pass.

Unit suite `tests/unit/build_contract/` grows 33 → **51 tests** with
new fixtures + mutations that fail on wrong resolved artifacts (wrong
alias targets, wrong pin/polarity/code/debounce, re-enabled inherited
buttons, reintroduced LED node, wrong heap/workq/timeout values,
missing `CONFIG_INPUT`, and nRF5340 feature-on).  Docs:
`docs/testing/behavior-contract.md` BUILD-007 and
`docs/testing/coverage-matrix.md` updated (test count 30 → 51 — also
fixing a stale pre-P6 count).

## Verification

- Builds (from the implementation commit): `fw-build-54l15` exit 0
  (RAM 161036/163840, 96.52%; FLASH 531668/1462272, 36.36%);
  `fw-build-5340` exit 0; `fw-build-dongle` exit 0.  Only documented
  pre-existing NCS v3.3.0 diagnostics: PARTITION_MANAGER deprecation,
  SW Split experimental symbols, FLPR-image `UART_CONSOLE`
  assigned-but-got, watchdog "No SOURCES given" (nRF54L15 wdt disabled
  with SDC), `__ASSERT()` informational, and the pre-existing dtc
  structural notes on `/soc/reserved-memory` /
  `rram@165000` / `memory@20028000` (nodes untouched by P6 — the P6
  overlay edits add only root-level aliases/buttons/leds).  Zero
  new/actionable warnings.
- Build contract on real builds: **95 assertions, 0 failed, BUILD
  CONTRACT PASSED**.
- Canonical gate `./scripts/test-all.sh` on clean `62b8727`: **59 PASS
  / 0 FAIL / 59 TOTAL**, exit 0 (35 twister + 5 exec-only + 16 Python +
  coverage + matrix + BSim Stage 1; log `/tmp/p6-gate2.log`).  Coverage
  child: baseline enforcement **0 errors** (no migration — no production
  C file changed; numeric population stays 36).  Matrix child: 0 errors,
  0 notes.  BSim Stage 1: all 17 scenarios strict-checked; **every
  pinned pin byte-identical** (mono 10 ms `0x22AB5C0D`, mono 7.5 ms
  `0x01A3EB05`, Mode A/B 10 ms `0xBAE24F7E`, Mode A/B 7.5 ms
  `0x2D95D15C`/`0xFF82CADB`, `invalid_sdu_resume_10ms` `0x0C61918D`,
  `modea_one_cis_loss_10ms` `0x30D6BAF0`, `modea_first_stop_10ms`
  `0x5A025240`, release/duplicate_release 10 ms `0xAEBD23A1`,
  disconnect/reconnect 10 ms `0x8500C966`, zero-push scenarios
  `0x00000000`).
- Full-stack wiring proven in the resolved ELF: `pairing_control_start`,
  `user_pairing_io_init`, `user_pairing_io_led_set`,
  `bt_bap_pairing_notifications_enable`, the gpio-keys driver
  (`gpio_keys_init`/`gpio_keys_interrupt`), and the registered input
  callback (`_input_callback__user_button_cb`).
- `git diff --check` clean.

## Commits

1. `b246447` — P6 handoff.
2. `62b8727` — overlay/conf + SRAM solution + build-contract
   implementation/tests/docs.
3. No coverage migration (no production C file changed; population 36).
4. This acceptance document.

## Deviations and notes

- **`CONFIG_INPUT=y` added** beyond the handoff's literal four-symbol
  list — mandatory unsatisfied dependency (see rationale above); without
  it the handoff's own INPUT=y contract assertion cannot hold.
- **`/delete-property/` of the inherited `led1`/`led2`/`led3` aliases**
  — required by the handoff's own `/delete-node/` mandate: a deleted
  node label still referenced by the inherited DK alias is a dtc hard
  error, so the aliases must be dropped with the nodes.  This is
  inherited-DK-control cleanup, not scope expansion.
- **`CONFIG_HEAP_MEM_POOL_SIZE=0`** — the handoff-sanctioned candidate,
  applied with map/source proof; all resolved heap users documented
  above; P8 validates runtime.
- First gate run failed 2 children only because the worktree was
  uncommitted (coverage enforcement requires a clean exact commit);
  re-run on the clean implementation commit passes 59/59.  Not a
  regression.
- No hardware tests (P8), no nRF5340 enablement, no advertising payload
  differentiation, no audio/FLPR/shared-memory/pin changes outside the
  inherited DK control cleanup — all per the handoff scope.

## Next-phase grounding

P8 runs the hardware acceptance matrix on the XIAO (button thresholds,
LED patterns, pairing/security, `bt unpair`, streaming after
transitions) and validates the SRAM budget and the 1024-byte pairing
work-queue stack at runtime; AGENTS/README/STATUS/design updates follow
only after the hardware pass.
