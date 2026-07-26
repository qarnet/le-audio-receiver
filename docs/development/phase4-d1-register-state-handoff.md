# Phase 4 I2S Investigation — D1 Register-State Test on Official XIAO Board

Status: ready for execution

## Corrected pin map

Official Seeed wiki and installed NCS board files agree:

| Header | SoC pin |
|---|---|
| D0 / AIN0 | P1.04 |
| D1 / AIN1 | P1.05 |
| D2 / AIN2 | P1.06 |
| D3 / AIN3 | P1.07 |

The existing receiver I2S P1.06 SDOUT assignment is correct. Do **not** move
SDOUT to P1.07. The user's analyzer CH2 must remain connected to physical D2,
which is P1.06.

## Goal

Determine whether observed D1-high state is:

1. firmware output latch/configuration issue;
2. analyzer probe/contact issue; or
3. electrical contention/external board or pad issue.

Use the official `xiao_nrf54l15/nrf54l15/cpuapp` board target, not the stock
`nrf54l15dk` target plus overlay. This removes DK UART flow-control pin defaults
as a possible confounder. This is a diagnostic target only; do not migrate the
main receiver target in this phase.

## Scope

Add `tests/hardware/nrf54l15_d1_register_state/` standalone app:

- `CMakeLists.txt`
- `prj.conf`
- `src/main.c`

No project receiver source, overlay, I2S, BLE, or DAC changes.

## Test behavior

1. Configure P1.5 as GPIO push-pull output. Check every return code.
2. Repeat forever: drive P1.5 low for 2 seconds, then high for 2 seconds.
3. Drive P1.4 and P1.6 low throughout. They are only sanity channels.
4. Print one boot banner; no loop log spam.
5. Build with official XIAO target. Confirm resolved DT has no PSEL owner of
   P1.5, and that console uses P1.9/P1.8.

## Hardware procedure

1. Keep analyzer wiring verified by user: CH0=physical D0/P1.04,
   CH1=physical D1/P1.05, CH2=physical D2/P1.06, reference=3V3 plus common
   ground.
2. Flash temporary app through current OpenOCD/Xiao path. Identify probe with
   `nrf-probes --find nrf54l`; do not hardcode a serial.
3. Capture at 100 kHz for 12 seconds. Expected CH1: 2 sec low / 2 sec high.
   CH0/CH2 remain low; reference remains high.
4. While app runs, make **read-only** OpenOCD snapshots without reset or halt:

   ```tcl
   mdw 0x400D8200 5   ;# P1 OUT, OUTSET, OUTCLR, IN, DIR
   mdw 0x400D8294 1  ;# P1 PIN_CNF[5]
   ```

   P1 non-secure base is `0x400D8200`; P1.5 mask is `0x00000020`.
   Collect enough snapshots to catch both two-second states. Do not use `mww`,
   `halt`, `reset`, recover, probe-rs, or a destructive operation.
5. Interpret evidence:

| P1.OUT bit 5 | P1.IN bit 5 | CH1 | Meaning |
|---|---|---|---|
| toggles | toggles | toggles | pass; earlier probe/setup issue |
| toggles | toggles | fixed high | analyzer probe/channel issue |
| toggles | fixed high | fixed high | physical external pull/short or damaged pad |
| fixed high | fixed high | fixed high | firmware/config test issue |
| toggles | fixed low | fixed low | physical pull/short to ground or damaged pad |

Do not call any cause confirmed unless all three evidence columns support it.

6. Restore main receiver: `fw-build-54l15`, `fw-flash-54l15`. Scan build output
for warnings. The committed test app remains available for reproduction.

## Results

Create `docs/development/phase4-d1-register-state-results.md` with full
capture details, OpenOCD snapshots, correlation table, result, raw artifact
paths, restore result, and next decision.

## Verification

```bash
nix flake check --no-build
west build -b xiao_nrf54l15/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_d1_register_state --pristine \
  -d build/test-nrf54l15-d1-register-state
fw-build-54l15
fw-build-5340
```

## Constraints

- Preserve unstaged receiver diagnostics in
  `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` and `src/bt_bap.c` exactly.
- No main-target migration in this phase.
- No I2S change. This test has no I2S device or PSEL config.
- Commit only new test, results, and this handoff. Do not push, amend, merge,
  or open a PR.

## Executor recap

Return files changed, full analyzer and register evidence, conclusion limited
to evidence, restore result, tests, commit hash/message, and next action.
