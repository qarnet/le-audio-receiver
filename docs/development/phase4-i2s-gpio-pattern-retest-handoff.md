# Phase 4 I2S Investigation — GPIO Pattern Retest

Status: required follow-up to GPIO pattern commit `2926b0a`

## Problem

First GPIO capture only lasted about 375 ms. P1.4/D0 toggled, but P1.5/D1 was
always high and P1.6/D2 showed one rising edge. Original 50 ms pulse pattern
and intended 10 ms sleeps made this evidence hard to interpret, especially
because the measured timing was roughly 2.8x faster than expected.

Do not diagnose I2S yet. First prove physical analyzer connection for every
target header pin.

## Goal

Make one short analyzer capture unambiguous even if fx2lafw stops after
~375 ms. Verify GPIO API calls succeed. Then restore receiver image.

## Scope

Change only:

- `tests/hardware/nrf54l15_gpio_pattern/src/main.c`
- `docs/development/phase4-i2s-gpio-pattern-results.md`
- `docs/development/phase4-i2s-gpio-pattern-handoff.md` (previously untracked;
  add it to git unchanged)
- this retest handoff document

## Required test waveform

Do not use pulse counting or assumed sleep accuracy. Repeat four states, each
for 100 ms:

| State | D0 / P1.4 | D1 / P1.5 | D2 / P1.6 |
|---|---|---|---|
| A | high | low | low |
| B | low | high | low |
| C | low | low | high |
| D | low | low | low |

Expected in a ~375 ms capture: each analyzer channel has a distinct,
non-overlapping high plateau. Exact durations are not acceptance criteria.

## Implementation requirements

1. Check and print every `gpio_pin_configure()` result. Stop with a clear
   error if any is non-zero.
2. Check every GPIO set operation. A helper which writes all three pins and
   checks/prints first error is acceptable. Avoid per-loop console spam.
3. Print one boot banner containing the state sequence. Serial logging is
   secondary; analyzer evidence is primary.
4. Keep P1.4/P1.5/P1.6 as GPIO only. Do not add I2S, SPI22, PWM20, OpenOCD GPIO
   writes, MCK, BLE, or DAC changes.
5. Rebuild pristine and confirm resolved DT still has no PSEL claimant on these
   three pins.

## Hardware execution

1. Record dynamic Xiao identity with `nrf-probes`; no hardcoded serial.
2. Flash temporary test image through existing OpenOCD/Xiao load path.
3. Capture D0/D1/D2/D3 with fx2lafw at 1 MHz, using the same channel map.
   Capture at least one complete four-state sequence. If tool still ends around
   375 ms, that is enough because nominal sequence is 400 ms; retry once at a
   lower rate (100 kHz) or with states shortened to 75 ms if needed. Do not
   perform repeated blind attempts.
4. PASS only when D0, D1, D2 each show their own high state and D3 remains high.
   If not, phase remains blocked. State observed pattern exactly; do not blame
   I2S or silicon without evidence.
5. Rebuild and reflash current receiver image with `fw-build-54l15` and
   `fw-flash-54l15` afterward. Scan build output for warnings.

## Results update

Replace stale first-capture conclusion with both attempts. Include raw artifact
paths, capture settings, actual state sequence, GPIO API outcomes, and explicit
PASS/BLOCKED verdict. Do not claim an external pull or pinctrl interference
without direct evidence.

## Verification

```bash
nix flake check --no-build
west build -b nrf54l15dk/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_gpio_pattern --pristine \
  -d build/test-nrf54l15-gpio-pattern
fw-build-54l15
fw-build-5340
```

## Constraints

- Preserve existing unstaged diagnostics in
  `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` and `src/bt_bap.c` exactly.
- No recover, erase, or persistent settings change.
- Use OpenOCD only; no probe-rs.
- Do not push, amend, merge, or open a PR.
- Commit all files in scope, including both handoff documents. Stage no
  pre-existing diagnostic files.

## Executor recap

Return exact capture evidence for every channel, GPIO API results, build/flash
and restore evidence, files committed, commit hash/message, blockers, and
recommended next step.
