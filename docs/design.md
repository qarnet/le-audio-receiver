# LE Audio Receiver — Design Document

Status: **historical architecture and evidence** — revised 2026-08-06
(R10 closeout).  Earlier history: accepted 2026-07-05, revised 2026-07-31
(Phase 5 closed — cpuapp ASRC accepted, Mode A+B 600 s zero faults;
Phase 6 FLPR offload Stages 0–5 complete; BZ1 BlueZ/WirePlumber PACS
availability landed; BZ2 BlueZ/WirePlumber stock desktop gate accepted;
BZ3 BlueZ/WirePlumber pairing/reconnect lifecycle accepted; BZ4
compatibility expansion not needed).

The accepted plan of record for the refactoring track (R0–R10) is
`docs/development/refactor-plan.md`.  The T0–T8 behavior-lock track is
COMPLETE/ACCEPTED — **historical evidence** (canonical gate 47 PASS /
0 FAIL / 47 TOTAL, coverage baseline `1a5842d`, builds 3/3, build
contract 76/76, both hardware matrices —
`docs/testing/pre-refactor-hardware-baseline.md`).  The R0–R10 refactor
track COMPLETE/ACCEPTED (2026-08-06) is the **historical refactor
baseline**: canonical gate **55 PASS / 0 FAIL / 55 TOTAL** (31
twister + 5 exec-only + 16 Python + coverage + matrix + BSim), coverage
population **33** (4024/4402 L, 1695/2356 B, 289/289 F; committed
baseline `54a6b8e`), builds 3/3, build contract 79/79, BSim Stage 1 17
scenarios/26 runs pins byte-identical — final evidence in
`docs/development/refactor-r10-results.md`.  The P1–P8 user pairing
control closeout (2026-08-08) is historical evidence: canonical gate
**62 PASS / 0 FAIL / 62 TOTAL** (35 twister + 5 exec-only + 19 Python +
coverage + matrix + BSim), coverage population **36** (4665/5121 lines,
2023/2820 branches, 357/357 functions), build contract **95/95**, BSim
17 scenarios / 26 runs pins byte-identical — see `STATUS.md` and
`docs/development/user-pairing-control-p8-results.md`.  The FR1 packaging
closeout (2026-08-09, canonical gate **63 PASS / 0 FAIL / 63 TOTAL** at
`1671a9f`, 35 twister + 5 exec-only + 20 Python + coverage + matrix + BSim)
is historical evidence — see
`docs/development/firmware-release-fr1-results.md`.  The FR2 firmware-build
CI closeout (2026-08-09, canonical gate **64 PASS / 0 FAIL / 64 TOTAL** at
`75a8093`, 35 twister + 5 exec-only + 21 Python + coverage + matrix + BSim)
is historical evidence — see
`docs/development/firmware-release-fr2-results.md`.  The FR3 automatic
draft-release closeout (2026-08-09, canonical gate **65 PASS / 0 FAIL /
65 TOTAL** at `b70b978`/`3d9a918...`, 35 twister + 5 exec-only + 22 Python
+ coverage + matrix + BSim) is historical evidence — see
`docs/development/firmware-release-fr3-results.md`.  The current
authoritative state is FR4 exact-artifact hardware acceptance, which is
**BLOCKED** (2026-08-10): the exact draft `v0.1.0` failed mandatory
nRF5340 mono acceptance and remains private, unpublished, and untagged;
the local replacement preflight passed both targets at `5e7f502` but is
not exact-artifact acceptance; the root `VERSION` remains `0.1.0` and the
firmware-build workflow is version-driven, but no replacement version or
candidate has been selected; a replacement candidate must be created
through the trusted-main lifecycle and its exact assets must pass FR4
before FR5 can publish anything.  Software gates at this code state:
canonical gate **65 PASS / 0 FAIL / 65 TOTAL**, coverage population **36**
(4777/5234 lines, 2091/2896 branches, 363/363 functions), build contract
**96/96**, BSim 17 scenarios / 26 runs pins byte-identical — see `STATUS.md`
and `docs/development/firmware-release-fr4-results.md` (canonical FR4
evidence).
All
"Phases 0–6" content below is dated architecture/evidence of the
pre-refactor design and is superseded by the module ownership described
in the current-architecture section that follows.

This is the consolidated design doc for the firmware supporting both **nRF5340**
and **nRF54L15**. It records current state, findings (historical), target
architecture, and phased plan.

Each phase below is intentionally concrete-but-not-exhaustive: detailed handoff
documents are written per phase when work on it starts.

---

# Current architecture (post-R10, authoritative)

