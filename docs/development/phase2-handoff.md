# Phase 2 — App Restructure — Implementation Handoff

Status: **ready for implementation**
Phase plan source: `docs/design.md` §Phase 2

## Goal

Split `main.c` into focused modules, introduce a platform-neutral audio-sink
interface between decode/volume and the I2S backend, and restrict the PACS
capability to 48 kHz (fixes design.md F4). External behavior on nRF5340 must
remain identical. The LC3 decode + channel routing math must become unit-
testable (no Zephyr BT dependencies in the routing logic).

## Decisions locked

1. **Split granularity**: 4 modules + 1 interface header. New files:
   - `src/bt_bap.c` + `src/bt_bap.h` — BT setup, pairing, ASCS callbacks,
     advertising, PACS registration, connection management.
   - `src/audio_decode.c` + `src/audio_decode.h` — LC3 decode + channel
     routing (mono / Mode A / Mode B). Unit-testable: the routing math takes
     raw LC3 frames + codec params → interleaved stereo PCM, with no BT deps
     in the pure routing functions.
   - `src/audio_sink.h` — platform-neutral interface: `init`, `push`, `stop`,
     `sdu_ref_update`. The I2S backend (`audio_i2s.c`) implements it.
   - `src/main.c` — shrinks to: watchdog, `main()` lifecycle wiring
     (init order), and the advertising restart loop. Delegates to `bt_bap_*`
     and `audio_*` functions.

2. **PACS 48 kHz fix**: change `lc3_codec_cap` in `bt_bap.c` to advertise
   only `BT_AUDIO_CODEC_CAP_FREQ_48KHZ` (drop 16K and 24K). Keep 7.5/10 ms
   frame durations, 1-2 channel support, octet range 20-120. This fixes F4
   so sources can no longer configure a non-48 kHz stream that the I2S can't
   play correctly.

3. **Audio-sink interface shape**: the existing `audio_i2s.h` API IS the
   interface. Rename it to `audio_sink.h` and have `audio_i2s.c` implement
   it. The interface is:
   - `int audio_sink_init(void);` (was `audio_i2s_init`)
   - `int audio_sink_push(const int16_t *stereo_data, size_t sample_count);` (was `audio_i2s_push`)
   - `void audio_sink_stop(void);` (was `audio_i2s_stop`)
   - `void audio_sink_sdu_ref_update(uint32_t sdu_ref_us);` (was `audio_i2s_sdu_ref_update`)

   `audio_i2s.c` keeps its internal I2S/slab/DMA logic but exposes the
   functions under the `audio_sink_*` names. The drift compensation
   (`audio_drift.c`) stays called from inside `audio_i2s.c` as today —
   Phase 3 refactors that. **No functional change** — just a rename of the
   public API + header file.

4. **Unit test target**: `tests/unit/decode` — tests the channel routing
   math (mono duplication, Mode A L+R interleave, Mode B split-decode) and
   the octets-per-channel calculation. Uses `native_sim` platform (same as
   `tests/unit/drift`). The decode functions must be compilable without
   Zephyr BT headers — only `lc3.h`, `stdint.h`, `string.h`, and the
   `audio_decode.h` interface.

5. **bsim test**: the existing `tests/bsim` references `src/main.c` directly.
   After the split, update its CMakeLists.txt to include the new source files.
   The bsim stub (`audio_i2s_stub.c`) must be renamed to
   `audio_sink_stub.c` and implement the `audio_sink_*` interface.

## In scope

### A. Create `src/audio_sink.h` (interface)

Rename `src/audio_i2s.h` → `src/audio_sink.h`. Update function names:
- `audio_i2s_init` → `audio_sink_init`
- `audio_i2s_push` → `audio_sink_push`
- `audio_i2s_stop` → `audio_sink_stop`
- `audio_i2s_sdu_ref_update` → `audio_sink_sdu_ref_update`

Update the Doxygen comments to be platform-neutral (remove "PCM5102A" and
"nRF5340 I2S0" references — those are implementation details of the I2S
backend, not the interface).

### B. Update `src/audio_i2s.c` to implement `audio_sink.h`

- `#include "audio_sink.h"` (replaces `#include "audio_i2s.h"`).
- Rename the four public functions to `audio_sink_*`.
- Keep `#include "audio_drift.h"` — the drift compensation call stays inside
  `audio_sink_sdu_ref_update` (Phase 3 will refactor this into the controller/
  actuator pattern).
