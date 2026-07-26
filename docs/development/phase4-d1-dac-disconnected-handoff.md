# Phase 4 D1 Investigation — DAC Digitally Disconnected

Status: user removed all DAC↔MCU signal connections; MUTE returned low

## Goal

Test D1/P1.05 GPIO waveform with external DAC digital path absent. This isolates
MCU plus analyzer from DAC/breakout interaction.

## Test condition

- DAC signal wires BCK, LRCK/WSEL, and DIN are disconnected from MCU.
- User set MUTE low. MUTE state is irrelevant while DAC digital path is absent.
- Analyzer remains connected directly to Xiao: CH0=D0/P1.04,
  CH1=D1/P1.05, CH2=D2/P1.06, CH3=3V3, with common ground.

Correct wiring record, for later reconnect:

| Xiao | DAC |
|---|---|
| D0 / P1.04 | BCK |
| D1 / P1.05 | LRCK / WSEL |
| D2 / P1.06 | DIN |

## Scope

No source/test change. Flash existing committed D1 register-state diagnostic,
capture analyzer, restore receiver, and correct results-doc signal-label error.

Edit only:

- `docs/development/phase4-d1-register-state-results.md`
- this handoff document

## Execution

1. Flash `build/test-nrf54l15-d1-register-state/merged.hex` using dynamic
   `nrf-probes --find nrf54l` and normal OpenOCD Xiao loader.
2. Capture at 100 kHz for 12 seconds:
   ```bash
   sigrok-cli -d fx2lafw --config samplerate=100k -C D0,D1,D2,D3 \
     --time 12000 -o /tmp/phase4-d1-dac-disconnected.csv -O csv
   ```
3. Expected: D1 toggles two seconds low / two seconds high. D0 and D2 remain
   low. D3 remains high.
4. Restore receiver image with `fw-flash-54l15`; do not rebuild unless needed.
5. Correct all old results-doc references that swap D1/D2 DAC functions. They
   must read P1.05→LRCK/WSEL and P1.06→DIN.

## Interpretation

- D1 toggles: DAC/breakout or DAC wiring caused prior observed D1 behavior.
- D1 fixed high: DAC is ruled out; analyzer-to-Xiao measurement path remains
  suspect despite user wiring check. Do not infer silicon failure.

## Constraints

- Preserve unstaged receiver diagnostics exactly.
- No source change, no OpenOCD register writes, no recover, no persistent state.
- Commit only results + handoff. Do not push, amend, merge, or open PR.

## Executor recap

### Execution summary (2026-07-26 17:26)

- **Flash**: 33,648 bytes written/verified (probe 8EE9B3FF, RRAM load).
- **Capture**: 1,200,000 samples at 100 kHz, 12s, D0-D3.
- **Result**: D1 toggles ~50/50 (599,881 LOW / 600,119 HIGH). D0 fixed LOW.
  D2 fixed LOW. D3 fixed HIGH (3V3).
- **Interpretation**: D1 toggles → DAC/breakout or DAC wiring caused prior
  observed D1 stuck-HIGH behavior. Analyzer measurement path is functional.
- **Restore**: 447,124 bytes restored via `fw-flash-54l15` (existing
  `build/nrf54l15/merged.hex`, no rebuild).
- **Doc corrections**: Fixed three signal-label errors in
  `phase4-d1-register-state-results.md`:
  - DAC MUTE section: P1.05 (DIN) → P1.05 (LRCK/WSEL), P1.06 (LRCK) → P1.06 (DIN)
  - Conclusion line 237: D1 (P1.05 / DIN) → D1 (P1.05 / LRCK/WSEL)
  - Conclusion line 246: D1/DIN → D1/LRCK/WSEL
- **Unstaged preserved**: `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`,
  `src/bt_bap.c` untouched.
- **Artifact**: `/tmp/phase4-d1-dac-disconnected.csv` (9.2 MB).

### Conclusion

D1 unblocked. Root cause: DAC/breakout electrical interaction on
D1/LRCK/WSEL line. Next: reconnect DAC wires, test with different DAC
unit or board-level isolation to confirm whether breakout design or this
specific unit causes contention. D1 hardware, GPIO driver, and firmware
are all confirmed functional.