The R0–R10 refactor track preserved the T8 accepted behavior (behavioral
baseline T8 commit `971e6a4` — not an identical source tree; R0–R9 made
structural production-source changes while preserving behavior, R10 is
docs/evidence only) while reorganizing ownership.  The current module map:

```
src/main.c                     hardware wiring, watchdog, advertising-loop adapter
src/app_lifecycle.c            pure fatal boot coordinator (ordered init, cold reboot,
                               advertising restart) — T6 direct suite
src/bt_bap.c                   BAP unicast server front end: ASCS/PACS, pairing,
                               advertising, thin recv adapter (R6), and the ONE
                               private teardown transition owner (R7:
                               teardown_transition/teardown_close_path —
                               first close wins, per-slot release once,
                               close→drain→sink-stop→offload-stop→reset)
src/bt_pairing_policy.c        pure OPEN/BONDED_ONLY policy snapshot
src/audio_stream_session.c     R6: exclusive owner of app audio receive/session
                               state (codec shape, decoders, per-CIS ISO seq
                               trackers, Mode A assembler, receive counters,
                               decode/conceal/volume/push, admission/lease
                               rx_open/rx_close) — direct-suite covered
src/audio_modea.c              bounded two-CIS event assembler + per-channel PLC
src/audio_iso_seq.c            pure per-CIS omitted-callback sequence tracker
src/audio_decode.c             LC3 decode + channel routing (mono / Mode A / Mode B)
src/audio_sink.h → audio_i2s.c platform-neutral sink (init/push/stop +
                               stream_open/stream_close admission + drain) over
                               slab/DMA I2S, 48 kHz stereo
src/audio_drift.c              dual-term PI clock-recovery controller (ppm output)
src/audio_clock_actuator_*.c   actuator interface: APLL (nRF5340) / NONE (nRF54L15,
                               ASRC consumes ppm directly)
src/audio_timing_*.c           platform timing: nrf54 (GRTC+TIMER20 PCLK measure,
                               feedforward) / none (nRF5340 no-op — no frequency
                               update; see correction note below)
src/audio_asrc.c + src/flpr/*  fixed-point linear stereo ASRC on FLPR (RISC-V VPR)
                               with identical cpuapp fallback; handshake, rings,
                               control-ACK, runtime; R8 split acceptance machinery
                               into src/flpr_acceptance.c + src/flpr/acceptance.c
src/audio_shell.c/bt_shell.c/  R4: shell command ownership split by subsystem;
flpr_shell.c/flpr_acceptance_shell.c  acceptance harness behind
                               CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS
scripts/bap_central.py         R9: thin CLI coordinator; device/security/endpoint/
+ bap_central_{device,security,  session modules each own one domain; CentralCleanup
endpoint,session}.py           single idempotent resource owner
```

Key ownership invariants (all covered by direct unit suites unless noted):

- **Teardown**: one owner (`bt_bap.c`, R7); no callback composes low-level
  stop/reset calls.
- **Receive/session**: `audio_stream_session.c` (R6) owns all app audio
  state; `bt_bap.c` owns Bluetooth objects/callbacks only.
- **Admission**: `audio_sink_stream_open()/close()` (R1) gate push
  admission; `audio_sink_stop()` drains admitted pushes before DROP.
- **FLPR diagnostics**: acceptance-only machinery is configurable
  (R4/R8); core ring/handshake/runtime files contain production logic.
- **Central tooling**: discovery/security/endpoint/session/teardown each
  have one module (R9).

Full per-file coverage evidence: `docs/testing/coverage-matrix.md`;
behavior contracts: `docs/testing/behavior-contract.md`.

# Part I — Current state & findings

## What works today

- **nRF5340 (Ebyte E83-2G4M03S module)**: BAP Unicast Server, sink-only,
  2 sink ASEs, LC3 decode (mono / stereo Mode A / stereo Mode B), VCP volume,
  shell diagnostics, watchdog. Audio out via I2S to UDA1334A DAC.
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
  **Main receiver end-to-end audio: ACCEPTED** (Phase 5 cpuapp ASRC: Mode A +
  Mode B 600 s zero faults; Phase 6 FLPR offload: Mode A + Mode B 120 s zero
  faults). Physical audibility UNAVAILABLE — see Phase 4/5 for completed
  measurable gates and evidence.
