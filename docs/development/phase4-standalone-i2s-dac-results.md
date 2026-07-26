# Phase 4 I2S Investigation — Standalone XIAO I2S DAC Test Results

Date: 2026-07-26
Executor: OpenCode agent (deepseek-v4-pro)
Probe: `8EE9B3FF` (Seeed Studio XIAO nRF54 CMSIS-DAP, DPIDR `0x6ba02477`,
  PART `0x00054b15` AAC0)

## Corrections to prior run (`a693c99`)

The prior results document called the `i2s_write: -5` at 565 ms a "known
NULL-released false-trigger" and claimed the test PASSed.  Both claims are
**false**:

- `released == NULL` in the Zephyr `data_handler` (line 191 of
  `i2s_nrfx.c`) is a **real driver starvation**: the `nrfx_i2s_start()`
  HAL latches initial buffers as `next_buffers` and sets `current_buffers`
  to `NULL/NULL` (lines 337–342 of `nrfx_i2s.c`).  The first TXPTRUPD
  ISR copies `current_buffers` (NULL) as `released`, and the Zephyr
  wrapper sets state=ERROR when it sees `released == NULL` at line 191.
  This is **not a false trigger** — it is the standard nrfx HAL design
  for the first block.
- The K_NO_WAIT + k_sleep(1ms) poll loop was too slow to keep the msgq
  populated.  `sin(double)` runs software-emulated double-precision FP
  on Cortex-M33's single-precision FPv5-SP FPU, taking ~12.6 ms per
  480-sample block (needs < 10 ms to stay ahead of I2S).  With 12 blocks
  (120 ms headroom), the deficit exhausted the msgq in ~45–64 additional
  blocks → the driver ran out of queued buffers → `released == NULL`
  → state=ERROR → `-EIO`.

The 565 ms / 45-block run was a **FAIL**, not a PASS.

## Fixes applied

| File | Change |
|------|--------|
| `main.c` | Replaced `sin(double)` with 48-entry LUT indexed by phase (uses `sinf()` at init only). Feed loop uses `K_FOREVER` slab alloc (no poll gap). Block count 12→16. Status every 100 blocks. |
| `prj.conf` | `CONFIG_I2S_NRFX_TX_BLOCK_COUNT` 12→16. `CONFIG_MAIN_STACK_SIZE=4096`. |
| `overlay` | **Attempted** `clock-source = "PCLK32M_HFXO"` (binding default, no explicit property): USAGE FAULT crash at `pc=0x1320` (literal pool). The onoff callback chain (`clock_started_callback` → `start_transfer` → `nrfx_i2s_init`) triggered an illegal EPSR fault on nRF54L15 I2S20 with PCLK32M_HFXO. **Fell back to `clock-source = "PCLK32M"`** (HFINT, ~±250 ppm), which works reliably. PCLK32M_HFXO requires further investigation. |

## Final test run — PASS

### UART console output

```
*** Booting nRF Connect SDK v3.3.0-ba167d9f3db4 ***
*** Using Zephyr OS v4.3.99-fd9204a02d52 ***

=== nRF54L15 I2S20 Standalone Sine Wave Test ===
Sample rate: 48000 Hz, channels: 2, bit width: 16
Block size: 1920 bytes, block count: 16
Duration: 20 seconds

I2S device ready.
I2S configured OK.
Pre-filled 16 blocks (30720 bytes total). Phase=7680
I2S started. Feeding for 20 seconds...
STATUS: 100 blocks fed, free=0
STATUS: 200 blocks fed, free=0
STATUS: 300 blocks fed, free=0
...
STATUS: 1900 blocks fed, free=0
STATUS: 2000 blocks fed, free=0

Test complete: 2016 blocks fed in 20001 ms
I2S stopped.
Drained 16 blocks from slab.
```

- **2016 blocks fed in 20001 ms** — full 20-second duration.
- **Zero EIO/underrun errors** — no `i2s_write: -5`, no "Next buffers not supplied".
- `free=0` at every status line: slab is fully utilized, a block is always
  either in the DMA pipeline or being refilled. The LUT-based fill is fast
  enough to stay ahead.
- `Drained 16 blocks from slab` — all blocks accounted for after stop.

### I2S20 register snapshot (read-only, during active transfer)

Taken via separate OpenOCD session (no halt) while test was running:

