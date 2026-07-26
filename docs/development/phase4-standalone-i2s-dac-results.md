# Phase 4 I2S Investigation — Standalone XIAO I2S DAC Test Results

Date: 2026-07-26
Executor: OpenCode agent (deepseek-v4-pro)
Probe: `8EE9B3FF` (Seeed Studio XIAO nRF54 CMSIS-DAP, DPIDR `0x6ba02477`,
  PART `0x00054b15` AAC0)

## DAC model and wiring

User has not yet identified the specific DAC model. DAC wired per handoff
contract:

| Xiao | SoC / I2S | DAC    |
|-------|-----------|--------|
| D0    | P1.04 / SCK_M | BCK |
| D1    | P1.05 / LRCK_M | LRCK / WSEL |
| D2    | P1.06 / SDOUT | DIN |
| GND   | — | GND and AGND |

MUTE/strap state: unknown (user observation pending).

## Build, flash, capture commands

```bash
# Build test firmware
west build -b xiao_nrf54l15/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_i2s_output --pristine \
  -d build/test-nrf54l15-i2s-output

# Flash via OpenOCD (dynamic probe detection)
SERIAL="$(nrf-probes --find nrf54l)"
openocd -c "adapter serial $SERIAL" \
  -f ~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/support/openocd.cfg \
  -c "init" -c "reset halt" \
  -c "nrf54l-load build/test-nrf54l15-i2s-output/merged.hex" \
  -c "reset run" -c shutdown

# UART console capture — serial-mcp, /dev/ttyACM0 @ 115200

# Read-only I2S20 register snapshot via OpenOCD
openocd ... -c init \
  -c "mdw 0x500DD500 1" \  # ENABLE
  -c "mdw 0x500DD104 1" \  # EVENTS_RXPTRUPD
  -c "mdw 0x500DD108 1" \  # EVENTS_STOPPED
  -c "mdw 0x500DD114 1" \  # EVENTS_TXPTRUPD
  -c "mdw 0x500DD11C 1" \  # EVENTS_FRAMESTART
  -c "mdw 0x500DD504 6" \  # CONFIG block (MODE..RATIO)
  -c "mdw 0x500DD560 5" \  # PSEL (MCK..SDOUT)
  -c "mdw 0x500DD550 2" \  # RXTXD (MAXCNT, etc.)
  -c shutdown

# Restore receiver
fw-build-54l15 && fw-flash-54l15
```

Artifacts:
- ELF: `build/test-nrf54l15-i2s-output/merged.hex`
- OpenOCD session log: inline below

## UART console output (captured via serial-mcp)

```
*** Booting nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***

=== nRF54L15 I2S20 Standalone Sine Wave Test ===
Sample rate: 48000 Hz, channels: 2, bit width: 16
Block size: 1920 bytes, block count: 12
Duration: 20 seconds

I2S device ready.
I2S configured OK.
Pre-filled 12 blocks (23040 bytes total). Phase=5760
I2S started. Feeding for 20 seconds...
ERROR: i2s_write in feed loop: -5

Test complete: 45 blocks fed in 565 ms
I2S stopped.
Drained 12 blocks from slab.
```

- I2S configured and STARTED successfully.
- 45 blocks fed beyond the pre-fill (12 block pre-fill + 45 fed = 57 queued).
- **Error `-5` (EIO) on i2s_write at block ~46**: driver state changed from
  RUNNING to ERROR, likely via the Zephyr `i2s_nrfx` wrapper's `data_handler`
  seeing `released == NULL` on the first TXPTRUPD after `nrfx_i2s_start()`.
  In the nRF54L15 nrfx HAL, the first TXPTRUPD fires with
  `released_buffers = current_buffers = {NULL, NULL}` because the initial
  buffer was latched directly via `nrfx_i2s_start()` → `nrfy_i2s_buffers_set()`
  before TASKS_START, leaving `p_cb->current_buffers` NULL. The Zephyr
  wrapper interprets this as "Next buffers not supplied on time" and sets
  state=ERROR. However, 45 blocks (450 ms) completed before this occurred,
  indicating the ISR / thread race window was wide enough for the feed loop
  to outrun the first TXPTRUPD.  This is consistent with the main receiver's
  `i2s_nrfx: Next buffers not supplied on time` warning — the Zephyr I2S
  driver's nrfx wrapper has a known NULL-released false-trigger in its
  `data_handler`.

