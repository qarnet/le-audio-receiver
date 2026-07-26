# Phase 4 D1 Register-State Results

Date: 2026-07-26
Target: xiao_nrf54l15/nrf54l15/cpuapp (official board target)
Probe: 8EE9B3FF (Seeed Studio XIAO nrf54 CMSIS-DAP → nRF54L15 AAC0)

## Test app

`tests/hardware/nrf54l15_d1_register_state/` — standalone GPIO toggle app.
P1.5 toggles 2s low / 2s high. P1.4 and P1.6 held low. UART20 console
on P1.9/P1.8 (SAMD11 CDC bridge). No BLE, no I2S, no overlay.

Build: `west build -b xiao_nrf54l15/nrf54l15/cpuapp tests/hardware/nrf54l15_d1_register_state --pristine -d build/test-nrf54l15-d1-register-state`

Resolved DTS: no PSEL owner of P1.4–P1.7. UART20 at P1.8/P1.9. Verified.

## Register address correction

Handoff specified non-secure P1 base `0x400D8200`. The test app runs in
flat/secure mode (`CONFIG_ARM_TRUSTZONE_M is not set`). Correct secure
P1 base is `0x500D8200` (confirmed from NCS MDK `nrf54l15_global.h`:
`NRF_P1_S_BASE 0x500D8200UL`).

nRF54L15 GPIO register layout (`NRF_GPIO_Type`, from `nrf54l15_types.h`):

| Offset | Register   | Address        |
|--------|-----------|----------------|
| 0x00   | OUT       | 0x500D8200     |
| 0x04   | OUTSET    | 0x500D8204     |
| 0x08   | OUTCLR    | 0x500D8208     |
| 0x0C   | IN        | 0x500D820C     |
| 0x10   | DIR       | 0x500D8210     |
| 0x80+  | PIN_CNF[] | 0x500D8280+    |
| 0x94   | PIN_CNF[5]| 0x500D8294     |

## OpenOCD read-only register snapshots

Three snapshots taken with independent OpenOCD sessions (`init` only —
no halt, no reset, no write). DHCSR=0x00140001 confirms core RUNNING
(C_HALT=0) during all reads.

### Snapshot 1 (T+0s — immediately after flash+reset)

```
0x500d8200: 00008200 00008200 00008200 00008300 00008270
0x500d8294: 00000003
```

Decode:
- OUT        = 0x00008200 → P1.5 (bit 5=0x20) = **LOW**
- OUTSET     = 0x00008200 → (unchanged)
- OUTCLR     = 0x00008200 → (unchanged)
- IN         = 0x00008300 → P1.5 input reads **LOW**
- DIR        = 0x00008270 → bits 4,5,6,9,15 = output ✓
- PIN_CNF[5] = 0x00000003 → DIR=1 (output), INPUT=1 (connect), DRIVE=S0S1, PULL=disabled, SENSE=disabled

### Snapshot 2 (T+3s)

```
0x500d8200: 00008220 00008220 00008220 00008320 00008270
0x500d8294: 00000003
```

Decode:
- OUT        = 0x00008220 → P1.5 (bit 5=0x20) = **HIGH**
- IN         = 0x00008320 → P1.5 input reads **HIGH**
- DIR        = 0x00008270 → unchanged
- PIN_CNF[5] = 0x00000003 → unchanged

### Snapshot 3 (T+6s)

```
0x500d8200: 00008220 00008220 00008220 00008320 00008270
0x500d8294: 00000003
```

Identical to Snapshot 2 — P1.5 HIGH (within 2s-high phase of toggle).

## Logic analyzer capture (2026-07-26 follow-up)

Capture command:

```
sigrok-cli -d fx2lafw --config samplerate=100k -C D0,D1,D2,D3 \
  --time 12000 -o /tmp/phase4-d1-capture.csv -O csv
```

| Parameter         | Value                                            |
|-------------------|--------------------------------------------------|
| Driver            | fx2lafw (Saleae Logic clone, 8 channels)         |
| Channels          | D0 (P1.04), D1 (P1.05), D2 (P1.06), D3 (3V3)    |
| Sample rate       | 100 kHz                                          |
| Duration          | 12 seconds (1,200,000 samples)                   |
| Output            | `/tmp/phase4-d1-capture.csv` (9.6 MB CSV)        |

### Raw channel counts

| Channel | Value | Sample count | Meaning          |
|---------|-------|-------------|------------------|
| D0      | 0     | 1,200,000   | LOW (as expected)|
| D1      | 1     | 1,200,000   | **stuck HIGH**   |
| D2      | 0     | 1,200,000   | LOW (as expected)|
| D3      | 1     | 1,200,000   | HIGH (3V3 rail)  |

D0 and D2 read LOW throughout — confirming analyzer channels and
probe wiring are functional for those pins. D3 reads HIGH (3V3 rail).
D1 reads HIGH for all 1,200,000 samples: no edges, no low periods.

Over 12 seconds the test app should toggle D1 six times (three low→high
transitions). No transition is visible in the capture.

## Correlation table