- Clean small modules: `audio_stats`, `audio_volume`, `audio_shell`.
- Current architecture modules: `app_lifecycle.c` (pure fatal boot
  coordinator — ordered init, cold reboot, advertising restart),
  `audio_modea.c` (bounded two-CIS event assembler with per-channel PLC),
  `audio_iso_seq.c` (pure per-CIS omitted-callback sequence tracker),
  `bt_pairing_policy.c` (pure OPEN/BONDED_ONLY policy snapshot; Bluetooth
  controller work remains in `bt_bap.c`).

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
frequency term from PCLK-vs-GRTC measurement (nRF54L15), plus phase term
from I2S buffer fill. Output in ppm, routed to platform-specific actuators
(APLL or consumed by ASRC). The packet-repeat fallback no longer fires in
steady state. See Part II §Clock recovery and `AGENTS.md` "Drift
controller" for current production architecture.

> **Correction (R10, 2026-08-06):** the historical text below and the
> "Drift measurement (production)" bullet previously claimed the nRF5340
> frequency term comes from "ISO `info->ts` deltas".  That is stale:
> `audio_sink_sdu_ref_update()` was REMOVED (Phase 4b.2) and ISO
> timestamps go ONLY to `audio_timing_sdu_ref_update()` — which is the
> nRF5340 no-op `audio_timing_none.c` (no GRTC/TIMER20).  Current
> production: **no frequency update on nRF5340** — feedforward term stays
> zero; phase-only PI from I2S buffer fill.  nRF54L15 keeps the 1 s
> PCLK-vs-GRTC feedforward.

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

**Actuators** behind one interface, selected per platform via Kconfig choice:

| Kconfig | Platform | Mechanism |
|---|---|---|
| `AUDIO_CLOCK_ACTUATOR_APLL` | nRF5340 | ppm → HFCLKAUDIO register trim (true clock steering) |
| `AUDIO_CLOCK_ACTUATOR_NONE` | nRF54L15 | no-op; ASRC consumes ppm directly in data path |

The nRF54L15 has no steerable audio clock. The PI controller output (ppm)
goes directly into the fixed-point linear ASRC resampler ratio. NONE actuator
is the production choice — no physical actuator on nRF54L15.

The data-path ASRC is a stateful cross-block fixed-point linear-interpolation
stereo resampler. Primary path runs on FLPR (RISC-V VPR); cpuapp ASRC is the
identical fallback. Controller → correction → [APLL hardware trim (nRF5340)
OR ASRC data-path resampler (nRF54L15)].

Historical: SAMPLE_ADJUST (sample insert/drop) was Phase 4's actuator;
retired after Phase 5 ASRC acceptance. Source retained for regression
testing only. AUDIO_CLOCK_ACTUATOR_ASRC was an architectural
placeholder — the ASRC is a data-path consumer of ppm, not a Kconfig
actuator entry.

The packet-repeat fallback in `audio_i2s.c` remains as an emergency path
only; a converged loop must not trigger it in steady state.

### Drift measurement (production)

- **nRF54L15**: TIMER20 in TIMER mode (PCLK-derived free-running ticks).
  GRTC compare at 1-second intervals triggers TIMER20 `TASKS_CAPTURE` via
  GPPI, hardware-snapshotted. The GRTC ISR reads the captured count, computes
  unsigned delta and elapsed GRTC microseconds, derives integer ppm.
  `audio_drift_frequency_error_update()` feeds this into the PI controller.
- **nRF5340**: no frequency measurement.  `audio_timing_none.c` is the
  production no-op timing module (no GRTC/TIMER20 on this platform);
  `audio_drift_frequency_error_update()` is never called there.
  Feedforward term stays zero; phase-only PI using I2S buffer fill.
  > **R10 correction:** earlier text claimed nRF5340 used "ISO
  > `info->ts` deltas" for a frequency term.  That path was removed
  > (Phase 4b.2): `audio_sink_sdu_ref_update()` no longer exists and ISO
  > timestamps reach only the (no-op) `audio_timing_sdu_ref_update()`.
  > The nRF5340 actuator (APLL) is driven by the phase-only PI output.

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
  `nrfutil sdk-manager toolchain env` shellHook, locked `nix-nrf-dev`
  `nrfutil` package,
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
pinctrl fix, SDC-on-cpuapp buffer counts), boot +
PACS/ASCS + BlueZ bonding verified. BLE CIS transport verified through
nRF5340DK `hci_uart` central. GPIO mapping D0/D1/D2 (P1.4/P1.5/P1.6) proven.
Standalone I2S20 DMA test completed (20 s, 2,016 blocks, no EIO). The old
DAC breakout held D1/LRCK high when unmuted — incompatible or defective
assembly; replaced with known-good DAC. All sub-gates completed: rate
conversion (4a.2), GRTC-driven PCLK feedforward + phase PI (4b.1/4b.2),
and 10-minute stability gate (4c) — all PASS. External digital I2S gate
PASS at DAC pins (BCK/LRCK ratio 31.999701). Physical audibility
UNAVAILABLE by user — not failed, not blocking further measurable work.

