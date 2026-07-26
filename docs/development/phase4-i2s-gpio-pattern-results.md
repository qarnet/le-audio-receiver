# Phase 4 I2S GPIO Pattern — Results

Status: **BLOCKED**

Date: 2026-07-26

## Verdict per channel

| Channel | Xiao pin | GPIO | Expected | Observed | Verdict |
|---------|----------|------|----------|----------|---------|
| D0      | D0       | P1.4 | 1 Hz square wave (500 ms HIGH/LOW) | Toggles ~2.8 Hz (~180 ms per half-cycle) | **PARTIAL** — proves probe placement works on D0, but timing wrong |
| D1      | D1       | P1.5 | Two 50 ms pulses/sec | Always HIGH — zero transitions across 375 ms capture | **FAIL** |
| D2      | D2       | P1.6 | Three 50 ms pulses/sec | LOW for ~320 ms, then single rising edge, stays HIGH | **FAIL** |
| D3      | 3V3      | —    | Stable HIGH | Stable HIGH | **PASS** |

## Capture details

- **Driver**: `fx2lafw` (Saleae Logic clone, 8 channels)
- **Sample rate**: 1 MHz
- **Captured duration**: ~375 ms (actual `--time 3000` fell short; fx2lafw buffer or continuous-mode limitation)
- **Channel mapping**: D0=P1.4, D1=P1.5, D2=P1.6, D3=3V3
- **Raw artifact**: `/tmp/phase4-gpio-pattern.sr` (3392 bytes, sigrok session)
- **Capture command**:
  ```
  sigrok-cli --driver fx2lafw --config samplerate=1m \
    --channels D0,D1,D2,D3 --time 3000 \
    --output-file /tmp/phase4-gpio-pattern.sr
  ```

## Probe target evidence

From `nrf-probes`:
```
SERIAL    PROBE                              TARGET    DPIDR       PART        VARIANT  NOTE
8EE9B3FF  Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  0x6ba02477  0x00054b15  AAC0
```

## Build and flash commands

```bash
# Build test app
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

# Rebuild and flash receiver
fw-build-54l15
fw-flash-54l15
```

## Test app

- Source: `tests/hardware/nrf54l15_gpio_pattern/`
- Uses Zephyr GPIO API to drive P1.4/P1.5/P1.6 as push-pull outputs
- 100-step cycle with 10 ms k_msleep per step (intended 1 s period)
- Stock DK overlay remaps UART20 to Xiao CDC pins (P1.9/P1.8), disables SPI22 + PWM20 + SPI flash
- Resolved DT verified: zero PSEL claims for P1.4/P1.5/P1.6
- Build: 31,108 bytes flash, zero warnings

## Analysis

D0 toggling proves the logic analyzer probe is correctly connected to D0/P1.4 and
GPIO1 port is operational. The measured period (~360 ms) is shorter than the
expected 1000 ms, suggesting `k_msleep(10)` resolves to ~3.6 ms on this
configuration (likely HFINT RC clock source, untrimmed).

D1 (P1.5) stuck HIGH across the entire ~375 ms capture is anomalous. The code
explicitly sets D1 LOW at step 0 and cycles through LOW/HIGH phases. In 375 ms,
the loop should have completed ~37 steps, driving D1 LOW for most of them.
Possible causes (not yet disambiguated):

- P1.5 may be pulled HIGH by an external board component or PCB trace
- The pinctrl `spi22_default` node (not deleted, only un-referenced) applies
  `bias-pull-down` to P1.6 but has no effect on P1.5 — unlikely to be the cause
- nRF54L15 GPIO output on P1.5 may not be taking effect (PIN_CNF register issue)
- Code bug in `gpio_pin_set(gpio1, D1_PIN, ...)` for pin 5

D2 (P1.6) starts LOW (consistent with either `spi22_default` pull-down or code
driving LOW at step 0), then transitions HIGH and stays HIGH. This single edge
does not match the expected three 50-ms pulses per second. The `spi22_default`
pinctrl with `bias-pull-down` exists in the resolved DT but should not be active
since &spi22 is disabled and its pinctrl refs were cleared. However, if the
pinctrl framework applies all pinctrl-0 groups at boot regardless of device
status, this could be a contributing factor.

## Limitations

- Capture duration only ~375 ms at 1 MHz (should have been 3 s); `fx2lafw`
  continuous mode or buffer limitation. A longer capture at lower sample rate
  would help distinguish timing issues from stuck-pin issues.
- No serial console captured from test app to verify program boot/logic. Serial
  MCP tool lists ports but returns schema error:
  ```
  MCP error -32602: ... data/ports/3 must have required property 'vid' ...
  ```
- No second capture attempt made with alternative sample rate due to time
  constraints.

## Receiver restore

Receiving firmware rebuilt and flashed successfully. 447,124 bytes written and
verified. Image: `build/nrf54l15/merged.hex`. No new build warnings.

## Next step

Phase is **BLOCKED**. Next action: debug why P1.5 (D1) is stuck HIGH and P1.6
(D2) shows only a single edge instead of the coded pulse pattern. Options:

1. Re-run with longer capture (lower sample rate) to confirm D1/D2 behavior over
   multiple seconds
2. Try a fixed 500 ms HIGH / 500 ms LOW pattern on all three pins (no pulses)
   to simplify diagnosis
3. Probe P1.5/P1.6 with a multimeter to check for external pull-up/pull-down
4. Add a UART log line in the test app loop to confirm `step` counter advancing
5. Check nRF54L15 Errata for known GPIO limitations on specific port 1 pins