| P1.OUT bit5 | P1.IN bit5 | Analyzer CH1 | Meaning                               |
|-------------|-----------|-------------|---------------------------------------|
| toggles     | toggles   | fixed HIGH  | register evidence confirms toggling; analyzer does not see toggle |

P1.OUT bit5 toggles between low (snapshot 1) and high (snapshots 2,3).
P1.IN bit5 tracks P1.OUT bit5 exactly — input buffer reads back driven
output value. DIR and PIN_CNF[5] are stable and correct (output,
push-pull, standard drive, input buffer connected).

## Conclusion

**Register evidence confirms firmware drives P1.5 with the specified
2s toggle pattern.** The register state shows:

1. P1.5 configured as push-pull output (DIR bit5=1, PIN_CNF[5].DIR=1,
   PIN_CNF[5].DRIVE=S0S1).
2. P1.OUT bit5 toggles between 0 and 1.
3. P1.IN bit5 mirrors P1.OUT bit5 — the input buffer reads back the
   driven output value, confirming internal GPIO configuration and
   sampled level are consistent.

**Limit of register evidence:** OUT/IN agreement proves the GPIO output
driver and input buffer are functioning internally. It does not
independently verify the external pin voltage or exclude all possible
sources of external electrical contention or physical bond-wire/pad
fault.

**Analyzer capture evidence:** Across 12 seconds at 100 kHz, CH1/D1
reads fixed HIGH. CH0/D0 and CH2/D2 read correctly (LOW as expected),
confirming the analyzer, probe wires, and connections for those channels
are functional.

The analyzer measurement path on D1 (probe wire, clip, solder joint,
or logic-analyzer channel input) requires physical inspection and
repair before a valid D1 waveform can be captured. The register
evidence from 27c5648 shows internal GPIO toggling; the analyzer
failure to observe the toggle is consistent with row 2 of the
interpretation table (analyzer probe/channel fault).

Phase 4 D1 is **blocked** on repair of the D1 analyzer measurement path.
Once repaired, re-capture CH1 while the test app runs to confirm D1
physically toggles 2s low / 2s high.

## Raw artifact paths

- Test firmware hex: `build/test-nrf54l15-d1-register-state/merged.hex`
- Resolved DTS: `build/test-nrf54l15-d1-register-state/nrf54l15_d1_register_state/zephyr/zephyr.dts`
- Resolved config: `build/test-nrf54l15-d1-register-state/nrf54l15_d1_register_state/zephyr/.config`

## Restore

Test app flashed from `build/test-nrf54l15-d1-register-state/merged.hex`
(33648 bytes written + verified). After capture, main receiver firmware
restored via `fw-flash-54l15`: 447124 bytes written and verified.
Unstaged receiver diagnostics preserved (no rebuild — existing
`build/nrf54l15/merged.hex` used).

## DAC MUTE isolation check (2026-07-26 17:15)

Test condition: DAC (CJMCU-1334 / UDA1334A) remains connected to
P1.04 (BCK), P1.05 (LRCK/WSEL), P1.06 (DIN). User set DAC MUTE pin HIGH
(analog output muted). No other wiring changed.

Per UDA1334A datasheet, MUTE high mutes analog audio output. It does
not drive, disconnect, or otherwise control BCLK/LRCK/DIN digital
input pins. Therefore no D1 change is expected unless board wiring
or DAC behavior differs from documented topology.

Test firmware: same `build/test-nrf54l15-d1-register-state/merged.hex`
(committed). No source change.

### Flash

```
Probe 8EE9B3FF (auto-detected: target identifies as nRF54L15)

openocd -c "adapter serial 8EE9B3FF" \
  -f ~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/support/openocd.cfg \
  -c "init" -c "reset halt" \
  -c "nrf54l-load build/test-nrf54l15-d1-register-state/merged.hex" \
  -c "verify_image build/test-nrf54l15-d1-register-state/merged.hex" \
  -c "reset run" -c "shutdown"

30396 bytes written at address 0x00000000
3252 bytes written at address 0x000076c0
downloaded 33648 bytes in 0.469762s (69.949 KiB/s)
verified 33648 bytes in 0.070238s (467.829 KiB/s)
```

### Capture

```
sigrok-cli -d fx2lafw --config samplerate=100k -C D0,D1,D2,D3 \
  --time 12000 -o /tmp/phase4-d1-mute-high-capture.csv -O csv
```

### Result

Raw channel counts (1,200,000 samples, 12s at 100 kHz):

| Channel | Value | Sample count  | Meaning                      |
|---------|-------|--------------|-------------------------------|
| D0      | 0     | 1,199,999    | LOW (as expected)             |
| D1      | 0     | 599,880      | LOW — **toggling**            |
| D1      | 1     | 600,119      | HIGH — **toggling**           |
| D2      | 0     | 1,199,999    | LOW (as expected)             |
| D3      | 1     | 1,199,999    | HIGH (3V3 rail)               |

D1 toggles approximately 50/50 — consistent with 2s low / 2s high
pattern. D0 and D2 read LOW throughout. D3 reads HIGH (3V3 rail).