### Phase 4 sub-gates (all PASS — 2026-07-26)

- **4a — I2S20 hardware + DAC**: GPIO pin map confirmed (D0/D1/D2).
  Standalone I2S20 DMA test: 20 s, 2,016 blocks, zero EIO. Old DAC
  breakout defective (held LRCK high); replaced. New DAC: 60,000 frames /
  600 s stream with zero faults. Rate converter (4a.2): bounded
  nearest-neighbor maps 480 → 476/477 frames for PCLK32M mismatch.
- **4b — Drift measurement + feedforward**: GRTC → GPPI → TIMER20
  hardware capture (1 s intervals). PCLK frequency error fed into PI
  controller. 4b.1 logged diagnostics; 4b.2 closed loop: 4,500 frames /
  45 s, PCLK +1,500..+1,757 ppm, inserts dominate (186:1). I2S
  FRAMESTART invalidated (fires at DMA boundaries, not LRCK edges).
- **4c — Stability gate**: Technical PASS — 60,000 frames / 600 s, zero
  faults, zero underruns. External I2S analyzer PASS: BCK/LRCK ratio
  31.999701. Physical audibility UNAVAILABLE.

Results consolidated in `docs/development/phase4-acceptance-results.md`.

### Phase 4 risks (tracked, not deferred)

- **R-4.1 CPU budget on single core**: SDC radio ISR + BT host ISO RX +
  LC3 decode (×2 for Mode B) + I2S DMA + ASRC, all on 128 MHz cpuapp.
  Mitigated by FLPR offload (Phase 6).
- **R-4.2 Central ISO/CIS quirks**: nRF5340DK `hci_uart` central is the
  verified transport. Other centrals need independent ISO/CIS verification.
- **R-4.3 nRF5340DK fallback as truth source**: known-good nRF5340 target
  for debugging central/test setup issues.

**Exit criterion (whole phase)**: All sub-gates green (4a, 4b, 4c).
See `docs/development/phase4-acceptance-results.md`.

## Phase 5 — ASRC quality upgrade ✅ COMPLETE (2026-07-27)

cpuapp fixed-point linear stereo ASRC accepted. Mode A (two mono ASEs) +
Mode B (single stereo ASE) each ran 600 s autonomous central streams on
nRF54L15 with zero faults. SAMPLE_ADJUST actuator removed from production
Kconfig; two actuators remain: APLL (nRF5340) and NONE (nRF54L15, ASRC
consumes ppm). All 20 ASRC unit tests pass (native_sim). nRF5340 builds
unchanged.

### Architecture

Stateful cross-block stereo s16 linear interpolation with continuous phase
accumulator and sample history (one previous sample per channel) carried
across block boundaries. No heap allocation; stack/static only. Reset state
on stream stop/disconnect.

Controller output feeds resampler ratio: `source_step = 1 + ppm·1e-6`.
Positive ppm → consume source faster → fewer output samples (equivalent to
drop). Negative ppm → consume source slower → more output samples (equivalent
to insert).

ASRC replaces the fixed-rate converter on nRF54L15 path. nRF5340 stays
identity bypass.

### Acceptance evidence

- 20/20 unit tests PASS (native_sim)
- Mode A 600 s: 60,000 frames, zero faults
- Mode B 600 s: 60,000 frames, zero faults
- nRF5340 builds (regression deferred — no E83 probe)
- Results: `docs/development/phase5-hardware-acceptance-results.md`

## Phase 6 — FLPR offload ✅ COMPLETE (2026-07-29)

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

**Results**: 432 unit tests pass (396 C + 36 Python), nRF54L15 CPUAPP FLASH
502904 B / RAM 152244 B. Hardware: Mode A 120 s + true Mode B 120 s at 100
fps zero faults, Mode A 180 s zero faults. See
`docs/development/phase6-stage5-results.md`.

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

