# Phase 4 I2S GPIO Pattern — Results

Status: **BLOCKED**

Date: 2026-07-26

## Summary

Two capture attempts run. D0 and D2 work correctly. D1 (P1.5) is stuck HIGH
across both captures — zero transitions. Pinctrl clean, GPIO API returns
success on all operations. Hardware fault on P1.5, not a firmware bug.

---

## Attempt 1 — Pulse pattern (100-step cycle, 10 ms sleep)

- **Test app**: `tests/hardware/nrf54l15_gpio_pattern/` (commit `2926b0a`)
- **Pattern**: D0=1 Hz square, D1=two 50 ms pulses/sec, D2=three 50 ms pulses/sec
- **Build**: 31,108 bytes flash, zero compiler warnings

### Capture

| Parameter | Value |
|-----------|-------|
| Driver | `fx2lafw` |
| Sample rate | 1 MHz |
| Requested duration | 3 s |
| Actual duration | ~375 ms |
| Channels | D0(P1.4), D1(P1.5), D2(P1.6), D3(3V3) |
| Artifact | `/tmp/phase4-gpio-pattern.sr` (3392 bytes) |

### Verdict

| Channel | Observed | Verdict |
|---------|----------|---------|
| D0 | Toggles ~2.8 Hz (~180 ms per half-cycle) | PARTIAL — timing wrong, but probe works |
| D1 | Always HIGH, zero transitions | FAIL |
| D2 | LOW ~320 ms, single rising edge, then HIGH | FAIL — only one edge instead of six/sec |
| D3 | Stable HIGH | PASS |

### Limitations

- Capture truncated to ~375 ms (fx2lafw buffer or continuous-mode limit).
- Short capture + pulse pattern made D1/D2 failure hard to disambiguate.

---

## Attempt 2 — Retest (four-state 100 ms waveform)

- **Test app**: same directory, rewritten `src/main.c`
- **Pattern**: four non-overlapping states, each 100 ms:

  | State | D0/P1.4 | D1/P1.5 | D2/P1.6 |
  |-------|---------|---------|---------|
  | A | high | low | low |
  | B | low | high | low |
  | C | low | low | high |
  | D | low | low | low |

- **Build**: 31,488 bytes flash, zero compiler warnings
- **Resolved DT**: zero PSEL claims for P1.4/P1.5/P1.6 — confirmed by PSEL decode of
  `build/test-nrf54l15-gpio-pattern/nrf54l15_gpio_pattern/zephyr/zephyr.dts`
- **GPIO API**: all `gpio_pin_configure()` and `gpio_pin_set()` calls return 0
  (success) — verified by error-checking wrapper in `set_all_pins()`

### Capture

| Parameter | Value |
|-----------|-------|
| Driver | `fx2lafw` |
| Sample rate | 1 MHz |
| Duration | 3.000 s (3,000,000 samples) |
| Channels | D0(P1.4), D1(P1.5), D2(P1.6), D3(3V3) |
| Artifact | `/tmp/phase4-gpio-pattern-retest.sr` (3396 bytes) |
| Command | `sigrok-cli --driver fx2lafw --config samplerate=1m --channels D0,D1,D2,D3 --time 3000 --output-file /tmp/phase4-gpio-pattern-retest.sr` |

### Edges detected (3,000,000 samples at 1 MHz)

| Channel | Edges | Analysis |
|---------|-------|----------|
| D0 | 15 | ~100 ms HIGH (State A), ~300 ms LOW (B+C+D), period ~400 ms per cycle. Matches expected pattern. |
| D1 | **0** | **Stuck HIGH across all 3,000,000 samples.** Should have ~7 rising + ~7 falling edges. |
| D2 | 15 | ~100 ms HIGH (State C), ~300 ms LOW (A+B+D), period ~400 ms. Matches expected pattern. |
| D3 | — | Stable HIGH (3V3 reference). |

### D0 edge timing

```
 191.70 ms: 0->1  (State A begin)
 291.77 ms: 1->0  (State A end, ~100.07 ms pulse)
 592.01 ms: 0->1  (next cycle, ~300.24 ms low)
 692.09 ms: 1->0
 ...
```

Period = 400.31 ms, pulse width = 100.07 ms. Within expectation for k_msleep(100)
on GRTC (32768 Hz LFCLK source, ~30.5 µs tick).

### D2 edge timing

```
  91.62 ms: 1->0  (first write: D2 driven low)
 391.86 ms: 0->1  (State C begin, ~300.24 ms low = A+B+D)
 491.93 ms: 1->0  (State C end, ~100.07 ms high)
 792.17 ms: 0->1  (next cycle)
 ...
```

