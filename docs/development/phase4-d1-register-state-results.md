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

## Correlation table

| P1.OUT bit5 | P1.IN bit5 | Analyzer CH1 | Meaning                               |
|-------------|-----------|-------------|---------------------------------------|
| toggles     | toggles   | ?           | register evidence confirms toggling   |

P1.OUT bit5 toggles between low (snapshot 1) and high (snapshots 2,3).
P1.IN bit5 tracks P1.OUT bit5 exactly — input buffer reads back driven
output value. DIR and PIN_CNF[5] are stable and correct (output,
push-pull, standard drive, input buffer connected).

Analyzer CH1 data not available in this session (remote execution).
User must supply analyzer capture to complete the third column.

## Conclusion

**Firmware is correctly driving P1.5 with the specified 2s toggle pattern.**
The register state shows:

1. P1.5 configured as push-pull output (DIR bit5=1, PIN_CNF[5].DIR=1,
   PIN_CNF[5].DRIVE=S0S1).
2. P1.OUT bit5 toggles between 0 and 1.
3. P1.IN bit5 mirrors P1.OUT bit5 — no physical contention or
   electrical conflict on the pin (input buffer reads back what the
   output driver puts out).

If the analyzer CH1 (physical D1/P1.05) still reads fixed high while
the registers toggle, the cause is an **analyzer probe or channel
issue** (row 2 of the interpretation table). If analyzer CH1 toggles,
the earlier observed D1-high was a prior probe/setup issue (row 1).

The firmware, GPIO configuration, and pin itself are not at fault.

## Raw artifact paths

- Test firmware hex: `build/test-nrf54l15-d1-register-state/merged.hex`
- Resolved DTS: `build/test-nrf54l15-d1-register-state/nrf54l15_d1_register_state/zephyr/zephyr.dts`
- Resolved config: `build/test-nrf54l15-d1-register-state/nrf54l15_d1_register_state/zephyr/.config`

## Restore

Main receiver firmware restored: `fw-build-54l15` + `fw-flash-54l15`.
447124 bytes written and verified. Both nRF5340 and nRF54L15 targets
build clean (0 errors, 0 new warnings).

## Next decision

User provides analyzer CH1 capture for column 3 of correlation table.
If CH1 toggles → pass (prior probe issue, resolved). If CH1 fixed high
→ analyzer probe/channel fault on D1 — check physical connection,
probe wire, and contact at D2 pad on Xiao board.