BabbleSim Stage 1 is an **accepted regular local gate**: the 17-scenario T4+R7
BAP matrix over real `src/bt_bap.c`, `src/audio_decode.c`, real Zephyr
BAP/ASCS/PACS, real ISO transport, and real liblc3
(`scripts/bsim-stage1-run.sh`, scenarios 1–9 run twice, remaining eight
once = 26 runs), with a strict PCM oracle and pinned deterministic hashes.  The official upstream smoke
(`scripts/bsim-official-smoke.sh`) remains **PARTIAL** because of the
documented upstream teardown disable-race and is **not** production
acceptance.  It complements, never substitutes, native unit tests and
real-hardware central-driven tests.

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
  FNV-1a hash.  Startup accounting uses local counters (sink stub only); no
  test fields in production `audio_stats`.  Validates `plc == startup_plc`,
  `total == pushes + startup_zero`, zero errors, nonzero hash, deterministic
  energy across runs.  CONFIG_TEST decode bypass removed — BSIM path identical
  to production hardware.
- **Runner** (`scripts/bsim-stage1-run.sh`): compile + run with per-process log
  capture, exit-code validation, PASS-marker parsing with invariant checks.
- **Official smoke** (`scripts/bsim-official-smoke.sh`): compiles upstream
  BAP unicast audio test, exits nonzero on known teardown disable-race → Baseline
  PARTIAL.

### Stage 1 acceptance + cleanup (2026-07-29) — historical evidence

> **Historical (pre-T2 oracle):** this stage-1 acceptance predates the T2B
> mono overlap-safe expansion fix; the `0xFE0D4245` hash below locked in the
> forward-expansion collapse defect and was superseded by the corrected T2
> values and then by the T4/T4+R7 scenario matrix (see `STATUS.md` T2/T4
> sections and `docs/development/bsim-stage1-results.md`).  Kept as dated
> evidence only; the current accepted gate is
> `scripts/bsim-stage1-run.sh`.

- Advertising → pairing → PACS/ASCS → one sink ASE → CIS start → valid LC3
  fixture (48 kHz, 48_4_1 preset) → 104 client sends → 100 nonzero receiver pushes.
- `startup_zero=8`, `startup_plc=7`, `plc=7`, `total=108`, `hash=0xFE0D4245`,
  `energy=12480` — fully deterministic across two consecutive runs.
- Production `audio_stats.h/.c` cleaned: startup fields and functions removed;
  startup accounting is local to sink stub only.
- Client `ASE_SRC_COUNT=2` (min viable per upstream BUILD_ASSERT + stream_tx.c
  array sizing).
- Both real-target builds clean (nRF5340, nRF54L15).
- Stage 1 accepted as regular local gate.  Official upstream smoke remains PARTIAL.
- Results: `docs/development/bsim-stage1-results.md`.

### Planned beyond Stage 1

The accepted BSim scenario set is the 17-scenario T4+R7 matrix
(mono/Mode A/Mode B 7.5+10 ms incl. one-CIS-loss, lifecycle, reconnect,
rejection, duplicate-release, and invalid-codec scenarios, run by
`scripts/bsim-stage1-run.sh`).

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
| A. ASRC on cpuapp | **Adopted** (Phase 5 — complete) | Linear interpolation at 48 kHz stereo; continuous-phase cross-block; 600 s Mode A+B zero faults |
| B. ASRC on FLPR | **Adopted** (Phase 6 — complete) | Zero cpuapp ASRC load; IPC + fixed-point port; RV32E no-FPU; 120 s Mode A+B zero faults |
| E. Single-sample insert/drop | **Adopted then retired** (Phase 4 only) | Degenerate ASRC; superseded by linear ASRC in Phase 5 |
| C. FLPR bit-banged BCLK/LRCK (I2S slave) | **Rejected** | Any FLPR stall becomes clock jitter → audible; burns FLPR entirely |
| D. PWM-generated I2S clock (slave) | **Rejected** | Same bit-perfect argument as C; limited frequency resolution (~PCLK/N steps); needs physical wires + DPPI choreography to keep LRCK = BCLK/64. |
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
  `docs/development/phase4-acceptance-results.md`.

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
  `docs/development/phase4-acceptance-results.md`.
- Raw capture file at `/tmp/opencode/phase4c-i2s.sr` (not committed).

### Pending: audible quality

- Technical stability gate PASS (Phase 4c): 10-minute uninterrupted stream,
  zero faults, controller converged. See `docs/development/phase4-acceptance-results.md`.
- External digital I2S gate PASS at DAC pins: fx2lafw analyzer confirmed
  valid I2S waveforms (BCK/LRCK ratio 31.999701).
- Physical audibility of ~86/s sample inserts at ~+1,800 ppm PCLK offset
  marked UNAVAILABLE by user — not failed, not blocking.
- Phase 5 linear ASRC completed and accepted (Mode A+B 600 s zero faults),
  Phase 6 FLPR offload completed (Mode A+B 120 s zero faults). Physical
  audibility remains UNAVAILABLE; measurable gates all PASS.