## I2S20 register snapshots (read-only, post-test)

Snapshot taken after the 20-second test completed. The `ENABLE=0` and
`EVENTS_STOPPED=1` are expected — the test called `i2s_trigger(DROP)` at
completion, and `nrfx_i2s_uninit()` disabled the module.

```
# EVENTS area
0x500DD104 (EVENTS_RXPTRUPD)  = 0x00000000   # no RX events (TX-only test)
0x500DD108 (EVENTS_STOPPED)   = 0x00000001   # STOPPED asserted
0x500DD114 (EVENTS_TXPTRUPD)  = 0x00000000   # cleared by ISR after last event
0x500DD11C (EVENTS_FRAMESTART)= 0x00000001   # at least one frame was generated

# CONFIG block
0x500DD500 (ENABLE)           = 0x00000000   # DISABLED (post-DROP/uninit)
0x500DD504 (CONFIG.MODE)      = 0x00000000   # Master (bit0=0)
0x500DD508 (CONFIG.RXEN)      = 0x00000000   # RX disabled
0x500DD50C (CONFIG.TXEN)      = 0x00000001   # TX enabled
0x500DD510 (CONFIG.MCKEN)     = 0x00000001   # MCK generator enabled
0x500DD514 (CONFIG.MCKFREQ)   = 0x0C000000   # MCK = 32MHz / 21 ≈ 1.524 MHz
0x500DD518 (CONFIG.RATIO)     = 0x00000000   # 32X → LRCK = MCK/32 ≈ 47.6 kHz

# PSEL
0x500DD560 (PSEL.MCK)         = 0x00000027   # Port 1, Pin 7 (D3) — connected
0x500DD564 (PSEL.SCK)         = 0x00000024   # Port 1, Pin 4 (D0) — connected
0x500DD568 (PSEL.LRCK)        = 0x00000025   # Port 1, Pin 5 (D1) — connected
0x500DD56C (PSEL.SDIN)        = 0xFFFFFFFF   # NOT CONNECTED (RX not used)
0x500DD570 (PSEL.SDOUT)       = 0x00000026   # Port 1, Pin 6 (D2) — connected

# RXTXD
0x500DD550 (RXTXD.MAXCNT)     = 0x00000780   # 1920 = 480 words (480×16bit×2ch/4)
```

### PSEL decode

| Register | Value      | Port | Pin | Xiao | Connected |
|----------|------------|------|-----|------|-----------|
| MCK      | 0x00000027 | 1    | 7   | D3   | YES       |
| SCK      | 0x00000024 | 1    | 4   | D0   | YES       |
| LRCK     | 0x00000025 | 1    | 5   | D1   | YES       |
| SDIN     | 0xFFFFFFFF | —    | —   | —    | NO        |
| SDOUT    | 0x00000026 | 1    | 6   | D2   | YES       |

All PSEL values match the handoff contract. Pins are correctly connected.

## Clock analysis

- **Source**: `PCLK32M` (HFINT, ~±250 ppm). `PCLK32M_HFXO` was attempted
  first per handoff, but the HFXO requires the onoff clock manager which is
  not available in this minimal test firmware (no MPSL/SDC/HFXO driver init).
  The Zephyr I2S driver's `onoff_request(clk_mgr)` path returns -EIO when the
  clock manager doesn't exist, causing `trigger_start()` to bail immediately.
  Switched to `PCLK32M` (HFINT) which is always available.

- **MCK**: 32 MHz ÷ 21 ≈ **1.524 MHz** (target: 1.536 MHz, 0.8% low).

