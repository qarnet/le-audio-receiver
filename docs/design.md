# LE Audio Receiver — Design Document

Status: **revised 2026-07-26** (plan rewritten after Phase 4 code landed and
the nRF54L15 first-stream bring-up revealed gaps in the original Phase 4
definition). Earlier history: accepted 2026-07-05, superseded
`nrf54l15-drift-compensation.md` (absorbed in Part II §Clock recovery and
Appendix A).

This is the consolidated design doc for evolving the project from a single-target
nRF5340 experiment into a structured codebase supporting both **nRF5340** and
**nRF54L15**. It records the current state, the findings from the 2026-07 repo
review, the target architecture, and a phased plan.

It **supersedes** `nrf54l15-drift-compensation.md` — that document's analysis is
absorbed here (Part II §Clock recovery and Appendix A).

Each phase below is intentionally concrete-but-not-exhaustive: detailed handoff
documents are written per phase when work on it starts.

---

# Part I — Current state & findings

## What works today

- **nRF5340 (Ebyte E83-2G4M03S module)**: BAP Unicast Server, sink-only,
  2 sink ASEs, LC3 decode (mono / stereo Mode A / stereo Mode B), VCP volume,
  CAS, shell diagnostics, watchdog. Audio out via I2S to UDA1334A DAC.
