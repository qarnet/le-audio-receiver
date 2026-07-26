# Phase 4 I2S Investigation — Standalone XIAO I2S DAC Test

Status: new DAC connected; ready for hardware execution

## Goal

Prove or disprove nRF54L15 I2S20 master-mode output independently from BLE,
LC3, receiver slab logic, drift control, and stock-DK overlays. Use a new DAC
but treat logic-analyzer electrical evidence as primary; audible sound is a
separate user observation.

## Wiring contract

| Xiao | SoC / I2S | DAC |
|---|---|---|
| D0 | P1.04 / SCK_M | BCK |
| D1 | P1.05 / LRCK_M | LRCK / WSEL |
| D2 | P1.06 / SDOUT | DIN |
| GND | — | GND and AGND |

D2 is P1.06. Do not use P1.07; it is D3.

For a UDA1334A, MUTE must be low for audible output. For a PCM5102A clone,
confirm its board-specific SCK/MCK 3-wire strap separately. This phase must
not modify the DAC wiring or persistent configuration.

## Scope

Add standalone test app:

```
tests/hardware/nrf54l15_i2s_output/
├── CMakeLists.txt
├── prj.conf
├── src/main.c
└── boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay
```

Build only for official `xiao_nrf54l15/nrf54l15/cpuapp` target.

## Implementation

1. Configure `i2s20` as master with:
   - 48 kHz frame clock;
   - 16-bit, stereo, standard I2S format;
   - `I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER`;
   - SCK=P1.04, LRCK=P1.05, SDOUT=P1.06;
   - optional MCK=P1.07, not connected to DAC;
   - `clock-source = "PCLK32M_HFXO"` first. Do not carry main-app diagnostic
     `PCLK32M` selection into this clean test.
2. Generate a continuous, bounded-amplitude stereo sine wave (1 kHz is fine)
   in DMA blocks. Start only after pre-queuing enough distinct slab blocks.
   Do not queue same memory block twice. Continue feeding unique buffers so
   transfer runs for at least 15 seconds.
3. Log only start/configure/write/trigger errors and periodic bounded status;
   no per-block log spam.
4. On error, log errno and preserve device state. Do not add recovery logic
   copied from receiver; test should expose raw driver behavior.

## Hardware capture

1. Flash temporary test image through dynamic `nrf-probes --find nrf54l` and
   current OpenOCD Xiao loader. Never hardcode probe serial.
2. Capture analyzer at 24 MHz for 0.5–1 s while output runs:
   CH0=D0/P1.04, CH1=D1/P1.05, CH2=D2/P1.06, CH3=3V3.
3. Expected electrical output:
   - BCK: approximately **1.536 MHz** (`48 kHz × 2 × 16`);
   - LRCK: 48 kHz;
   - BCK/LRCK ratio: 32;
   - SDOUT: changing serial data;
   - 3V3: stable high.
4. During active output, use read-only OpenOCD snapshots. I2S20 is secure for
   this flat/secure test image; base `0x500DD000`:

   ```tcl
   mdw 0x500DD108 6 ;# STOPPED, RXPTRUPD, TXPTRUPD, FRAMESTART, error/event area
   mdw 0x500DD500 7 ;# ENABLE, CONFIG, MODE, RATIO and adjacent fields
   mdw 0x500DD560 5 ;# PSEL.MCK/SCK/LRCK/SDIN/SDOUT
   ```

   Confirm exact offsets from generated MDK/header before recording results.
   No `mww`, halt, reset, recovery, or probe-rs during snapshots.
5. Restore receiver with `fw-flash-54l15` after capture. Build first only if
   source/config changed; preserve existing unstaged receiver diagnostics.

## Results document

Create `docs/development/phase4-standalone-i2s-dac-results.md` containing:

- DAC model if user identifies it, exact wiring state, MUTE/strap state;
- build/flash/capture commands and raw artifact path;
- measured BCK/LRCK/data behavior and ratio;
- register snapshots with decoded PSEL values;
- UART/serial results if serial MCP works; record schema failure if it does not;
- user audible result as **pending** unless user directly confirms it;
- PASS/BLOCKED decision and narrow next step.

## Acceptance

Digital I2S pass requires active BCK, LRCK, and SDOUT at expected relation for
at least 15 seconds with no driver error/queue starvation. Audible DAC result
requires user confirmation; do not infer it from signal capture.

## Verification

```bash
nix flake check --no-build
west build -b xiao_nrf54l15/nrf54l15/cpuapp \
  tests/hardware/nrf54l15_i2s_output --pristine \
  -d build/test-nrf54l15-i2s-output
fw-build-54l15
fw-build-5340
```

## Constraints

- Preserve unstaged receiver diagnostics in receiver overlay and `src/bt_bap.c`.
- Do not change main receiver target or its existing diagnostic modifications.
- No erase/recover/persistent settings change.
- Commit only new test, results, and this handoff. Do not push, amend, merge,
  or open a PR.

## Executor recap

Return changed files, analyzer measurements, I2S register evidence, test/build
and restore results, commit hash/message, blocker, and exact user action needed
for audible confirmation.