- All internal static functions (slab, DMA, i2s_do_configure) keep their names.

### C. Create `src/audio_decode.c` + `src/audio_decode.h`

Extract the LC3 decode + channel routing from `main.c`'s `stream_recv` +
`push_stereo` + the `audio_sink` struct's decode-related fields.

**`audio_decode.h`** — public API (no BT deps):
```c
#ifndef AUDIO_DECODE_H
#define AUDIO_DECODE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(CONFIG_LIBLC3)
#include "lc3.h"
#endif

#define AUDIO_DECODE_MAX_CHANNELS 2

struct audio_decode_ctx {
    int chan_count;
    int samples_per_ch;
    int frames_per_sdu;
#if defined(CONFIG_LIBLC3)
    lc3_decoder_t decoder;
    lc3_decoder_mem_48k_t dec_mem;
    lc3_decoder_t decoder_r;
    lc3_decoder_mem_48k_t dec_mem_r;
#endif
};

/**
 * Configure decoders from codec parameters.
 * Returns 0 on success, negative errno on failure.
 */
int audio_decode_config(struct audio_decode_ctx *ctx, int chan_count,
                        int freq_hz, int frame_us, int frames_per_sdu);

/**
 * Decode one SDU into interleaved stereo PCM.
 *
 * @param ctx        Decoder context (configured).
 * @param frame_data Raw SDU payload (LC3 frames). NULL for PLC (packet loss).
 * @param frame_len  Length of frame_data in bytes.
 * @param valid       True if packet is valid (not lost/corrupt).
 * @param stereo_out  Output buffer, interleaved int16_t [L,R,L,R,...].
 *                   Must hold samples_per_ch * 2 int16_t values.
 * @return 0 on success, negative on error.
 */
int audio_decode_sdu(struct audio_decode_ctx *ctx, const uint8_t *frame_data,
                     size_t frame_len, bool valid, int16_t *stereo_out);

/**
 * Reset decoder state (call on stream start).
 */
void audio_decode_reset(struct audio_decode_ctx *ctx);

/**
 * Mono routing: duplicate a mono buffer into both stereo channels.
 * Called when num_sink_ase == 1 and chan_count == 1.
 */
void audio_decode_mono_to_stereo(const int16_t *mono, int16_t *stereo_out,
                                 int samples);

/**
 * Stereo interleave: combine separate L and R buffers into interleaved stereo.
 * Called in Mode A (2 mono ASEs) when both L and R have arrived.
 */
void audio_decode_interleave(const int16_t *l, const int16_t *r,
                             int16_t *stereo_out, int samples);

#endif /* AUDIO_DECODE_H */
```

**`audio_decode.c`** — implements the above. Extract the decode logic from
`main.c`'s `stream_recv`:
- The Mode B (stereo single-ASE, chan_count >= 2) path: split SDU per-channel,
  two independent `lc3_decode` calls with stride=2.
- The mono (chan_count == 1) path: single `lc3_decode`, then mono-to-stereo
  duplication.
