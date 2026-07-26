# Phase 4 I2S Investigation — GPIO Pattern Proof

Status: ready for execution

## Goal

Prove logic-analyzer probe placement and Xiao header-to-GPIO mapping before
debugging I2S20. The existing streaming evidence shows P1.4 activity near
6 MHz while P1.5/P1.6 are flat. That can be an I2S failure, but can also be
a shifted/loose logic-analyzer connection. This phase separates those cases.

## Scope

Create and run a small, standalone nRF54L15 GPIO pattern application. It must
drive the three target pins as GPIO, not as I2S:

| Xiao header | GPIO | Required observable pattern |
|---|---|---|
| D0 | P1.4 | 1 Hz square wave |
| D1 | P1.5 | two short pulses per one-second period |
| D2 | P1.6 | three short pulses per one-second period |

Use the existing `fx2lafw` analyzer to capture D0/D1/D2 and 3V3. Preserve a
short raw capture and record command, sample rate, duration, channel mapping,
and measured pulse pattern.

The diagnostic image temporarily replaces receiver firmware. Rebuild and flash
the current main application at end of the phase, leaving receiver in its
normal state.

## Out of scope

- I2S20 driver, overlay, clock-source, MCK, DAC, BLE, LC3, drift, ASRC, or
  central changes.
- OpenOCD/SWD writes to GPIO registers. They are not a valid electrical test
  while I2S owns pins and can perturb GPIO state.
- Any erase/recover operation, persistent settings change, or probe serial
  hardcoding.

## Current facts

- Receiver is Xiao nRF54L15, probe auto-detected by `nrf-probes --find nrf54l`.
- Console is `/dev/ttyACM0` at 115200, but serial is not required for this app.
- Current application I2S target pins are P1.4/P1.5/P1.6. P1.7 is currently
  diagnostic MCK output; do not rely on it in this phase.
- Logic analyzer: `fx2lafw`, channels D0/D1/D2 on Xiao D0/D1/D2, D3 on 3V3.
- Existing tracked worktree contains intentional uncommitted diagnostics in
  `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` and `src/bt_bap.c`. Preserve
  them exactly. Do not reset, stash, revert, or commit them.

## Implementation

1. Add standalone app under `tests/hardware/nrf54l15_gpio_pattern/`:
   - `CMakeLists.txt`, `prj.conf`, `src/main.c`, and board-specific overlay if
     needed.
   - Build target: `nrf54l15dk/nrf54l15/cpuapp`.
   - Use Zephyr GPIO API and `DT_NODELABEL(gpio1)`; configure P1.4, P1.5, and
     P1.6 as push-pull outputs.
   - Pattern must repeat indefinitely and be slow enough to identify without
     decoder tooling. Use a documented 1000 ms cycle. D1/D2 pulse width must
     be at least 50 ms.
   - Print a one-line boot banner and pattern description once, then do not
     spam logs.
   - Configure UART20 to existing Xiao console pins only if needed for the
     banner. Do not reuse P1.4/P1.5/P1.6 for any peripheral.
2. Build pristine into ignored directory `build/test-nrf54l15-gpio-pattern`.
3. Verify resolved DT and generated configuration show gpio1 available and no
   unexpected PSEL claimant for P1.4/P1.5/P1.6.
4. Identify probe dynamically with `nrf-probes --find nrf54l`; include raw
   target identity evidence from `nrf-probes` in results. Do not write serial
   into a file or command.
5. Flash test image using OpenOCD and stock Xiao config, following existing
   `scripts/bin/fw-flash-54l15` behavior but pointing at test image:
   - use the dynamically found adapter serial;
   - `reset halt`;
   - `nrf54l-load <test merged.hex>`;
   - `verify_image <test merged.hex>`;
   - `reset run`.
   Do not use `nrf54l_recover`, mass erase, probe-rs, or another flasher.
6. Capture a 2–3 second sample with `sigrok-cli --driver fx2lafw` at 1 MHz or
   higher. Record D0/D1/D2/D3. Preserve raw data under `/tmp`, not repo.
7. Analyze capture. Success requires D0 1 Hz, D1 exactly two pulses/sec, D2
   exactly three pulses/sec, and D3 stable high. If capture cannot prove all
   four, report phase blocked; do not infer I2S fault.
8. Rebuild and flash current receiver app using `fw-build-54l15` then
   `fw-flash-54l15`. Capture console with serial-mcp if port listing/open works;
   if serial-mcp server schema fails again, record exact MCP failure. Confirm
   image flashed and no new build warnings.

## Results document

Create `docs/development/phase4-i2s-gpio-pattern-results.md` with:

- PASS/BLOCKED verdict for each D0–D3 channel.
- Exact build, flash, capture, and restore commands.
- Probe target evidence from `nrf-probes`.
- Analyzer sample rate/duration/channel map and raw artifact path.
- Any limitation, especially missing/ambiguous analyzer data or serial MCP
  failure.
- Next step: standalone I2S output sample only if GPIO result passes.

## Verification

Run and report:

```bash
nix flake check --no-build
west build -b nrf54l15dk/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_gpio_pattern --pristine \
  -d build/test-nrf54l15-gpio-pattern
fw-build-54l15
fw-build-5340
git diff --check
git status --short
```

Scan full build output for warnings. Fix warnings introduced by this phase.

## Commit

Commit only new GPIO test files and new results document. Do not stage current
pre-existing modifications to the receiver overlay or `src/bt_bap.c`. Do not
push, amend, merge, or open a PR. No AI/tool attribution in commit message.

## Executor recap

Return files changed, exact hardware evidence, analyzer result, commands/tests
and output, restore result, commit hash/message, blockers, deviations, and
recommended next action.
