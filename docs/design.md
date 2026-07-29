# LE Audio Receiver — Design Document

Status: **revised 2026-07-29** (Phase 5 closed — cpuapp ASRC accepted, Mode A+B 600s zero faults; Phase 6 FLPR offload Stages 0–5 complete). Earlier history: accepted 2026-07-05.

This is the consolidated design doc for the firmware supporting both **nRF5340**
and **nRF54L15**. It records current state, findings (historical), target
architecture, and phased plan.

Each phase below is intentionally concrete-but-not-exhaustive: detailed handoff
documents are written per phase when work on it starts.

---

# Part I — Current state & findings

## What works today

- **nRF5340 (Ebyte E83-2G4M03S module)**: BAP Unicast Server, sink-only,
  2 sink ASEs, LC3 decode (mono / stereo Mode A / stereo Mode B), VCP volume,
  CAS, shell diagnostics, watchdog. Audio out via I2S to UDA1334A DAC.
- **Dual-core flash** via OpenOCD + CMSIS-DAP (Pico probe), single-session
  `west flash` for both cores (`boards/ebyte/e83_nrf5340/support/flash_nrf5340.tcl`,
  documented in `docs/flashing.md`).
- **Drift compensation on nRF5340**: `src/audio_drift.c` trims the HFCLKAUDIO
  APLL from ISO timestamps. Hardware-independent module with a ztest unit
  suite (`tests/unit/drift`) and a bsim scaffold (`tests/bsim`).
- **nRF54L15 (Seeed Xiao)**: builds, flashes (OpenOCD + Xiao CMSIS-DAP),
  boots, advertises, pairs with BlueZ (nRF5340DK `hci_uart` central), exposes
  PACS (0x1844) + ASCS (0x1850) over GATT. I2S20 on D0/D1/D2 (P1.4/P1.5/P1.6)
  initializes (`I2S ready`, `I2S DMA started`). GPIO mapping proven: D0=BCK
  toggles, D1/LRCK and D2/SDOUT toggle when DAC digital wires removed.
  Standalone I2S20 test ran 20 seconds, fed 2,016 blocks, zero EIO/underrun.
  SAMPLE_ADJUST actuator wired. **Main receiver end-to-end audio: technical
  PASS (Phase 4c, 10-minute stability gate); physical audibility UNAVAILABLE**
  — see Phase 4 for completed sub-gates and evidence.
- Clean small modules: `audio_stats`, `audio_volume`, `audio_shell`.

## Findings (historical — all resolved in Phases 0–4)

### F1 — nRF54L15 build was broken (three causes) — RESOLVED Phase 1

Board files exist (`boards/nrf54l15dk_nrf54l15_cpuapp.{conf,overlay}`,
commit `cd87bf2`). All three causes fixed:

1. `CMakeLists.txt:7` no longer appends overlays unconditionally.
2. `src/audio_i2s.c` uses `DT_ALIAS(i2s_audio)`.
3. `sysbuild.cmake` gates `SB_CONFIG_NETCORE_HCI_IPC` on nRF5340 only.

### F2 — Dead code — RESOLVED Phase 0

- `src/net_core_bootloader.c` + `src/net_core_fw.h`: deleted.
- `src/stream_tx.c` + `src/stream_tx.h`: deleted.

### F3 — Machine-specific configuration committed to the repo — RESOLVED Phase 0

Probe serial removed from `CMakeLists.txt`; resolved at flash time via
`nrf-probes`. Hardcoded paths removed from `flake.nix`.

### F4 — Multi-rate audio bug — RESOLVED Phase 2

PACS capability now advertises **48 kHz only**. Multi-rate support is backlog.

### F5 — Three coexisting build workflows — RESOLVED Phase 0

Single workflow: `nix-nrf-dev` flake → `mkNrfShell`. All helpers in
`scripts/bin/`. `activate.sh`, `build.sh`, `west.yml` deleted.

### F6 — Clock recovery was an FLL, not a PLL — RESOLVED Phases 3–4

`audio_drift.c` now implements a dual-term ppm-based PI controller:
frequency term from PCLK-vs-GRTC measurement (nRF54L15) or ISO timestamps
(nRF5340), plus phase term from I2S buffer fill. Output in ppm, routed to
platform-specific actuators (APLL or SAMPLE_ADJUST). The packet-repeat
fallback no longer fires in steady state. See Part II §Clock recovery and
`AGENTS.md` "Drift controller" for current production architecture.

### F7 — Misc — RESOLVED

- `main.c` restructured (Phase 2): BT setup, ASCS callbacks, LC3 decode,
  channel routing, watchdog, and advertising loop split into separate modules.
- Code style unified to Zephyr convention.
- GitHub Actions CI remains disabled (backlog).

## Tooling reference: nix-nrf-dev