- The Mode A (2 mono ASEs) path: decode into separate L/R buffers, then
  interleave. This is orchestrated by the caller (`bt_bap.c`'s `stream_recv`),
  not inside `audio_decode_sdu` — `audio_decode_sdu` handles ONE ASE's SDU.
  The L+R assembly for Mode A uses `audio_decode_interleave`.

The `audio_sink` struct from `main.c` (lines 139-151) moves: the decode
fields go into `struct audio_decode_ctx` (in `audio_decode.h`), the
`bt_bap_stream` and `recv_cnt` stay in `bt_bap.c`.

### D. Create `src/bt_bap.c` + `src/bt_bap.h`

Extract from `main.c`:
- The `lc3_codec_cap` (PACS capability) — **change FREQ to 48 kHz only**.
- The `audio_sink` struct (stream + recv_cnt + decode ctx) — keep here, it's
  the BT-facing state.
- `sink_idx`, `stream_alloc_idx` helpers.
- ASCS callbacks: `lc3_config`, `lc3_qos`, `lc3_enable`, `lc3_start`,
  `lc3_metadata`, `lc3_disable`, `lc3_stop`, `lc3_release`.
- `stream_recv` — the ISO data callback. It calls `audio_decode_sdu` +
  `audio_sink_push` (replacing inline decode + `audio_i2s_push`).
- `stream_stopped`, `stream_started`, `stream_enabled_cb`, `stream_ops`.
- Connection callbacks: `connected`, `disconnected`.
- Pairing callbacks: `pairing_accept`, `pairing_complete`, `pairing_failed`,
  `conn_auth_cb`, `conn_auth_info_cb`.
- PACS: `cap_sink`, `set_location`, `set_supported_contexts`,
  `set_available_contexts`.
- Advertising data: `ad`, `unicast_server_addata`.
- `qos_pref`.

**`bt_bap.h`** — public API:
```c
#ifndef BT_BAP_H
#define BT_BAP_H

#include <zephyr/bluetooth/audio/bap.h>
#include "audio_decode.h"

/** Initialize BAP unicast server, PACS, pairing, advertising. */
int bt_bap_init(void);

/** Restart advertising after disconnection. */
int bt_bap_restart_advertising(void);

#endif /* BT_BAP_H */
```

`bt_bap_init()` does: register conn auth callbacks, register ASCS callbacks,
register PACS cap, set location/contexts, register stream ops, create + start
advertising. It does NOT call `bt_enable` or `settings_load` — those stay in
`main()` (lifecycle ordering is critical per the gotchas).

### E. Shrink `src/main.c`

After extraction, `main.c` contains:
- Watchdog code (lines 59-111) — stays.
- `main()` — calls in order:
  1. `wdt_init()`
  2. `bt_conn_auth_cb_register` + `bt_conn_auth_info_cb_register` (stays in
     main? or moves to `bt_bap_init`? — **move to `bt_bap_init`**, the
     callbacks are defined in `bt_bap.c`)
  3. `bt_enable(NULL)` — stays in main (lifecycle)
  4. `settings_load()` — stays in main (must be after bt_enable, before pacs)
  5. `audio_volume_init()` — stays in main
  6. `bt_bap_init()` — the new init function (does PACS, ASCS, stream ops,
     advertising)
  7. `audio_sink_init()` — stays in main
  8. Advertising start + restart loop — move to `bt_bap_init` / a
     `bt_bap_start_advertising` call. The `sem_disconnected` + restart loop
     can stay in main OR move to `bt_bap.c`. **Keep the loop in main** —
     it's the app lifecycle, not BT logic.

  Actually, simplify: `bt_bap_init()` creates the adv but doesn't start it.
  `main()` starts adv, then loops on `sem_disconnected` →
  `bt_bap_restart_advertising()`. The `sem_disconnected` is defined in
  `bt_bap.c` and exposed via a getter or a callback. **Simplest**: keep
  `sem_disconnected` in `bt_bap.c`, expose `bt_bap_wait_disconnect()` that
  does `k_sem_take(&sem_disconnected, K_FOREVER)`. Main calls it in the loop.

### F. Fix PACS 48 kHz (F4)

In `bt_bap.c`, change the `lc3_codec_cap`:
```c
static const struct bt_audio_codec_cap lc3_codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_48KHZ,
	BT_AUDIO_CODEC_CAP_DURATION_7_5 | BT_AUDIO_CODEC_CAP_DURATION_10,
	BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1) | BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(2), 20u,
	120u, 1u, AVAILABLE_SINK_CONTEXT);
```

Only `BT_AUDIO_CODEC_CAP_FREQ_48KHZ` — drop `FREQ_16KHZ | FREQ_24KHZ`.

### G. Update CMakeLists.txt

Replace:
```cmake
target_sources(app PRIVATE
  src/main.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_stats.c
  src/audio_volume.c
)
```
with:
```cmake
target_sources(app PRIVATE
  src/main.c
  src/bt_bap.c
  src/audio_decode.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_stats.c
  src/audio_volume.c
)
```

`audio_shell.c` stays conditional on `CONFIG_SHELL`.

### H. Create `tests/unit/decode`

New unit test directory:
```
tests/unit/decode/
├── CMakeLists.txt
├── prj.conf
├── testcase.yaml
└── src/
    └── test_decode.c
```

**`CMakeLists.txt`**:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_decode)