This is a **change from the prior capture** (MUTE low, section above):
in that capture D1 was stuck HIGH for all 1,200,000 samples. With MUTE
high, D1 correctly toggles as the register evidence predicted.

The analyzer is therefore functional on all four channels. D0 and D2
were working in both captures. D1 was the only anomalous channel, and
its behavior flips between MUTE-low (stuck HIGH) and MUTE-high (toggling).

### Conclusion

**DAC MUTE level changes D1 (P1.05 / LRCK/WSEL) electrical behavior.**

With DAC MUTE low (prior capture): D1 stuck HIGH at analyzer —
register toggling not visible externally.

With DAC MUTE high (this capture): D1 toggles correctly, matching
register evidence.

The UDA1334A datasheet states MUTE controls analog output only; it
should not affect digital input pins. Observed behavior contradicts
datasheet expectation: the DAC's MUTE state alters the voltage seen
at D1/LRCK/WSEL via the logic analyzer.

This is consistent with either:
1. A board-level electrical interaction between MUTE and LRCK/WSEL on the
   CJMCU-1334 breakout (shared pull-up, leakage path, or pcb trace
   coupling), or
2. A damaged unit where MUTE inadvertently drives LRCK/WSEL.

With MUTE high, D1 toggles correctly — D1 is **not blocked** on
analyzer measurement-path repair. Phase 4 D1 is **unblocked** when
DAC MUTE is held high. Next step: isolate DAC digital wires (BCK,
DIN, LRCK) and re-capture D1 without DAC attached, to determine
whether D1 still toggles independently of DAC state.

Artifact: `/tmp/phase4-d1-mute-high-capture.csv` (9.2 MB, 1,200,001 lines)

### Restore

Main receiver firmware restored via `fw-flash-54l15`: 447,124 bytes
written and verified (existing `build/nrf54l15/merged.hex`, no rebuild).
Unstaged receiver diagnostics preserved (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`,
`src/bt_bap.c` — not touched).

## DAC digital disconnect test (2026-07-26 17:26)

Test condition: DAC signal wires (BCK, LRCK/WSEL, DIN) physically
disconnected from MCU. MUTE set low (irrelevant while digital path
absent). Analyzer remains connected: CH0=D0/P1.04, CH1=D1/P1.05,
CH2=D2/P1.06, CH3=3V3, common ground.

Test firmware: same `build/test-nrf54l15-d1-register-state/merged.hex`
(committed). No source change.

### Flash

```
Probe 8EE9B3FF (auto-detected: target identifies as nRF54L15)

openocd -c "adapter serial 8EE9B3FF" \
  -f ~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/support/openocd.cfg \
  -c "init" -c "reset halt" \
  -c "nrf54l-load build/test-nrf54l15-d1-register-state/merged.hex" \
  -c "verify_image build/test-nrf54l15-d1-register-state/merged.hex" \
  -c "reset run" -c "shutdown"

30396 bytes written at address 0x00000000
3252 bytes written at address 0x000076c0
downloaded 33648 bytes in 0.469591s (69.974 KiB/s)
verified 33648 bytes in 0.069664s (471.684 KiB/s)
```

### Capture

```
sigrok-cli -d fx2lafw --config samplerate=100k -C D0,D1,D2,D3 \
  --time 12000 -o /tmp/phase4-d1-dac-disconnected.csv -O csv
```

### Result

Raw channel counts (1,200,000 samples, 12s at 100 kHz):

| Channel | Value | Sample count  | Meaning                      |
|---------|-------|--------------|-------------------------------|
| D0      | 0     | 1,200,000    | LOW (as expected)             |
| D1      | 0     | 599,881      | LOW — **toggling**            |
| D1      | 1     | 600,119      | HIGH — **toggling**           |
| D2      | 0     | 1,200,000    | LOW (as expected)             |
| D3      | 1     | 1,200,000    | HIGH (3V3 rail)               |

D1 toggles approximately 50/50 — consistent with 2s low / 2s high
pattern. D0 and D2 read LOW throughout. D3 reads HIGH (3V3 rail).

### Conclusion

**DAC digital wiring caused prior D1 stuck-HIGH behavior.** With DAC
signal wires disconnected, D1 toggles correctly, matching register
evidence. The analyzer measurement path (probe wire, clip, channel
input) is functional. The MUTE-high test (section above) corroborates:
DAC/breakout interaction, not analyzer fault, is the root cause.

### Restore

Main receiver firmware restored via `fw-flash-54l15`: 447,124 bytes
written and verified (existing `build/nrf54l15/merged.hex`, no rebuild).
Unstaged receiver diagnostics preserved (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`,
`src/bt_bap.c` — not touched).

Artifact: `/tmp/phase4-d1-dac-disconnected.csv` (9.2 MB, 1,200,007 lines)

## Next decision

D1 toggles correctly when DAC digital path is absent. D1 unblocked.
Root cause: DAC/breakout electrical interaction on D1/LRCK/WSEL line.
Next: reconnect DAC digital wires and test with different DAC unit or
board-level isolation to confirm whether the CJMCU-1334 breakout or
this specific unit causes the contention.