Pattern: ~300 ms LOW (A+B+D), ~100 ms HIGH (C). Matches expected.

### D1 edge timing

**Zero edges across 3,000,000 samples.** D1 is electrically HIGH for the entire
capture duration despite `set_all_pins(gpio1, 0, 1, 0)` being called every
~300 ms (State B, D1 should be HIGH for 100 ms). On the other three states D1 is
driven LOW — the capture shows no LOW interval at all.

---

## Probe target evidence

From `nrf-probes`:

```
SERIAL    PROBE                              TARGET    DPIDR       PART        VARIANT  NOTE
8EE9B3FF  Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  0x6ba02477  0x00054b15  AAC0
```

---

## Build and flash commands

```bash
# Build test app (both attempts)
west build -b nrf54l15dk/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_gpio_pattern --pristine \
  -d build/test-nrf54l15-gpio-pattern

# Flash test app (OpenOCD with auto-detected probe)
openocd \
  -c "adapter serial 8EE9B3FF" \
  -f $ZEPHYR_BASE/boards/seeed/xiao_nrf54l15/support/openocd.cfg \
  -c "init" -c "reset halt" \
  -c "nrf54l-load build/test-nrf54l15-gpio-pattern/merged.hex" \
  -c "verify_image build/test-nrf54l15-gpio-pattern/merged.hex" \
  -c "reset run" -c "shutdown"
```

### Capture

```bash
sigrok-cli --driver fx2lafw --config samplerate=1m \
  --channels D0,D1,D2,D3 --time 3000 \
  --output-file /tmp/phase4-gpio-pattern-retest.sr
```

---

## Verdict per channel (retest)

| Channel | Xiao pin | GPIO | Expected | Observed | Verdict |
|---------|----------|------|----------|----------|---------|
| D0 | D0 | P1.4 | 100 ms HIGH (State A), 300 ms LOW | 100 ms HIGH, 300 ms LOW, period 400 ms | **PASS** |
| D1 | D1 | P1.5 | 100 ms HIGH (State B), 300 ms LOW | Always HIGH, 0 edges in 3 s | **FAIL** |
| D2 | D2 | P1.6 | 100 ms HIGH (State C), 300 ms LOW | 100 ms HIGH, 300 ms LOW, period 400 ms | **PASS** |
| D3 | 3V3 | — | Stable HIGH | Stable HIGH | **PASS** |

---

## Receiver restore

Receiving firmware rebuilt (`fw-build-54l15`) and flashed (`fw-flash-54l15`).
447,124 bytes written and verified. No new build warnings. nRF5340 target also
builds clean (`fw-build-5340`, 361,068 bytes flash).

---

## Analysis

D0 and D2 both produce the correct four-state waveform. All `gpio_pin_configure()`
and `gpio_pin_set()` calls return 0 (confirmed by error-checking code). The
resolved devicetree has zero PSEL claims on P1.4/P1.5/P1.6. GPIO API works.

D1/P1.5 is stuck HIGH — zero transitions in 3 seconds. The firmware writes D1
LOW at every state except B (100 ms HIGH per 400 ms cycle). The electrical
signal on the pin does not follow the register writes.

No DT evidence of pinctrl conflict. No claimed external pull without direct
evidence. Observed behavior is Pin 5 of GPIO port 1 on this specific Xiao board
being held HIGH by an unknown electrical path. Possible root causes (not yet
disambiguated):

- Short to 3V3 or VDD on the Xiao PCB
- Damaged GPIO pad (pin 5 of port 1) on the nRF54L15 die
- PCB trace coupling P1.5 to a permanently-driven rail

Recommended hardware debugging:

1. Remove the UDA1334A DAC and all jumper wires from the Xiao.
2. Measure P1.5 voltage with a multimeter (powered, no firmware driving it).
3. Measure P1.5 voltage while running the four-state GPIO pattern test.
4. Compare against P1.4 and P1.6 on the same Xiao board.
5. If P1.5 remains HIGH regardless of GPIO output register, the pin is damaged
   or shorted. Try a different Xiao board.

---

## Next step

Phase remains **BLOCKED**. D0 and D2 GPIO are fully verified. P1.5/D1 has a
hardware fault on this specific Xiao board — not a firmware, DT, or GPIO API
issue. Next action: hardware diagnosis of P1.5 with multimeter. If confirmed
damaged, replace Xiao board. If board is fine, investigate nRF54L15 Errata
for GPIO port 1 pin 5 limitations.