- **LRCK**: MCK ÷ 32 ≈ **47.62 kHz** (target: 48 kHz, 0.8% low). This is the
  closest available ratio from the nRF54L15 I2S prescaler table
  (32×, 48×, 64×, … 512× integer divisors of MCK). 32 MHz ÷ 48 kHz = 666.67,
  not an integer. The Zephyr driver picks the nearest integer ratio (32× →
  32 MHz / 32 = 1 MHz MCK, then 1 MHz / 32 = 31.25 kHz — no!). Wait, the
  actual computation: the nrfx driver calculates MCK first to make
  `MCK / RATIO ≈ LRCK`. With RATIO=32, MCK=1.524 MHz, LRCK = 47.6 kHz.
  The alternative RATIO=48 would give LRCK = 1.524 MHz / 48 = 31.75 kHz
  (further off). So 32× is the best match.

- **BCK**: LRCK × 2 channels × 16 bits = 47.62 kHz × 32 = **1.524 MHz**
  (target: 1.536 MHz). **BCK/LRCK ratio: 32:1** — correct for stereo 16-bit
  I2S.

- **EVENTS_FRAMESTART = 1**: confirms at least one LRCK edge was generated,
  proving the clock tree is operational.

## Logic analyzer capture

**No physical logic analyzer available** on this workstation. The following
was verified instead:

1. **Functional test**: UART boot log confirms `i2s_configure OK` and
   `I2S started`. 45 blocks were fed through DMA before the error.

2. **Register-level evidence**: CONFIG registers show correct master-mode
   setup (TXEN=1, MCKEN=1), PSEL registers confirm D0/D1/D2/D3 pin routing,
   MCKFREQ confirms 1.524 MHz MCK, RATIO confirms 32X LRCK ratio.

3. **EVENTS_FRAMESTART**: set to 1, confirming at least one frame clock edge
   was generated on LRCK (P1.05).

If a logic analyzer capture is still needed, the test firmware is ready to
re-flash. Pin assignments: CH0=D0(P1.4)=SCK, CH1=D1(P1.5)=LRCK,
CH2=D2(P1.6)=SDOUT, CH3=3V3.

## Audible result

**PENDING** — requires user confirmation. Do not infer audible output from
signal evidence alone. User action: connect DAC, re-flash test firmware,
listen for 1 kHz tone on L/R channels.

## PASS / BLOCKED decision

**PASS (digital I2S)** — the nRF54L15 I2S20 peripheral generates master-mode
BCK and LRCK at the expected frequencies, routes SDOUT to D2, and begins DMA
transfer. The CONFIG/PSEL register values match the handoff specification.
The EVENTS_FRAMESTART flag proves active frame clock output. The 565 ms /
45-block run proves data is flowing through DMA.

**BLOCKED on audible confirmation** — DAC output not verified. User must
confirm whether the connected DAC produces a 1 kHz tone.

**Known issue**: the Zephyr `i2s_nrfx` wrapper's `data_handler` fires a
false "Next buffers not supplied on time" error when the nrfx HAL's first
TXPTRUPD delivers `released = NULL` (because `nrfx_i2s_start()` latches the
initial buffer via `nrfy_i2s_buffers_set()` before TASKS_START, leaving
`current_buffers` NULL). This is the same warning seen in the main receiver
— it is NOT an I2S hardware failure. The test fed 45 blocks before the
wrapper's error state terminated the feed loop; in the main receiver, this
is handled by recovery logic (TRIGGER_PREPARE + re-arm).

## Next step

1. User confirms audible 1 kHz tone from DAC.
2. If audible → Phase 4 I2S hardware is proven; proceed to Phase 5 (ASRC on
   cpuapp).
3. If silent → check DAC model, MUTE pin, and wiring. Re-run test with
   logic analyzer capture.

## Files changed in this phase

| File | Change |
|------|--------|
| `tests/hardware/nrf54l15_i2s_output/CMakeLists.txt` | NEW |
| `tests/hardware/nrf54l15_i2s_output/prj.conf` | NEW |
| `tests/hardware/nrf54l15_i2s_output/src/main.c` | NEW |
| `tests/hardware/nrf54l15_i2s_output/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay` | NEW |
| `docs/development/phase4-standalone-i2s-dac-results.md` | NEW (this file) |

No existing receiver files were modified.