target_sources(app PRIVATE
  src/test_decode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_decode.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
```

**`prj.conf`**:
```
CONFIG_ZTEST=y
CONFIG_LIBLC3=y
```

**`testcase.yaml`**:
```yaml
tests:
  audio.decode:
    platform_allow: native_sim
    integration_platforms:
      - native_sim
```

**`test_decode.c`** — test cases:
1. `test_mono_to_stereo` — `audio_decode_mono_to_stereo` duplicates mono into
   both channels. Verify `stereo_out[2*i] == stereo_out[2*i+1] == mono[i]`.
2. `test_interleave` — `audio_decode_interleave` produces correct L/R/L/R
   ordering. Verify `stereo_out[2*i] == l[i]` and `stereo_out[2*i+1] == r[i]`.
3. `test_config` — `audio_decode_config` sets chan_count, samples_per_ch,
   frames_per_sdu correctly for 48 kHz / 10 ms / mono and / stereo.
4. `test_decode_mono_sdu` — decode a mono SDU, verify output length and
   non-zero samples (construct a minimal valid LC3 frame or use PLC path
   with `valid=false` to test the error path).
5. `test_octets_per_channel` — verify the Mode B octets-per-channel
   calculation: `(sdu_len / frames_per_sdu) / chan_count`.

The decode tests that call `lc3_decode` need `CONFIG_LIBLC3=y`. For the
PLC path (`valid=false`), `lc3_decode` with NULL input produces a
comfort-noise frame — test that it doesn't crash and produces output.

### I. Update bsim test

- Rename `tests/bsim/src/audio_i2s_stub.c` → `tests/bsim/src/audio_sink_stub.c`.
- Update the stub to implement `audio_sink_*` (not `audio_i2s_*`).
- Update `tests/bsim/CMakeLists.txt`:
  - `src/main.c` → `src/main.c src/bt_bap.c src/audio_decode.c`
  - `audio_i2s_stub.c` → `audio_sink_stub.c`
  - Add `src/audio_i2s.c` to the source list (the bsim test needs the real
    I2S backend compiled, but the stub overrides the functions — OR the stub
    provides empty implementations and `audio_i2s.c` is NOT included.
    Keep the existing pattern: the stub provides empty `audio_sink_*`
    implementations, `audio_i2s.c` is NOT in the bsim source list.)

  Actually, check the current bsim CMakeLists: it does NOT include
  `audio_i2s.c` — it uses the stub. So just rename the stub + update the
  function names. Add `bt_bap.c` + `audio_decode.c` to the bsim source list.
  The bsim test may need BT headers — it already includes `main.c` which
  pulls BT, so it should be fine.

### J. Update AGENTS.md

- "Key Files" table: add `bt_bap.c`, `audio_decode.c`, `audio_sink.h`.
  Remove or update the `audio_i2s.h` row (it's now `audio_sink.h`).
  Update `main.c` description to "lifecycle wiring + watchdog + advertising
  loop".
- "Stack" section: mention the audio-sink interface and decode/routing module.
- Plan-of-record status: update to "Phases 0–2 complete" (but only after
  review — the executor should write "Phases 0–1 complete, Phase 2 in
  progress" and the orchestrator updates to "complete" after review).
- Gotchas: the `bt_audio_codec_cfg_get_chan_allocation` returns 0 on success
  gotcha stays. The "Stereo single-ASE needs two LC3 decoders" gotcha stays.
  These are now in `bt_bap.c` / `audio_decode.c` — update the line references.
- PACS gotcha: the F4 bug is now fixed — update or remove the "Known bug:
  PACS advertises 16/24/48 kHz" bullet.

## Out of scope (DO NOT touch)

- Drift controller refactor (ppm-based PI + phase term) — Phase 3.
- APLL actuator interface — Phase 3.
- nRF54L15 audio bring-up (GRTC, SAMPLE_ADJUST) — Phase 4.
- `audio_drift.c/h` — stays as-is (Phase 3 refactors it).
- `audio_i2s.c` internal logic (slab, DMA, underrun recovery) — only the
  public function names change (rename to `audio_sink_*`).
- `audio_volume.c/h`, `audio_stats.c/h`, `audio_shell.c` — unchanged.
- `prj.conf` — unchanged.
- `boards/` — unchanged.
- `sysbuild.cmake`, `sysbuild.conf` (deleted), `Kconfig.sysbuild` — unchanged.
- `docs/design.md`, `docs/nrf54l15-drift-compensation.md` — unchanged.
- I2S pin assignments — unchanged.

## Test plan / verification

1. **nRF5340 build**:
   ```bash
   fw-build-5340
   ```
   Must complete. Same FLASH/RAM sizes as before (the code is equivalent,
   just reorganized).

2. **nRF54L15 build**:
   ```bash
   fw-build-54l15
   ```
   Must complete (same as Phase 1).

3. **Unit tests** (new):
   ```bash
   # Build + run the decode unit test
   west build -b native_sim tests/unit/decode -d build/test-decode --pristine
   ./build/test-decode/zephyr/zephyr.exe
   ```
   All test cases must pass. Report the output.

4. **Existing drift unit test still builds**:
   ```bash
   west build -b native_sim tests/unit/drift -d build/test-drift --pristine
   ```
   Must build (no source change to audio_drift.c).

5. **bsim test still builds**:
   ```bash
   west build -b native_sim tests/bsim -d build/test-bsim --pristine
   ```
   Must build (CMakeLists updated for new source files).

6. **No behavior change on nRF5340**:
   - Build the firmware, flash, capture boot log via serial-mcp on
     `/dev/ttyUSB0`. Expect: `BLE ready`, `settings_load() OK`,
     `I2S ready (48 kHz, 16-bit, stereo, 12 blocks)`,
     `Advertising as "LE Audio Receiver"`.
   - The PACS capability change means a source/scanner will see only 48 kHz
     — this is intentional (F4 fix). No source test needed (no BAP source
     available).

7. **PACS 48 kHz in the built config**:
   ```bash
   grep -n 'FREQ_48KHZ\|FREQ_16KHZ\|FREQ_24KHZ' src/bt_bap.c
   ```
   Must show only `FREQ_48KHZ` in the `lc3_codec_cap`.

8. **No stale `audio_i2s_*` references**:
   ```bash
   git grep -n 'audio_i2s_init\|audio_i2s_push\|audio_i2s_stop\|audio_i2s_sdu_ref' -- src/ tests/
   ```
   Must return nothing (all renamed to `audio_sink_*`). The only `audio_i2s`
   references should be inside `audio_i2s.c` (internal static functions).

## Constraints and invariants

- **nRF5340 must keep building AND booting identically.** The boot log
  sequence must be unchanged: `BLE ready` → `settings_load() OK` →
  `VCP ready` → `I2S ready` → `Advertising`.
- **`settings_load()` ordering**: after `bt_enable()`, before
  `bt_pacs_register()`. This is a documented gotcha — do not break it.
- **`bt_audio_codec_cfg_get_chan_allocation` returns 0 on success** — the
  existing `if (cc == 0)` check in `lc3_config` must be preserved.
- **Stereo Mode B needs two LC3 decoders** — the two-decoder path in
  `audio_decode.c` must preserve the stride-2 decode for L and R.
- **I2S double-write gotcha** — `audio_i2s.c`'s slab logic is unchanged.
- **`audio_i2s_stop` must not clear `configured`** — unchanged (just renamed).
- **`CONFIG_LOG_PRINTK=y`** — unchanged.
- **No source testing** — the user has no BAP source. Use BlueZ + nRF5340DK
  hci_uart central for future testing. Do not write source-test instructions.

## Commit structure

1. **audio-sink interface**: rename `audio_i2s.h` → `audio_sink.h`, rename
   public functions to `audio_sink_*`, update `audio_i2s.c` to implement the
   new names, update all callers (`main.c`, `audio_shell.c` if it references
   them). Update bsim stub.
2. **app restructure**: create `bt_bap.c/h`, `audio_decode.c/h`, shrink
   `main.c`, update `CMakeLists.txt`, update bsim CMakeLists.
3. **PACS 48 kHz**: change `lc3_codec_cap` in `bt_bap.c` to 48 kHz only.
4. **unit tests**: add `tests/unit/decode/`, test cases for mono-to-stereo,
   interleave, config, decode.
5. **docs**: update AGENTS.md (key files, status, gotchas, PACS fix).

Commit messages: no AI attribution. Imperative style.

## Hard rules

- Implement ONLY the scoped Phase 2 work. Do not touch `audio_drift.c/h`,
  `audio_i2s.c` internal logic (only rename public API), `prj.conf`,
  `boards/`, `sysbuild.cmake`, `docs/design.md`,
  `docs/nrf54l15-drift-compensation.md`.
- Do NOT run `fw-flash-5340` or any hardware command. Build + unit tests are
  the exit criterion. Hardware flash + boot verification is the orchestrator's
  job after review.
- Do NOT push, merge, open PRs, amend, or force-push. Commit locally only.
- No AI/tool attribution in commit messages.
- If the build fails for a reason not covered here, STOP and report it — do
  not silently patch unrelated code.