```
ENABLE           0x500DD500 = 0x00000001  → ENABLED
EVENTS_RXPTRUPD  0x500DD104 = 0x00000000  → no RX (TX-only)
EVENTS_STOPPED   0x500DD108 = 0x00000000  → NOT stopped
EVENTS_TXPTRUPD  0x500DD114 = 0x00000000  → cleared by ISR (processed)
EVENTS_FRAMESTART 0x500DD11C = 0x00000001 → frame clock active

CONFIG.MODE      0x500DD504 = 0x00000000  → Master
CONFIG.RXEN      0x500DD508 = 0x00000000  → RX disabled
CONFIG.TXEN      0x500DD50C = 0x00000001  → TX enabled
CONFIG.MCKEN     0x500DD510 = 0x00000001  → MCK generator enabled
CONFIG.MCKFREQ   0x500DD514 = 0x0C000000  → MCK = 32MHz ÷ 21 ≈ 1.524 MHz
CONFIG.RATIO     0x500DD518 = 0x00000000  → 32× → LRCK = MCK/32 ≈ 47.6 kHz

PSEL.MCK         0x500DD560 = 0x00000027  → P1.7 (D3)
PSEL.SCK         0x500DD564 = 0x00000024  → P1.4 (D0)
PSEL.LRCK        0x500DD568 = 0x00000025  → P1.5 (D1)
PSEL.SDIN        0x500DD56C = 0xFFFFFFFF  → not connected
PSEL.SDOUT       0x500DD570 = 0x00000026  → P1.6 (D2)

RXTXD.MAXCNT     0x500DD550 = 0x00000780  → 1920 bytes
```

ENABLE=1 + STOPPED=0 proves the I2S was actively transferring during
snapshot. PSEL values match expected pin map (D0/D1/D2/D3). MAXCNT
matches configured block size.

### Logic analyzer capture

**fx2lafw @ 24 MHz** (3 captures in `/tmp/`):

| File | Run | Size |
|------|-----|------|
| `nrf54l15-i2s-capture.sr` | Failed run 1 (PCLK32M_HFXO) | 19 KB |
| `nrf54l15-i2s-capture-v2.sr` | Failed run 2 (PCLK32M_HFXO) | 353 KB |
| `nrf54l15-i2s-capture-v3.sr` | PASS run (PCLK32M) | 366 KB |

All captures on CH0=D0(SCK), CH1=D1(LRCK), CH2=D2(SDOUT), CH3=3V3.

The fx2lafw captures ~490 ms of 24 MHz data per run (USB bandwidth limit).
Each capture spans ~490 ms of the 20-second test. Captures are raw,
un-decoded; BCK/LRCK/SDOUT edge-rate, ratio, and SDOUT-transition
analysis have not been extracted from them.

### DAC

**Audible result: PENDING** — requires user confirmation with connected DAC
and headphone/speaker.  The test generates continuous 1 kHz stereo sine
wave at ~80% amplitude.  DAC model not yet identified.

## Acceptance

| Criterion | Status |
|-----------|--------|
| Full 20-second test, no I2S EIO/underrun | **PASS** — 2016 blocks in 20001 ms, zero errors |
| Register snapshot confirms enabled master/TX, PSEL | **PASS** — ENABLE=1, CONFIG.TXEN=1, all PSEL match |
| Analyzer proves BCK/LRCK/SDOUT, correct ratio | **PENDING ANALYSIS** — raw sigrok captures exist but BCK/LRCK/SDOUT measurements were not extracted. Register config (MCKFREQ=32MHz÷21, RATIO=32×) and FRAMESTART=1 confirm clock generation from digital side; external waveform validation requires capture decoding. |
| Receiver rebuilt and restored | **PASS** — `fw-build-54l15 && fw-flash-54l15` clean |
| PCLK32M_HFXO clock source | **BLOCKED** — USAGE FAULT (Illegal use of EPSR, pc=0x1320) in onoff callback chain; root cause TBD. Fell back to PCLK32M (HFINT) which works. |
| DAC audible | **PENDING** — user confirmation |

## Known issues

1. **PCLK32M_HFXO crashes nRF54L15 I2S20**: the `clock_started_callback`
   → `start_transfer` path triggers a USAGE FAULT (EPSR illegal use) at
   `pc=0x00001320` (literal pool).  This happens during
   `nrfx_i2s_init` → `nrfx_i2s_start` called from the onoff manager
   callback, not from thread context.  Does NOT crash with `PCLK32M`
   (which skips the onoff request entirely).  This needs investigation
   but is not a blocker for the main receiver (which also uses PCLK32M).

2. **`sin(double)` is too slow** on Cortex-M33 FPv5-SP.  Use LUT or
   `sinf(float)` for real-time sine/tone generation.

## Build, flash, restore commands

```bash
# Build standalone test
west build -b xiao_nrf54l15/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_i2s_output --pristine \
  -d build/test-nrf54l15-i2s-output

# Flash test
SERIAL="$(nrf-probes --find nrf54l)"
openocd -c "adapter serial $SERIAL" \
  -f ~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/support/openocd.cfg \
  -c "init" -c "reset halt" \
  -c "nrf54l-load build/test-nrf54l15-i2s-output/merged.hex" \
  -c "reset run" -c shutdown

# UART console: /dev/ttyACM0 @ 115200 8N1

# Logic analyzer: fx2lafw conn=5.22, 24 MHz
sigrok-cli --driver=fx2lafw:conn=5.22 --config samplerate=24M \
  --channels D0=D0,D1=D1,D2=D2,D3=D3 --time 800 \
  -o /tmp/nrf54l15-i2s-capture.sr

# Restore receiver
fw-build-54l15 && fw-flash-54l15
```