`flake.nix` consumes [`nix-nrf-dev`](https://github.com/qarnet/nix-nrf-dev)
via `mkNrfShell { ncsVersion = "v3.3.0"; }`. The flake provides the NCS
toolchain (loaded scoped inside `west` only — the shell itself stays clean
so `nix`, agents, editors, and the python used by test scripts all work),
`openocd` (master build, the only flash backend), `nrf-probes`, and
multilib GCC for `native_sim`. Build/flash helpers (`fw-build-5340`,
`fw-build-54l15`, `fw-flash-5340`, `fw-flash-54l15`) live in
`scripts/bin/` and are on PATH from the dev shell.

**Extension policy:** add non-toolchain runtime deps (python modules for
test scripts, etc.) via `mkNrfShell { packages = [ ... ]; }` — the
`packages` argument is a standard `mkDerivation` extension. Do NOT eval the
Nordic sdk-manager env into the shell globally; it exports `PYTHONHOME`,
`PYTHONPATH`, `LD_LIBRARY_PATH` that break non-toolchain tools.

---

# Part II — Target architecture

## Layered audio pipeline

```
BLE / BAP front-end        (ASCS callbacks, PACS, pairing, adv loop)
        │  SDUs + ISO timestamps
Decode & channel routing   (LC3, mono / Mode A / Mode B → interleaved PCM)
        │
Volume                     (VCP-driven, existing module)
        │
Audio sink interface       (platform-neutral: init/push/stop + clock feedback)
        │
Platform I2S backend       (device via DT_ALIAS(i2s_audio); nRF5340 i2s0,
                            nRF54L15 i2s20)
```

`main.c` shrinks to wiring + lifecycle. Decode/routing moves out of the ASCS
callback file so it can be unit-tested.

## Clock recovery: one controller, escalating actuators

The core insight: the "software PLL" separates into a platform-independent
**controller** and a platform-specific **actuator**.

**Controller** (pure math, ztest-covered):

- **Frequency term**: PCLK-vs-GRTC measurement from platform timing hardware
  (nRF54L15: `audio_drift_frequency_error_update()` fed every 1 s from the
  GRTC work handler; nRF5340: not implemented, stays zero).  Positive = local
  PCLK/I2S faster than controller.  Filtered through an integer EMA (N=8,
  ~0.35 Hz corner) to reject one-second jitter.  Feedforward correction =
  `-filtered_measurement` (local fast → negative correction → insert eventually).
- **Phase term**: I2S buffer-fill deviation from a setpoint, computed once
  per rendered audio block in `audio_sink_push()`.  Phase error = `PHASE_SETPOINT
  - slab_free_count` (positive when slab is filling, negative when draining).
- Pure integer PI loop (no floating point).  Gains in milli-units.
  Directional anti-windup on phase integrator: at a saturation rail,
  same-direction increments are blocked but opposite-direction increments
  are allowed to unwind toward range.  Controllers are thread-safe
  (k_spinlock serialises workqueue / audio-path / reset).  Combined
  output = filtered feedforward + phase PI.
- Output in **ppm** (not APLL register units).

**Actuators** behind one interface, selected per platform via Kconfig choice
(working names):

| Kconfig | Platform | Mechanism |
|---|---|---|
| `AUDIO_CLOCK_ACTUATOR_APLL` | nRF5340 | ppm → HFCLKAUDIO register trim (true clock steering) |
| `AUDIO_CLOCK_ACTUATOR_SAMPLE_ADJUST` | nRF54L15 (Phase 4) | single-sample insert/drop when accumulated phase > 1 sample (~20.8 µs @ 48 kHz) |
| `AUDIO_CLOCK_ACTUATOR_ASRC` | nRF54L15 (Phase 5/6) | fixed-point fractional resampler; ratio = 1 + ppm·1e-6 |
| *(future)* CS2200 | custom PCB | ppm → I²C register write to fractional-N clock chip |

Insert/drop **is** nearest-neighbor ASRC — the degenerate case. The evolution
path is continuous: same controller, progressively better interpolation
(drop/insert → linear → polyphase), and FLPR offload is purely a deployment
decision if cpuapp runs out of budget. Nothing gets thrown away between
phases.

The packet-repeat fallback in `audio_i2s.c` remains as an emergency path
only; a converged loop must not trigger it in steady state.

### Drift measurement (production)

- **nRF54L15**: TIMER20 in TIMER mode (PCLK-derived free-running ticks).
  GRTC compare at 1-second intervals triggers TIMER20 `TASKS_CAPTURE` via
  GPPI, hardware-snapshotted. The GRTC ISR reads the captured count, computes
  unsigned delta and elapsed GRTC microseconds, derives integer ppm.
  `audio_drift_frequency_error_update()` feeds this into the PI controller.
- **nRF5340**: ISO `info->ts` deltas (no GRTC/TIMER20 on this platform).
  Feedforward term stays zero; phase-only PI using I2S buffer fill.

**Historical (invalidated):** I2S20 FRAMESTART was considered as a sample-clock
counter but FRAMESTART fires at DMA buffer boundaries (~100 Hz), not LRCK edges.
Direct RADIO RX capture is forbidden (MPSL/SDC owns RADIO). Both paths are
closed.

### nRF54L15-specific constraints

- No HFCLKAUDIO APLL (`NRF_CLOCK_HAS_HFCLKAUDIO == 0`); HFXO `TASKS_XOTUNE`
  is one-shot calibration, not runtime trim; HFPLL fixed at boot. The clock
  driving I2S **cannot** be steered → resampling-family actuators only
  (short of a PCB change).
- FLPR: RISC-V VPR, **no FPU** (fixed-point ASRC mandatory),
  cannot access I2S20 (cpuapp domain) but can access GRTC; IPC via shared
  SRAM + VEVIF/icmsg; NCS v3.3.0 FLPR/HPF support is still young →
  prototype ASRC on cpuapp first.
- CPU budget: on nRF54L15 the SoftDevice Controller, BT host, **and** LC3
  decode share one 128 MHz core (vs. dual-core nRF5340) — this strengthens
  the FLPR-offload case (Phase 6) more than raw quality arguments do.

## Board abstraction

Custom board definition for the E83-2G4M03S hardware (nRF5340), replacing
overlay patching of the stock DK board. Migration table already exists in
`docs/flashing.md` §"What a custom board file would absorb":

- Board `.dts` absorbs: UART0 pinctrl remap, I2S pins + 12.288 MHz audio
  clock, QSPI disable, `i2s-audio` alias.
- `board.cmake` absorbs: `board_set_flasher(openocd)` + runner args +
  `include(openocd.board.cmake)` — removing the `BOARD_FLASH_RUNNER` CACHE
  hack and the `app_set_runner_args()` guard from `CMakeLists.txt`.
- Stays project-level: `NET_CORE_HEX` path (sysbuild layout), the custom TCL
  procs, `BUILD_ONLY` on hci_ipc.
- Probe serial moves to an **untracked local file / env var** (per-machine).

The nRF54L15 DK is used as-is (stock board + small overlay) until custom
hardware exists for it.

## Assumptions (decided 2026-07-05, reaffirmed 2026-07-25)

1. **48 kHz only for now** — PACS capability shrinks to 48 kHz (fixes F4);
   multi-rate returns as a backlog item.
2. **nRF5340 is the reference target** — every phase keeps it working; the
   nRF54L15 catches up phase by phase.
3. **Freestanding app** against `~/ncs/v3.3.0` with toolchain env loaded via
   `nix-nrf-dev`'s `mkNrfShell` (scoped inside `west`); the unused `west.yml`
   is removed (or fixed if a workspace is ever wanted — not now).
4. **Testing is local-first** (ztest + bsim); CI revival is backlog.
5. **No ignored warnings** — every build/boot/flashing warning is fixed at
   the source or explicitly suppressed with a recorded reason. See
   `AGENTS.md` "Policy — never ignore warnings".

---

# Part III — Phases

Phases 0–4 are sequential (completed). Phase 5 and Phase 6 are sequential
with each other; BabbleSim runs in parallel. Each phase ends with the
nRF5340 target building, flashing, and streaming, and (where applicable)
the nRF54L15 target building.

## Phase 0 — Tooling & hygiene

- Port the serial-mcp flake approach (table in Part I): dynamic
  `nrfutil sdk-manager toolchain env` shellHook, `nrfutil-core` derivation,
  `ZEPHYR_BASE` fallback derivation, `flake-utils`, helper scripts
  (`fw-build-5340`, `fw-build-54l15`, `fw-flash`, …), compile-DB export
  wired to `.clangd`. Keep `openocd-master`.
- Collapse the three build workflows into one; delete `activate.sh`,
  `build.sh`, the `env_ncs.sh` mechanism; update `AGENTS.md` accordingly.
- Delete dead code: `net_core_bootloader.c`, `net_core_fw.h`,
  `stream_tx.c`, `stream_tx.h` (and their CMake lines).
- Remove `west.yml`.
- Move probe serial out of the repo (local file / env var).
- Unify code style (Zephyr style; reformat `main.c`).

**Exit criterion**: fresh clone + `direnv allow` → build + flash + stream on
nRF5340 with no machine-specific edits beyond the local probe config.

## Phase 1 — Board foundation

- Custom board definition for the E83 module per the `docs/flashing.md`
  migration table; delete the nRF5340 overlay hacks it absorbs.
- Per-board conf/overlay via standard Zephyr auto-discovery — no
  unconditional `DTC_OVERLAY_FILE` appends (fixes F1.1).
- Board-conditional sysbuild config: `SB_CONFIG_NETCORE_HCI_IPC` only where
  a netcore exists (fixes F1.3).
- Switch `audio_i2s.c` to `DT_ALIAS(i2s_audio)` (fixes F1.2).

**Exit criterion**: **both** targets compile (`nrf54l15dk/nrf54l15/cpuapp`
audio not yet expected to work end-to-end); nRF5340 unchanged in behavior.

## Phase 2 — App restructure

- Split `main.c`: BT setup/pairing/advertising, ASCS callback glue, LC3
  decode + channel routing (unit-testable, no Zephyr BT deps in the routing
  math), pipeline wiring.
- Introduce the audio-sink interface between decode/volume and the I2S
  backend.
- Restrict PACS capability to 48 kHz (fixes F4 for now).

**Exit criterion**: identical external behavior on nRF5340; decode/routing
covered by new unit tests.

## Phase 3 — Clock recovery v2 (nRF5340)

- Refactor `audio_drift` into the ppm-based PI controller: frequency term
  from ISO timestamps + **phase term from I2S buffer fill** (fixes F6).
- APLL becomes the first actuator behind the actuator interface.
- Extend `tests/unit/drift` to the new controller (convergence, lock
  behavior, wrap handling, actuator saturation).

**Exit criterion**: packet-repeat fallback does **not** fire in steady-state
streaming on nRF5340 (observable via `audio status` shell counters).

## Phase 4 — nRF54L15 audio bring-up — COMPLETE (2026-07-26)

Phase 4 is closed on measurable exit criteria. Code landed (I2S20
pinctrl fix, SAMPLE_ADJUST actuator, SDC-on-cpuapp buffer counts), boot +
PACS/ASCS + BlueZ bonding verified. BLE CIS transport verified through
nRF5340DK `hci_uart` central. GPIO mapping D0/D1/D2 (P1.4/P1.5/P1.6) proven.
Standalone I2S20 DMA test completed (20 s, 2,016 blocks, no EIO). The old
DAC breakout held D1/LRCK high when unmuted — incompatible or defective
assembly; replaced with known-good DAC. All sub-gates completed: rate
conversion (4a.2), GRTC-driven PCLK feedforward + phase PI (4b.1/4b.2),
and 10-minute stability gate (4c) — all PASS. External digital I2S gate
PASS at DAC pins (BCK/LRCK ratio 31.999701). Physical audibility UNAVAILABLE
by user — not failed, not blocking further measurable work.
The sub-steps and their evidence are summarized below:

### Phase 4a — COMPLETED

All sub-gates completed. Historical detail below; see Phase 4 opening
for current closed status.

- **4a.0 completed hardware characterization:**
  - Correct Xiao pin map: D0/P1.4 = BCK, D1/P1.5 = LRCK, D2/P1.6 = SDOUT.
  - Old DAC isolation result: with DAC digital wires connected and MUTE low,
    D1/LRCK held high; with digital wires removed, D1 toggles. The old
    breakout/wiring assembly is incompatible or defective.
  - Standalone I2S20 evidence: 20.001 seconds, 2,016 blocks fed, zero
    EIO/underrun, ENABLE=1, TASKS_START triggered, PSEL correct,
    FRAMESTART firing. I2S20 hardware works.
  - PCLK32M clock source works; `PCLK32M_HFXO` usage-fault is tracked
    separately.
- **4a.1 main-pipeline/new-DAC retest — COMPLETED (2026-07-26):**
  - Technical stability gate PASS: 60,000 frames / 600.00 s (10 minutes),
    zero disconnect, zero slab-full/underrun/warning/error/fault, clean
    teardown. See `docs/development/phase4c-technical-results.md`.
  - External digital I2S gate PASS at DAC pins: fx2lafw logic analyzer
    captured active 30 s Mode A stream, BCK 1,525,637 Hz, LRCK 47,677 Hz,
    BCK/LRCK ratio 31.999701 (expected 32), SDOUT nonconstant activity.
    See `docs/development/phase4c-i2s-analyzer-results.md`.
  - Physical audibility: UNAVAILABLE by user — not failed, not blocking
    further measurable work. Analog audio quality is not claimed.
  - Original acceptance criteria that required an audible report have
    been superseded by the measurable technical gate above; the phase is
    closed on measurable criteria and audibility is non-blocking.
- **4a.2 rate conversion — COMPLETED (2026-07-26):**
  - Root cause: PCLK32M clock source produces ~47,619 Hz LRCK, while decoder
    output and I2S writes were fixed at 48,000 Hz / 480 frames per block.
    Queue filled at ~100 blocks/s, drain at ~99.2 blocks/s → slab-full every
    ~1.57 s.
  - Fix: bounded nearest-neighbor rate converter (`src/audio_rate_convert.{c,h}`)
    maps each 480-input-frame block to 476/477 output frames, averaging 47,619
    output frames per 100 input blocks. Remainder accumulator ensures exact
    total. nRF5340 stays at 48k→48k identity (default).
  - Verified: 10/10 unit tests (native_sim), both builds pass, 35-second
    Mode A stream with zero slab-full drops, zero DMA underruns.
    See `docs/development/phase4a2-rate-conversion-results.md`.
  - Residual: nearest-neighbor conversion removes about 381 frames/s at
    nominal mismatch. Artifact audibility and character are unmeasured.
    Peer-drift correction addressed by Phase 4b (GRTC feedforward + phase
    PI, now complete). Phase 5 quality ASRC is planned implementation
    work — see Phase 5 section.

### Phase 4b — Supported ISO timestamp presentation scheduling — **4b.1 PASS, 4b.2 HARDWARE PASS (2026-07-26)**

**Mandatory, not deferred.** With the fixed PCLK32M rate mismatch resolved
by 4a.2, residual peer-drift between BLE controller clock and I2S clock still
needs correction. The original plan to capture direct RADIO RX events via DPPI
is unsupported — MPSL/SDC owns RADIO and forbids any direct RADIO register,
event, IRQ, or DPPI access. Phase 4b is rewritten to use the Nordic
ISO-time-sync pattern, documented at:

`nrf/samples/bluetooth/iso_time_sync/`

1. **ISO SDU reference time**: Validate `BT_ISO_FLAGS_TS` before consuming
   `info->ts`. On nRF54 Series the ISO timestamp is controller-clock time;
   treat it as the ISO SDU reference time — do not use callback arrival as
   an RX timestamp.

2. **Future GRTC presentation trigger**: Use the Nordic ISO-time-sync pattern:
   schedule a GRTC compare/action at `info->ts + presentation_delay`.
   GRTC + DPPI executes the final reference action independent of callback
   wake latency. This is the supported, documented path (see Nordic ISO
   time-sync sample and nRF Audio synchronization module as conceptual
   references — the dual-core architecture of those samples is not directly
   portable to this single-core application).

3. **~~I2S LRCK frame counter~~** — **INVALIDATED (2026-07-26)**. Hardware
   validation showed I2S20 `FRAMESTART` fires at DMA audio-buffer boundaries
   (~100 Hz in this configuration), not every physical LRCK edge (~47,619 Hz).
   Counting FRAMESTART cannot measure sample-clock frequency. The production
   path is a PCLK-derived free-running TIMER (see step 4).

4. **Drift estimate**: TIMER20 runs in TIMER mode (32-bit, prescaler 0,
   PCLK-derived free-running ticks). GRTC compare at 1-second intervals
   triggers TIMER20 `TASKS_CAPTURE[0]` via GPPI, hardware-snapshotting
   the timer count. Nominal timer frequency is determined by the HAL
   macro `NRF_TIMER_BASE_FREQUENCY_GET(timer_reg)` (16 MHz on TIMER20).
   The GRTC ISR reads the captured count, computes unsigned delta and
   elapsed GRTC microseconds, and derives integer ppm relative to the
   nominal tick count for the elapsed interval. Since I2S derives from
   `PCLK32M`, this ppm is the local PCLK frequency error relative to
   controller/GRTC time — input for Phase 4b.2. Phase 4b.1 logs
   diagnostics only — the output is not yet fed into the PI controller.

5. **No direct RADIO access**: Direct RADIO RX `ADDRESS`/`END` captures are
   forbidden with SDC/MPSL. There is no fallback direct-RADIO implementation
   — the ISO-timestamp path (step 1) is the only supported input.

6. **SDC Event Start Task**: `sdc_hci_cmd_vs_set_event_start_task()` is an
   optional ACL timing-event diagnostic only. It is not a CIS RX/SDU timestamp
   and is not an input to the PI controller.

7. **Phase 4b.2 hardware PASS recorded** in `docs/development/phase4b2-results.md`:
   4,500 frames / 45 s at 100 fps, PCLK diagnostics +1,500..+1,757 ppm,
   closed-loop correction (insert-to-drop 186:1), channel-pair gate correct
   (16 drops before first PCLK measurement, inserts only thereafter), clean
   teardown, no slab-full/I2S underrun/warning/fault.

### Phase 4c — Stability + artifact verification — **TECHNICAL PASS (2026-07-26)**

With GRTC driving the controller and SAMPLE_ADJUST consuming its output,
the technical stability gates have been verified. See
`docs/development/phase4c-technical-results.md`.

- **10-minute uninterrupted stream**: PASS — `Done: 60000 frames in 600.00 s
  (100.0 fps)`. No disconnect, no slab exhaustion, no underrun storms.
- PCLK diagnostics active for full run: roughly +1,523 to +2,058 ppm.
- Sample correction overwhelmingly insert direction: startup settled at 13
  drops, then inserts rose monotonically; last logged `ins=51487 drops=13
  (total=51500)`, average ~86 inserts/s. This is expected for SAMPLE_ADJUST
  at this PCLK/HFINT offset — it does NOT indicate controller non-convergence.
- Sample adjustments are **not** rare at this clock offset; HFINT/PCLK
  mismatch requires frequent inserts (~86/s), which is the expected behavior
  for the SAMPLE_ADJUST actuator.
- **External digital I2S gate**: PASS — fx2lafw logic analyzer capture at
  DAC pins during active 30 s Mode A stream confirms valid I2S waveforms:
  BCK 1,525,637 Hz, LRCK 47,677 Hz, BCK/LRCK ratio 31.999701 (expected 32),
  SDOUT nonconstant activity (324,633 transitions, high duty 0.494).
  See `docs/development/phase4c-i2s-analyzer-results.md`.
- **Audible quality**: UNAVAILABLE — user did not provide listening report.
  This is not a failure and does not block further measurable work. Analog
  output quality is not claimed.

### Phase 4 risks (tracked, not deferred)

- **R-4.1 CPU budget on single core**: SDC radio ISR + BT host ISO RX +
  LC3 decode (×2 for Mode B) + I2S DMA refill + sample_adjust memmove, all
  on the 128 MHz cpuapp. nRF5340 splits this across two cores. If the
  budget blows, ISO RX packet loss (audio gaps) or I2S underruns result.
  Mitigation: measure recv_cnt vs. expected SDU rate during 4a; if drops
  scale with LC3 complexity, advance Phase 6 (FLPR offload) from the already-planned
  schedule. Phase 5.0 instrumentation gives CPU budget numbers.
- **R-4.2 Central ISO/CIS quirks**: the nRF5340DK `hci_uart` central
  is the proven transport (ISO verified at 3000 packets/15 s). Any
  other central (USB dongle, built-in adapter) must be independently
  verified for ISO/CIS support before debugging receiver issues.
- **R-4.3 nRF5340DK fallback as truth source**: if nRF54L15 streaming
  fails on both centrals, flash the nRF5340DK receiver via the J-Link
  (udev-fixed, OpenOCD working) and stream to it — it is the known-good
  target. If it also fails, the bug is in the central/test setup, not the
  nRF54L15 firmware.

**Exit criterion (whole phase)**: 4a + 4b + 4c all green. Stable
indefinitely-running audio stream on the nRF54L15 (verified: 10 minutes,
zero faults); external digital I2S gate PASS at DAC pins (verified:
fx2lafw, 30 s stream, BCK/LRCK ratio 31.999701); GRTC drift measurement
active. Physical audibility marked UNAVAILABLE by user — not failed, not
blocking further measurable work.

## Phase 5 — ASRC quality upgrade **(intended implementation work)**

Phase 4 proves stability — 10-minute stream, zero faults, controller
converged. But at measured PCLK offsets (+1,523..+2,058 ppm),
SAMPLE_ADJUST produced ~86 inserts/s (51500 insertions/13 drops over
10 min). Each sample insert/drop is a temporal discontinuity in the PCM
stream. Even without listening evidence, frequent discontinuous insertion
at this rate is sufficient engineering risk to require a continuous ASRC
(86 inserts/s → average interval ~12 ms between discontinuities; audibility
is not claimed from this number).

Phase 5 adds a stateful cross-block fixed-point linear-interpolation ASRC
on cpuapp. The same PI controller feeds the resampler ratio. The ASRC is a
data-path resampler, not a hardware actuator: it consumes the controller
correction (ppm) through an explicit API separate from the actuator
interface. APLL (nRF5340) remains the hardware clock-steering actuator
unchanged. SAMPLE_ADJUST remains selectable during Phase 5 for A/B
comparison and rollback; it is removed only after ASRC acceptance. The
architectural contract is: controller → correction → [APLL hardware trim OR
data-path resampler (SAMPLE_ADJUST today, ASRC after acceptance)]. Whether
the ASRC adapter is implemented as an actuator-choice entry or wired
directly into the data path is a phase-design decision, not frozen here.

### 5.0 — Instrumentation baseline

Before adding ASRC code, instrument the existing data path to capture the
SAMPLE_ADJUST baseline: callback-deadline headroom for Mode A and Mode B,
whole data-path CPU budget (SDC + BT host + LC3 decode + I2S DMA +
SAMPLE_ADJUST memmove), slab-range statistics (min/max/mean free count),
repeat/underrun/push-failure counters over 10-minute runs. This measures the
current system with SAMPLE_ADJUST — ASRC-specific cycle counts are measured
after implementation and compared against this baseline. The baseline also
feeds the R-4.1 CPU-budget question with measured numbers, not estimates.

### 5.1 — Resampler semantics (controller-preserving)

The source-step semantics preserve the existing controller output sign
convention:

- `source_step = input_rate / physical_output_rate * (1 + correction_ppm / 1e6)`
- positive ppm → source_step > 1.0 → consume source faster → equivalent
  to sample drop over time.
- negative ppm → source_step < 1.0 → consume source slower → equivalent
  to sample insert over time.

Controller output goes directly into the ratio calculation; no sign flip
or ambiguous "speed up / slow down" wording. The ratio is exposed as a
Q32.32 fixed-point `source_step` (or equivalently justified fixed-point
format chosen during phase design).

### 5.2 — ASRC implementation

- Stateful cross-block stereo s16 linear interpolation.
- Continuous phase accumulator and sample history (one previous sample per
  channel) carried across block boundaries — no per-block reset.
- No heap allocation; stack/static only. Bounded output capacity: worst-case
  output frame count is determined by the configured minimum `source_step`
  (most negative ppm correction → smallest step → most output frames), plus
  interpolation history margin. Compile-time capacity proof against
  `MAX_OUTPUT_FRAMES` (currently 481 stereo frames in `src/audio_i2s.c`,
  sized for 48→47,619 Hz drain plus one SAMPLE_ADJUST insert headroom) and
  runtime assertion are required for the configured ppm bounds. Frames and
  interleaved samples are not conflated.
- Reset state on stream stop/disconnect. Silence prefill (I2S preamble
  blocks) must not consume source phase — the resampler only advances
  phase against real decoded audio. Physical-rate frame scheduling and
  silence prefill may use a separate remainder helper without involving
  the ASRC source phase.
- ASRC replaces the current fixed-rate converter (`src/audio_rate_convert.c`)
  on the nRF54L15 path. It honors the actual decoded frame count
  (`sample_count`) passed into `audio_sink_push()` — not an intermediate
  rate-converter output. The nRF5340 path stays unchanged (identity).
- Account for current slab allocation: `MAX_OUTPUT_FRAMES=481` stereo frames
  per block (defined in `audio_i2s.c`); `CONFIG_I2S_NRFX_TX_BLOCK_COUNT=12`
  determines the number of slab blocks in the pool. Both are relevant to
  capacity planning.

### 5.3 — Testing

- **Unit tests** (ztest, native_sim): long-run frame totals, sign chain
  (positive ppm → fewer output samples over time), block-boundary
  phase continuity, chunking invariance (same output regardless of input
  block sizes), stereo isolation (L and R independent), constant/ramp/
  full-scale input patterns, output capacity/canary checks, abrupt ppm
  changes, deterministic 60,000-block run, host-reference digital-quality
  comparison against Python float64 reference.
- **Hardware tests**: Mode A + Mode B 10-minute autonomous central streams
  on nRF54L15; zero faults/underruns/repeats/capacity failures; measured
  callback deadline margin with ASRC active; external I2S activity and
  BCK/LRCK ratio; objective digital PCM comparison loopback if a
  trustworthy digital capture path can be set up.
- **Regression**: nRF5340 builds and streams unchanged (APLL actuator,
  identity resampler or bypass).

### 5.4 — Acceptance

Both targets build. All unit tests pass. nRF5340 regression zero. nRF54L15
Mode A + Mode B 10-minute streams with zero faults. Measured callback
deadline margin. Audibility is optional observation only — never a gate.
SAMPLE_ADJUST removed from Kconfig choice after acceptance.

### 5.5 — Likely files

Based on current repo truth, likely new/modified files (exact API is a
phase-design output, not frozen here):

- `src/audio_asrc.{c,h}` — resampler module (state, phase accumulator,
  stereo s16 linear interp, takes ppm correction through explicit API;
  wiring into the data path is a phase-design decision — adapter entry
  in the actuator choice or direct consumer in the audio pipeline).
- `Kconfig` — new config for source-step fixed-point format, ppm bounds
  for compile-time capacity proof.
- `src/audio_i2s.c` — ASRC wired into the push path (replaces rate
  converter on nRF54L15, identity on nRF5340); the existing
  `consume_sample_adjustment()` path is already actuator-agnostic.
- `tests/unit/asrc/` — new test suite.

## Phase 6 — FLPR offload **(intended implementation work)**

Phase 6 moves the accepted fixed-point ASRC from cpuapp to the nRF54L15
FLPR (RISC-V VPR). Goal is implementation, not merely gated on
CPU pressure. Measurements from Phase 5 instrumentation inform the
offload decision but do not gate it — the offload is intended regardless.

Uses the generic Zephyr FLPR image first with SRAM execution
(`nrf54l15dk/nrf54l15/cpuflpr`). HPF (High-Performance Framework)
stays optional optimization because experimental in NCS v3.3.0 and no
official ASRC framework exists for it. Stages build incrementally.

### Stage 0 — Boot, handshake, memory map

- Build a separate Zephyr/sysbuild FLPR image alongside cpuapp. Use the
  Nordic VPR launcher pattern: cpuapp builds with `cpuflpr_vpr` source
  memory (image stored in RRAM) and execution memory (reserved SRAM
  region for FLPR runtime). The FLPR image is a sysbuild snippet
  (`nordic-flpr`, per the `nrf/samples/ipc/ipc_service/` pattern).
- Establish cpuapp↔FLPR boot handshake: cpuapp releases FLPR from reset
  (VPR launcher), FLPR signals ready via VEVIF notification. VEVIF is
  the IPC notification mechanism, not the boot-launch path.
- Memory map: reserve shared SRAM region (`RAM_00` for DMA/ISR-critical
  data stays on cpuapp side; shared/FLPR placement measured).
- Extend current OpenOCD flash helper to program the FLPR image at the
  correct RRAM offset (`0x165000` in cpuapp address space — verified
  by write/read-back).
- Verify: both images load, FLPR boots and signals ready, cpuapp logs
  handshake.

### Stage 1 — Shared SPSC PCM rings + ICMsg/VEVIF control

- Allocate two single-producer-single-consumer ring buffers in shared SRAM:
  one cpuapp→FLPR (decoded PCM blocks + per-block metadata: correction_ppm,
  sample_count, stereo flag, sequence number), one FLPR→cpuapp (resampled
  PCM blocks + metadata).
- Control channel over ICMsg/VEVIF (no per-block copied payload on the
  control path — only commands, acks, error codes).
- SPSC ownership: cpuapp writes input ring, FLPR reads input ring; FLPR
  writes output ring, cpuapp reads output ring. No locks — barriers and
  cache handling per ARMv8-M / RISC-V coherence rules.
- MPSL-owned RADIO and reserved peripherals untouched.
- VEVIF channels based on official IPC sample (`nrf/samples/ipc/ipc_service/`).
- Verify: ordered message stress test, bit-exact ring wrap test, forced
  stall/recovery.

### Stage 2 — Identity loopback

- FLPR receives a PCM block, copies it unchanged to the output ring,
  signals done.
- cpuapp consumes output ring instead of direct I2S slab push.
- Prove end-to-end: Mode A 10-minute stream, zero faults, no timing
  regression vs pre-offload.
- nRF5340 builds unchanged (no FLPR on nRF53).

### Stage 3 — Move accepted fixed-point ASRC

- Port the Phase 5 cpuapp ASRC to FLPR as fixed-point only.
  **RV32E e/m/c constraints**: no FPU, no A (atomic) extension —
  use load/store with barriers for SPSC. Fixed-point arithmetic
  (Q32.32 multiply, accumulate) must be verified on RV32E target;
  generated code size and cycle count are measured from the compiled
  binary, not estimated from libgcc symbols. No heap — stack/static
  only.
- FLPR receives correction_ppm per block from cpuapp (cpuapp still owns
  the PI controller and ISO timestamp interpretation — only the
  heavy arithmetic moves).
- FLPR does NOT read GRTC for control initially; GRTC used for profiling
  and deadline measurement only in this stage.

### Stage 4 — Reset, fault, fallback

- FLPR watchdog or heartbeat timeout → cpuapp detects stall → resets FLPR
  → falls back to cpuapp ASRC path (the accepted Phase 5 ASRC retained on
  cpuapp during Phase 6 — not identity, which would reintroduce the PCLK
  rate mismatch). Re-arms handshake → resumes FLPR offload once FLPR is
  healthy.
- Fallback behaviour: bounded glitch (exact recovery latency TBD from
  measurements; not assumed to be within one block period).
- Verify: FLPR forced reset during streaming, fallback path engages, FLPR
  recovery and re-offload, nRF5340 unaffected.

### Stage 5 — Optimize and compare ✅ COMPLETE (2026-07-29)

**Decisions**:
- Keep ICMsg over VEVIF. Measured production max: FLPR ASRC 1.052 ms, RTT
  2.148 ms against 8 ms deadline (5.852 ms headroom). Raw VEVIF complexity
  not justified.
- Do not adopt experimental HPF. No SRAM/contention/deadline evidence.
- Keep four-slot 8 KiB rings and 8 ms deadline; proven fault margin.
- Remove dead Stage 2 identity submit API + 1920 B scratch buffer.
  Live production uses ASRC API only.
- Actuator set remains APLL (nRF5340) / NONE (nRF54L15).

**Results**: 276 unit tests pass, nRF54L15 CPUAPP FLASH 502904 B /
RAM 152244 B. Hardware: Mode A 120 s + true Mode B 120 s at 100 fps zero
faults, Mode A 180 s zero faults. See
`docs/development/phase6-stage5-optimize-close-handoff.md`.

### Gate criteria per stage

| Stage | Gate |
|---|---|
| 0 | FLPR image boots, handshake OK, memory map verified |
| 1 | Ordered message stress, bit-exact ring wrap, forced stalls |
| 2 | Identity 10-minute Mode A run, nRF5340 unaffected |
| 3 | Host-reference ASRC quality, Mode A/B 10-minute runs |
| 4 | Forced FLPR reset/recovery, fallback path, nRF5340 unaffected |
| 5 | Memory/cycle/deadline evidence, architecture and tuning choices documented |

### Constraints

- **RV32E e/m/c no FPU/no A extension**: fixed-point mandatory, no hardware
  atomics — barriers + ordered stores for SPSC.
- **RAM_00 for DMA/ISR-critical paths**: I2S DMA slabs stay on cpuapp in
  RAM_00. Shared SRAM for rings uses a measured, profiled region.
- **MPSL RADIO and reserved resources**: never touched by FLPR code.
- **VEVIF channels**: based on official `nrf/samples/ipc/ipc_service/` sample.
- **Separate cpuapp/cpuflpr image packaging**: sysbuild multi-image
  configuration; OpenOCD flash helper extended.
- **nRF5340 unchanged**: builds, flashes, streams with no FLPR path.

### Phase 6 goal

Implemented offload — not a conditional optimization. Measurements from
Phases 5/6 choose architecture and tuning; implementation only stops for
proven hardware or SDK impossibility that needs an explicit redesign
(e.g. an NCS v3.3.0 FLPR limitation that cannot be worked around).

## BabbleSim — cross-cutting verification track

BabbleSim is a planned research-then-implementation track that runs in
parallel with Phases 5–6. It is not a release blocker until the
environment is provisioned and the test scenario is valid. It complements,
never substitutes, native unit tests and real-hardware central-driven tests.

### Current state (NCS v3.3.0)

- `nrf5340bsim/nrf5340/cpuapp` and `nrf5340bsim/nrf5340/cpunet`: supported;
  BAP/CIS/ISO via SW Split.
- `nrf54l15bsim/nrf54l15/cpuapp`: supported.
- `nrf54l15bsim/nrf54l15/cpuflpr`: **explicitly unsupported** (CMake
  `FATAL_ERROR` in `boards/native/nrf_bsim/CMakeLists.txt`).
- I2S is removed/unmodeled in bsim (no sample clock, no DMA, no audio
  path). SDC/MPSL, real PCLK/GRTC drift fidelity, APLL register modelling,
  FLPR offload, CPU budget, DAC and audio quality cannot be validated.

### Research and setup task

- Provision BabbleSim separately; only with explicit user approval.
  Do not embed install commands in AGENTS.md as automatic actions.
- Environment variables: `BSIM_OUT_PATH`, `BSIM_COMPONENTS_PATH`.
- Current Twister/FORTIFY environment issue must be resolved before
  automated runs work (noted 2026-07-26; not a code bug — host toolchain
  interaction).
- Official group-filter/update/build requirements from the BabbleSim
  documentation.

### Current scaffold audit (`tests/bsim/`) — Stage 0/1 COMPLETE

- **Sysbuild** (`CMakeLists.txt`, `Kconfig.sysbuild`, `sysbuild.cmake`): dual-core
  nRF5340bsim receiver + SW Split cpunet.  Custom BAP client in `tests/bsim/client/`
  with 48_4_1 preset override and send-counter wrapper.  Both binaries build via
  `scripts/bsim-stage1-run.sh`.
- **Audio sink stub** (`audio_sink_stub.c`): startup-zero/PLC oracle with ordered
  FNV-1a hash.  Validates `plc == startup_plc`, `total == pushes + startup_zero`,
  zero errors, nonzero hash, deterministic energy across runs.  CONFIG_TEST decode
  bypass removed — BSIM path identical to production hardware.
- **Runner** (`scripts/bsim-stage1-run.sh`): compile + run with per-process log
  capture, exit-code validation, PASS-marker parsing with invariant checks.
- **Official smoke** (`scripts/bsim-official-smoke.sh`): compiles upstream
  BAP unicast audio test, exits nonzero on known teardown disable-race → Baseline
  PARTIAL.

### Stage 1 acceptance (2026-07-29)

- Advertising → pairing → PACS/ASCS → one sink ASE → CIS start → valid LC3
  fixture (48 kHz, 48_4_1 preset) → 104 client sends → 100 nonzero receiver pushes.
- `startup_zero=8`, `startup_plc=7`, `plc=7`, `total=108`, `hash=0xFE0D4245`,
  `energy=12480` — fully deterministic across two consecutive runs.
- Both real-target builds clean (nRF5340, nRF54L15).
- Results: `docs/development/bsim-stage1-results.md`.

### Planned beyond Stage 1

- Mode A / Mode B / reconnect / error injection.
- CI revival (build matrix for both boards + unit tests + bsim).
- nRF54L15 bsim target (cpuapp supported, FLPR unsupported by NCS).

BabbleSim cannot validate ASRC quality, I2S behaviour, SDC realism, FLPR
offload, or hardware stability — those remain hardware-only gates. It is
a fast feedback loop for BAP protocol, LC3 pipeline, and controller logic.

## Backlog (unscheduled)

- Multi-rate support (16/24 kHz): I2S reconfig from ASE codec config, then
  re-widen the PACS capability.
- Crossfade smoothing on the emergency fallback path (softens the residual
  artifact; complementary to everything above).
- CS2200 (or similar fractional-N clock chip) actuator — the designated
  production-hardware path if a custom PCB happens; I2S slave mode, ppm →
  I²C write, controller unchanged.
- CI revival (build matrix for both boards + unit tests + bsim).
- bsim test scenario expansion (see BabbleSim track above — smallest-useful
  scenario is planned; beyond that, Mode A/B, reconnect, error injection).
- GRTC-based drift measurement on nRF5340 (no GRTC there — equivalent is
  TIMER capture via DPPI; only if ISO-ts proves too noisy in practice).

---

# Appendix A — Options considered and rejected/deferred (nRF54L15)

Recorded here so they are not re-litigated.

| Option | Disposition | Reason |
|---|---|---|
| A. ASRC on cpuapp | **Adopted** (Phase 5 — intended implementation) | Linear interpolation at 48 kHz stereo; continuous-phase cross-block; measured CPU budget from Phase 5.0 instrumentation replaces estimates |
| B. ASRC on FLPR | **Adopted** (Phase 6 — intended implementation) | Zero cpuapp ASRC load; costs IPC + fixed-point port + ~10 ms latency; RV32E no-FPU required |
| C. FLPR bit-banged BCLK/LRCK (I2S slave) | **Rejected** | Any FLPR stall (cache miss, IPC, VEVIF) becomes clock jitter → audible; burns the FLPR entirely; needs physical jumper wires. Only unique benefit was bit-perfect output — for 16-bit LC3-decoded audio, resampling error sits below the codec noise floor, so the benefit is inaudible here. |
| D. PWM-generated I2S clock (slave) | **Rejected** | Same bit-perfect argument as C; limited frequency resolution (~PCLK/N steps); needs physical wires + DPPI choreography to keep LRCK = BCLK/64. |
| E. Single-sample insert/drop | **Adopted** (Phase 4) | Degenerate ASRC; ~10⁴× smaller artifact than the 10 ms packet repeat; no hardware change. |
| F. External fractional-N oscillator (CS2200 class) | **Deferred to backlog** | Not discarded — becomes just another actuator behind the same interface (ppm → I²C). Requires PCB; industry standard for network-audio clock recovery (<1 ppb resolution). |
| G. Crossfade smoothing | **Deferred to backlog** | Not an alternative; cheap mitigation for the emergency fallback path regardless of actuator choice. |

---

# Appendix B — Phase 4 evidence (2026-07-26)

Current evidence, not forward-looking plan. Separated by verification state.

### Established: BLE ISO delivery

- nRF5340DK `hci_uart` central verified: 3,000 ISO Data TX packets over 15 s,
  two CISes (Mode A stereo), 48 kHz LC3 at 100 fps, zero flow-control stalls.
- Receiver SDUs arrive: `stream_recv tally: valid=1006 invalid=144` (climbing).
- BlueZ connect, JustWorks pairing (bonded), BAP negotiation all work.
- Clean ACL teardown in `bap_central.py`; three consecutive runs with no DK
  reset, no zombie-slot exhaustion.

### Established: standalone I2S hardware/DMA

- I2S20 on D0/P1.4 (BCK), D1/P1.5 (LRCK), D2/P1.6 (SDOUT) — mapping confirmed.
- Standalone test ran 20.001 seconds, fed 2,016 blocks, zero EIO/underrun.
  Register state: ENABLE=1, TASKS_START triggered, PSEL correct, FRAMESTART
  firing. I2S20 hardware works.
- PCLK32M clock source works; `PCLK32M_HFXO` UsageFault is tracked separately
  (not an I2S issue). Frequency analysis completed in Phase 4c — see
  `docs/development/phase4c-i2s-analyzer-results.md`.

### Old DAC failure/isolation evidence

- With old DAC breakout connected and MUTE low: D1/LRCK held high, no toggling.
- With digital wires removed (BCK/LRCK/SDOUT disconnected): D1/LRCK toggles.
- Conclusion: old breakout/wiring assembly is incompatible or defective. Must
  not be treated as known-good.

### Completed: main-pipeline / new-DAC retest

- External logic analyzer (fx2lafw) measured I2S20 waveform at DAC pins
  during active 30 s Mode A stream. BCK 1,525,637 Hz, LRCK 47,677 Hz,
  BCK/LRCK ratio 31.999701 (expected 32), SDOUT nonconstant activity.
  Digital I2S gate PASS. See
  `docs/development/phase4c-i2s-analyzer-results.md`.
- Raw capture file at `/tmp/opencode/phase4c-i2s.sr` (not committed).

### Pending: audible quality

- Technical stability gate PASS (Phase 4c): 10-minute uninterrupted stream,
  zero faults, controller converged. See `docs/development/phase4c-technical-results.md`.
- External digital I2S gate PASS at DAC pins: fx2lafw analyzer confirmed
  valid I2S waveforms (BCK/LRCK ratio 31.999701). See
  `docs/development/phase4c-i2s-analyzer-results.md`.
- Physical audibility of ~86/s sample inserts at ~+1,800 ppm PCLK offset
  marked UNAVAILABLE by user — not failed, not blocking.
- Phase 5 (linear ASRC) is planned implementation work — engineering
  risk from ~86 discontinuous sample inserts/s at ~+1,800 ppm PCLK offset
  is sufficient rationale without physical listening evidence.