- **Dual-core flash** via OpenOCD + CMSIS-DAP (Pico probe), single-session
  `west flash` for both cores (`scripts/flash_nrf5340.tcl`,
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
  SAMPLE_ADJUST actuator wired. **Main receiver end-to-end audio not yet
  accepted; new DAC connected, audible result pending** — see Phase 4.
- Clean small modules: `audio_stats`, `audio_volume`, `audio_shell`.

## Findings

### F1 — nRF54L15 build is broken (three causes)

Board files exist (`boards/nrf54l15dk_nrf54l15_cpuapp.{conf,overlay}`,
commit `cd87bf2`) but the target cannot build:

1. `CMakeLists.txt:7` appends `boards/nrf5340dk_nrf5340_cpuapp.overlay` to
   `DTC_OVERLAY_FILE` **unconditionally**. That overlay references `&uart0`,
   `&i2s0`, `&qspi` — none exist on nRF54L15 → devicetree compile error.
2. `src/audio_i2s.c:24` hardcodes `DT_NODELABEL(i2s0)`. Both board overlays
   define an `i2s-audio` alias, but the code never uses it (regressed in
   `1dc661b`). On nRF54L15 the node is `i2s20` → compile error.
3. `sysbuild.conf` sets `SB_CONFIG_NETCORE_HCI_IPC=y` unconditionally; the
   nRF54L15 is single-core (no netcore). Warning today, wrong shape either way.

### F2 — Dead code

- `src/net_core_bootloader.c` + `src/net_core_fw.h`: referenced by nothing in
  `CMakeLists.txt`. Leftover from a pre-OpenOCD flashing experiment.
- `src/stream_tx.c` + `src/stream_tx.h`: gated behind `CONFIG_BT_AUDIO_TX`,
  a Zephyr *sample-internal* Kconfig never set in this project; includes
  `stream_lc3.h`, which does not exist in the repo. Cannot compile even if
  enabled.

### F3 — Machine-specific configuration committed to the repo

- CMSIS-DAP probe serial `E6635C08CB1F502B` in `CMakeLists.txt`.
- Absolute toolchain path `/home/thomas-workstation/ncs/toolchains/911f4c5c26`
  and NCS path in `flake.nix`.

### F4 — Multi-rate audio bug

The PACS capability advertises **16/24/48 kHz**, but `audio_i2s.c` is
hardcoded to 48 kHz with fixed 480-sample blocks. A source configuring a
16/24 kHz stream gets LC3 frames with fewer samples, played at 48 kHz with
zero-padding per block → wrong pitch plus gaps. Either the I2S must be
reconfigured from the ASE codec config, or the capability must advertise
only 48 kHz until it is. (Plan: restrict to 48 kHz now — see Assumptions.)

### F5 — Three coexisting build workflows

1. `AGENTS.md`: build from `~/ncs/v3.3.0` via
   `nrfutil sdk-manager toolchain launch`.
2. `activate.sh` → generated `env_ncs.sh` → `build.sh`.
3. `flake.nix`: hand-rolled `west` Python wrapper against hardcoded
   toolchain paths.

Additionally `west.yml` (workspace manifest) exists but none of the three
workflows uses it.

### F6 — Clock recovery on nRF5340 is an FLL, not a PLL

`audio_drift.c` measures the central's ISO interval against the local clock
via `info->ts` deltas over 100 ms windows and feed-forwards a ppm trim into
the APLL register. Weaknesses:

- **No phase feedback**: nothing observes whether the I2S consumer is actually
  ahead or behind. Residual error (APLL step ≈ 3.3 ppm; 1 µs timestamp
  quantization = 10 ppm per 100 ms window) accumulates as buffer-fill drift
  until the packet-repeat fallback in `audio_i2s.c` fires. `AGENTS.md`
  documents periodic `Next buffers not supplied on time` as *expected* —
  i.e. the loop does not fully converge by design.
- Lock threshold 16 µs / 100 ms ≈ **160 ppm** — far looser than real crystal
  offsets (±20–50 ppm), so `LOCKED` carries little meaning.
- Output is in APLL register units, coupling the (otherwise platform-neutral)
  controller to nRF5340 hardware.

### F7 — Misc

- `main.c` is ~826 lines mixing BT setup, pairing, ASCS callbacks, LC3
  decode, channel routing, watchdog, and the advertising loop.
- Code style is split: `main.c` uses 2-space clang-format style; the other
  modules use Zephyr tab style.
- GitHub Actions CI was disabled (`9a21370`).

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

- Frequency term: drift estimate from ISO timestamps (later: hardware
  timestamping, below).
- Phase term: I2S buffer-fill deviation from a setpoint.
- PI loop → output in **ppm** (not APLL register units).

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

### Drift measurement

- **Today (both platforms)**: ISO `info->ts` deltas. 1 µs quantization →
  10 ppm per 100 ms window; usable with longer windows / averaging.
- **Target (nRF54L15)**: GRTC + DPPI hardware timestamping — RADIO RX event
  and I2S `FRAMESTART` both captured on GRTC channels (~7.8 ns resolution),
  delta-of-deltas over N seconds gives exact ppm at zero CPU cost during
  measurement. Far better SNR than either ISO timestamps or the old
  queue-depth heuristic.
- **nRF5340 equivalent**: no GRTC on nRF53; the same idea maps to
  TIMER capture via DPPI if the ISO-timestamp signal proves too noisy.

### nRF54L15-specific constraints (absorbed from the superseded doc)

- No HFCLKAUDIO APLL (`NRF_CLOCK_HAS_HFCLKAUDIO == 0`); HFXO `TASKS_XOTUNE`
  is one-shot calibration, not runtime trim; HFPLL fixed at boot. The clock
  driving I2S **cannot** be steered → resampling-family actuators only
  (short of a PCB change).
- FLPR: RISC-V VPR @ 128 MHz, **no FPU** (fixed-point ASRC mandatory),
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

Phases are sequential unless marked conditional. Each phase ends with the
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

## Phase 4 — nRF54L15 audio bring-up

This phase is **not done** as of the 2026-07-26 rewrite. Code landed (I2S20
pinctrl fix, SAMPLE_ADJUST actuator, SDC-on-cpuapp buffer counts), boot +
PACS/ASCS + BlueZ bonding verified. BLE CIS transport verified through
nRF5340DK `hci_uart` central. GPIO mapping D0/D1/D2 (P1.4/P1.5/P1.6) proven.
Standalone I2S20 DMA test completed (20 s, 2,016 blocks, no EIO). The old
DAC breakout held D1/LRCK high when unmuted — incompatible or defective
assembly. A new DAC is connected; main receiver end-to-end audio and
audible output are pending. The phase is re-scoped into ordered sub-steps:

### Phase 4a — Ordered verification gates

Each gate blocks the next. Do not skip ahead.

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
- **4a.1 current main-pipeline retest:**
  - First run the unchanged receiver firmware with the new DAC and a working
    central (nRF5340DK `hci_uart`).
  - Capture serial (`serial-mcp` on `/dev/ttyACM0`) + logic analyzer
    (sigrok-cli fx2lafw on D0/D1/D2 + 3V3) in parallel during the run.
  - Expected BCK: **approximately 1.536 MHz** (48 kHz × 16 bits × 2 channels
    = 1,536,000 Hz); BCK/LRCK ratio = 32. The old plan's 3.072 MHz figure
    was incorrect for 16-bit I2S.
  - External analyzer measurement and user audible report of the new DAC output
    are **required** — neither is a soft criterion.
  - If audio works end-to-end and is audible, record the evidence and
    proceed to 4b.
- **4a.2 rate conversion — COMPLETED (2026-07-26):**
  - Root cause: PCLK32M clock source produces ~47,619 Hz LRCK, while decoder
    output and I2S writes were fixed at 48,000 Hz / 480 frames per block.
    Queue filled at ~100 blocks/s, drain at ~99.2 blocks/s → slab-full every
    ~1.57 s.
  - Fix: bounded nearest-neighbor rate converter (`src/audio_rate_convert.{c,h}`)
    maps each 480-input-frame block to 476/477 output frames, averaging 47,619
    output frames per 100 input blocks. Remainder accumulator ensures exact
    total. nRF5340 stays at 48k→48k identity (default).
  - Verified: 10/10 unit tests (native_sim), both builds clean, 35-second
    Mode A stream with zero slab-full drops, zero DMA underruns.
    See `docs/development/phase4a2-rate-conversion-results.md`.
  - Residual: nearest-neighbor conversion removes about 381 frames/s at
    nominal mismatch. Artifact audibility and character are unmeasured.
    Peer-drift still needs Phase 4b GRTC. Phase 5 quality ASRC stays
    conditional on listening result.

### Phase 4b — Supported ISO timestamp presentation scheduling

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

3. **I2S LRCK frame counter**: Route I2S20 `FRAMESTART` through GPPI to
   TIMER20 `TASKS_COUNT` (32-bit COUNTER mode). This gives every LRCK edge
   a hardware frame index. One captured FRAMESTART edge has no frame index
   and cannot measure frequency — a counter is required.

4. **Drift estimate**: GRTC compare at 1-second intervals triggers TIMER20
   `TASKS_CAPTURE[0]` via GPPI, hardware-snapshooting the frame count.
   The GRTC ISR reads the captured count, computes unsigned delta
   and elapsed GRTC microseconds, and derives integer ppm relative to
   `CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ`. Phase 4b.1 logs diagnostics
   only — the output is not yet fed into the PI controller.

5. **No direct RADIO access**: Direct RADIO RX `ADDRESS`/`END` captures are
   forbidden with SDC/MPSL. There is no fallback direct-RADIO implementation
   — the ISO-timestamp path (step 1) is the only supported input.

6. **SDC Event Start Task**: `sdc_hci_cmd_vs_set_event_start_task()` is an
   optional ACL timing-event diagnostic only. It is not a CIS RX/SDU timestamp
   and is not an input to the PI controller.

7. **Phase 4b remains mandatory before 4c**. The wording that treated ISO
   timestamps as a fallback, or that claimed both ISO and hardware paths
   must not coexist, is withdrawn — the ISO timestamp is a required input to
   the supported hardware schedule (step 2).

### Phase 4c — Stability + artifact verification

With GRTC driving the controller and SAMPLE_ADJUST consuming its output,
verify the Phase 4 exit criteria:

- Stable, indefinitely-running stream (run for ≥ 10 minutes, no disconnect,
  no slab exhaustion, no underrun storms).
- Glitch magnitude ≤ 1 sample (~21 µs) per correction event — measure via
  the logic analyzer (a single-sample insert/drop is a 1-sample-period step
  in BCK/LRCK timing or a discontinuity in DIN).
- sample_adjust events are **rare** in steady state (log a counter; if it
  fires every block, the controller is not converging — diagnose).
- Listening test: confirm the single-sample insert/drop artifact is
  inaudible at the 48 kHz/16-bit LC3 floor. If audible → Phase 5 (linear
  ASRC) is needed; record the evidence.

### Phase 4 risks (tracked, not deferred)

- **R-4.1 CPU budget on single core**: SDC radio ISR + BT host ISO RX +
  LC3 decode (×2 for Mode B) + I2S DMA refill + sample_adjust memmove, all
  on the 128 MHz cpuapp. nRF5340 splits this across two cores. If the
  budget blows, ISO RX packet loss (audio gaps) or I2S underruns result.
  Mitigation: measure recv_cnt vs. expected SDU rate during 4a; if drops
  scale with LC3 complexity, escalate to Phase 6 (FLPR offload) earlier.
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
indefinitely-running audio stream on the nRF54L15; glitch magnitude ≤ 1
sample per correction event; GRTC drift measurement active (not the ISO-ts
fallback); sample_adjust events rare in steady state.

## Phase 5 — ASRC quality upgrade *(conditional)*

Gate: only if Phase 4c listening test finds the single-sample insert/drop
artifact audible. If inaudible, skip — the design's "resampling error sits
below the codec noise floor" argument (Appendix A, option C) holds and
Phase 6 is not needed for quality, only for CPU budget.

- Fixed-point linear-interpolation ASRC on cpuapp; same controller, ratio
  actuator. Measure cpuapp headroom before/after (feeds R-4.1 decision).

## Phase 6 — FLPR offload *(conditional)*

Gate: only if Phase 5 (or Phase 4 + LC3) leaves insufficient cpuapp headroom
(R-4.1 realized).

- Move ASRC to FLPR: shared-SRAM ring buffers, VEVIF/icmsg signaling,
  FLPR reads GRTC directly for drift; fixed-point only (no FPU).
- Accept +1 frame (~10 ms) latency for the extra buffer hop.
- Risk to re-assess at gate time: NCS FLPR/HPF framework maturity.

## Backlog (unscheduled)

- Multi-rate support (16/24 kHz): I2S reconfig from ASE codec config, then
  re-widen the PACS capability.
- Crossfade smoothing on the emergency fallback path (softens the residual
  artifact; complementary to everything above).
- CS2200 (or similar fractional-N clock chip) actuator — the designated
  production-hardware path if a custom PCB happens; I2S slave mode, ppm →
  I²C write, controller unchanged.
- CI revival (build matrix for both boards + unit tests + bsim).
- bsim test expansion (ISO streaming scenarios).
- GRTC-based drift measurement on nRF5340 (no GRTC there — equivalent is
  TIMER capture via DPPI; only if ISO-ts proves too noisy in practice).

---

# Appendix A — Options considered and rejected/deferred (nRF54L15)

Absorbed from `nrf54l15-drift-compensation.md`; recorded so they are not
re-litigated.

| Option | Disposition | Reason |
|---|---|---|
| A. ASRC on cpuapp | **Adopted** (Phase 5) | 5–15 % cpuapp load at 48 kHz stereo; linear interp cheap |
| B. ASRC on FLPR | **Adopted, gated** (Phase 6) | Zero cpuapp impact; costs IPC + fixed-point port + ~10 ms latency |
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
  (not an I2S issue).
- Raw logic-analyzer capture file exists; frequency/data analysis pending.

### Old DAC failure/isolation evidence

- With old DAC breakout connected and MUTE low: D1/LRCK held high, no toggling.
- With digital wires removed (BCK/LRCK/SDOUT disconnected): D1/LRCK toggles.
- Conclusion: old breakout/wiring assembly is incompatible or defective. Must
  not be treated as known-good.

### Pending: main-pipeline / new-DAC retest

- New DAC connected to the Xiao. Audible output not yet confirmed.
- Main receiver firmware with new DAC has not yet been streamed against.
- Required: external analyzer measurement of I2S20 waveform +
  user listening report. Expected BCK ≈ 1.536 MHz, BCK/LRCK ratio = 32.
- If 4a.1 retest still produces slab-full/EIO, compare application queue
  behavior with standalone test before changing source (4a.2).

### Pending: GRTC/DPPI and stability

- GRTC + DPPI drift measurement not yet implemented (Phase 4b).
- No stability/artifact verification yet (Phase 4c, depends on 4a.1 + 4b).
