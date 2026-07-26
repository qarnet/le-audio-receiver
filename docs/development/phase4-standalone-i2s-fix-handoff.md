# Phase 4 Standalone I2S Test — Driver-Starvation Fix and Real Capture

Status: required correction after `a693c99`

## Review findings

Current standalone test did **not** meet acceptance:

- it failed `i2s_write(...): -EIO` after 565 ms, not 15 seconds;
- `released == NULL` in `i2s_nrfx` means a real missing-next-buffer event,
  not a first-event false positive;
- `PCLK32M_HFXO` does not require MPSL/SDC. The I2S driver requests Nordic
  HF clock-control manager. Current fallback claim is unsupported;
- no analyzer capture was taken despite analyzer being available in earlier
  phases.

Do not call digital I2S PASS until this phase passes.

## Scope

Edit only:

- `tests/hardware/nrf54l15_i2s_output/src/main.c`
- `tests/hardware/nrf54l15_i2s_output/prj.conf` if queue depth increases
- `tests/hardware/nrf54l15_i2s_output/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay`
- `docs/development/phase4-standalone-i2s-dac-results.md`
- this handoff document

## Required implementation changes

1. Restore `clock-source = "PCLK32M_HFXO"` or omit it to use binding default.
   Do not use HFINT/PCLK32M fallback in this clean test. If START fails, report
   exact error and resolved config; do not change source again.
2. In feed loop, replace polling `k_mem_slab_alloc(..., K_NO_WAIT)` plus
   `k_sleep(1ms)` with `k_mem_slab_alloc(..., K_FOREVER)`, then fill and queue
   block immediately. This avoids producer gap after a released DMA block.
3. Keep ownership correct: each successful `i2s_write` transfers exactly one
   unique slab block to driver; never free or requeue that pointer.
4. Increase source slab block count and `CONFIG_I2S_NRFX_TX_BLOCK_COUNT`
   together to 16 for added scheduling margin. Preserve 10 ms / 1920-byte
   blocks. Pre-fill every block before START.
5. Keep output run duration 20 seconds. Test fails on any `i2s_write` EIO,
   driver underrun, or premature end.
6. Correct all results claims that call NULL-released event false or call the
   prior 565 ms run PASS.

## Required hardware verification

1. Build and flash temporary test as before. New DAC remains connected using:
   D0→BCK, D1→LRCK/WSEL, D2→DIN, common GND. MUTE/strap state recorded.
2. Use serial-mcp if available to capture full test result. If unavailable,
   record exact MCP failure and use analyzer/register evidence.
3. Capture sigrok fx2lafw at 24 MHz for 0.5–1 seconds while transfer is active:
   CH0=D0, CH1=D1, CH2=D2, CH3=3V3. Preserve artifact under `/tmp`.
4. Measure:
   - BCK close to 1.536 MHz;
   - LRCK close to 48 kHz;
   - exact ratio 32;
   - SDOUT transitions;
   - 3V3 high.
5. Make read-only OpenOCD register snapshot during active transfer. Validate
   secure I2S20 addresses and capture EVENTS, ENABLE, CONFIG, RATIO, PSEL.
   No halt/reset/write in snapshot session.
6. Restore receiver only after test/capture. Build main before restore because
   test does not modify receiver sources but repo policy requires scanning full
   current build output for warnings.

## Acceptance

PASS requires all:

- full 20-second test completes with no I2S EIO/underrun;
- analyzer proves BCK/LRCK/SDOUT and correct ratio;
- active register snapshot confirms enabled master/TX configuration and PSEL;
- receiver rebuilt and restored cleanly.

DAC audible result remains user-only evidence. Record it pending unless user
reports it explicitly.

## Constraints

- Preserve existing unstaged receiver diagnostics exactly.
- No main receiver source/overlay modifications, no erase/recover, no probe-rs.
- Commit only scoped files. New commit; no amend, push, merge, or PR.

## Executor recap

Return actual full-duration UART/analyzer/register evidence, any failure exact
code/log, restore evidence, commit hash/message, and user action needed.
