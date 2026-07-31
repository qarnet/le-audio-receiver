# STATUS — le-audio-receiver — 2026-07-31

> Probe identities are resolved at runtime via `nrf-probes`. Never assume a
> serial↔board mapping from docs — run `nrf-probes`.

## Pre-refactor testing track — STARTED (2026-07-31)

Test plan accepted: `docs/development/pre-refactor-testing-plan.md`.  Phases
T0–T8 lock current supported behavior before large-scale refactoring.

**Phase T0 — behavior contract and honest coverage map** — ACCEPTED (2026-07-31).

Creates `docs/testing/behavior-contract.md` (57 numbered contracts for
supported and unsupported behavior), `docs/testing/coverage-matrix.md` (every
production source file classified with current evidence and gaps), and
`docs/testing/v0.0.1-baseline.md` (release and RF-fix baseline evidence).

Acceptance evidence:

- Full gate **21 PASS / 0 FAIL / 21 TOTAL** on the provisioned workstation
  (`thomas-workstation`) from a detached temporary worktree of the exact T0
  commit, transferred via non-destructive git bundle; BSim hashes
  deterministic across repeated runs (10 ms `0xFE0D4245`,
  7.5 ms `0x5853F445`).
- All three builds pass on the T0 commit: `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle`.
- No production behavior changed; T0 touched documentation only.
- No numeric line/branch coverage is claimed; no honest coverage report
  exists until Phase T7 instrumentation.

**Phase T2 — audio pipeline unit characterization** — in progress.

Locks LC3 decode/routing, volume, and statistics to direct production-source
proof: deterministic 48 kHz golden fixtures, real-decode golden tests, and
three production decoder defects fixed.  Evidence:
`docs/testing/t2-audio-pipeline-tests.md`; updated
`docs/testing/behavior-contract.md` (CODEC-006..010, STAT-001) and
`docs/testing/coverage-matrix.md`.

- `tests/fixtures/lc3/` — four checked-in fixture pairs (mono 7.5/10 ms,
  Mode B 7.5/10 ms; 60-byte LC3 frames), reproducible generator
  (`generate.sh`, liblc3 C API, `-O3 -std=c11 -ffast-math`), deterministic
  and path-independent (verified by repeated runs), SHA-256 + CRC-32
  recorded.  Tests embed the binaries; fixtures are never regenerated
  during test runs.
- `tests/unit/decode` — 37 tests execute real `audio_decode.c` +
  `audio_stats.c` + real liblc3 1.1.2: byte-exact golden PCM for all four
  fixtures, full/per-channel CRC-32, config rejection (liblc3 untouched),
  SDU rejection with output guards, PLC accounting, overlap-safe mono
  expansion, Mode B dual accounting, hard-failure accounting.
- `tests/unit/volume` — 12 tests execute the real VCP branch against a
  test-local shadow of the exact NCS v3.3.0 renderer types + fake
  `bt_vcp_vol_rend_register()`; real `audio_perf.c` proves hook balance on
  all exits; concurrent callback toggling proves atomic snapshot packing.
- `tests/unit/stats` — 10 tests execute real `audio_stats.c`: exact
  counter coupling, reset, by-value snapshots, 4-thread concurrent exact
  counts.
- Fixed defects: mono in-place expansion overlap corruption (backward
  expansion when input/output share the base); Mode B right-channel
  decoder accounting (was: success/PLC uncounted); hard decode failures
  propagated as `-EBADMSG` instead of success (second Mode B decoder still
  invoked to keep independent state aligned).
- liblc3 1.1.2 semantic correction: malformed bitstream of valid length
  returns 1 (PLC), not a hard negative — verified empirically; the
  hard-error accounting path is exercised via a test-only linker wrap of
  `lc3_decode()` (delegates to the real implementation otherwise).
- ASCS response-code mapping of decode-layer rejection remains T4 (known
  gap preserved).

Focused suites (desktop `thomas-main`): decode 37/37, volume 12/12,
stats 10/10 — zero compiler warnings.

Acceptance evidence (to be completed on `thomas-workstation` from a
detached temporary worktree of the exact T2 commit, transferred via
non-destructive git bundle):

- Desktop full gate: **22 PASS / 1 FAIL / 23 TOTAL** — the only failing
  child is `bsim: stage1`, which cannot run on `thomas-main` because the
  BabbleSim component binaries are not built there
  (`~/ncs/v3.3.0/tools/bsim/bin/bs_2G4_phy_v1` missing); the workstation
  provides the authoritative BSim leg (same as T1).
- All three builds pass on the T2 working tree: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` (documented Kconfig/CMake
  diagnostics only; no compiler warnings).
- **T3 is next**: I2S and sink state machine.

**Phase T1 — FLPR production-source tests** — ACCEPTED (2026-07-31).

Replaces the false-confidence FLPR runtime, ring-manager, and handshake
suites with native_sim suites that compile and execute the real production
implementations.  Evidence and hook architecture:
`docs/testing/t1-flpr-production-tests.md`; updated
`docs/testing/coverage-matrix.md`.

- `tests/unit/flpr_runtime` — 21 tests execute the real nRF54 restart body
  (shadow VPR HAL, host source/execution arrays, ordered event log, mutex
  busy thread).  Fixed: `failed_stage` set before disconnect/stop, failed
  attempts included in `max_duration_ms`, header/module wording corrected
  (no active-stream rejection inside `flpr_runtime_restart()`, held-reset
  sequence).
- `tests/unit/flpr_ring_mgr` — 53 tests execute `src/flpr_ring_mgr.c` +
  real `flpr_ring.c`/`flpr_cache.c` with host ring arrays; production
  static IPC handlers run through the handshake-mock captured handlers.
- `tests/unit/flpr_handshake` — 44 tests execute `src/flpr_handshake.c`
  against a fake IPC service backend (NCS v3.3.0 ipc_service test pattern,
  real `ipc_service_*` APIs).  Fixed: `flpr_handshake_send_msg()` routes
  through `send_msg()` so failures count `err_send`; header wording
  corrected (ring handlers dispatch without the module spinlock).
- No copied restart/reset/produce/consume/callback algorithm remains
  primary proof in any of the three suites.
- Physical cache/FLPR entry-point behavior remains hardware-only; native
  `flpr_cache.c` coverage is API/barrier-call proof only.

**Review fix round (2026-07-31, commit `fix: enforce FLPR validation
before acceptance`)** — closes two production-contract violations found
during orchestrator review:

- `flpr_handshake_wait_new_ready()` fast path now requires
  `flpr.ready && flpr.acked && flpr.epoch != previous_epoch`.  A changed
  epoch whose READY_ACK send failed can no longer succeed without
  waiting; the fast path is taken only after the ACK for the changed
  epoch succeeded (new regression test + extended ACK-failure test).
- `flpr_ring_mgr_consume_asrc_result()` now verifies the payload CRC over
  the ring payload BEFORE copying PCM to the caller.  Every validation
  failure preserves both caller buffers byte-for-byte; only
  `result->output_frames` is zeroed, exactly as the header contract
  documents (sentinel-buffer tests for CRC, flags, frame range, reserved
  state, and zero-frame error output).
- T1 evidence (`docs/testing/t1-flpr-production-tests.md`) updated: the
  two previously "characterized" behaviors are now recorded as fixed
  defects; the inaccurate CRC "payload may carry data" characterization
  is removed.

Acceptance evidence:

- Focused suites on the desktop (`thomas-main`): runtime 21/21,
  ring manager 53/53, handshake 44/44 — zero compiler warnings.
- Full gate on the provisioned workstation (`thomas-workstation`) from a
  detached temporary worktree of the exact T1 commit, transferred via
  non-destructive git bundle: **21 PASS / 0 FAIL / 21 TOTAL** — three
  consecutive full-gate runs on the final validated commit, all clean;
  BSim hashes deterministic in every run — 10 ms `0xFE0D4245`,
  7.5 ms `0x5853F445`.
- Transient-failure disposition: the earlier single 20/21 gate run's
  failing child could not be identified after the fact — its console
  output was not retained (only BSim logs survive in `/tmp`) and no
  other evidence exists.  Per the review-fix handoff fallback rule, no
  root cause is claimed; the unsupported claim was removed and the
  final exact-commit gate was instead run three consecutive times, all
  passing (see above).
- All three builds pass on the T1 commit: `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle` (both desktop and workstation).
- Desktop gate is 20/21 locally: the `bsim: stage1` child cannot run on
  `thomas-main` because the BabbleSim component binaries are not built
  there (`~/ncs/v3.3.0/tools/bsim/bin/bs_2G4_phy_v1` missing); the
  workstation provides the authoritative BSim leg.
- Production fixed addresses and wire ABI unchanged; production images
  contain no test hooks (test-only compile definitions are applied only by
  test CMakeLists).
- Worktree clean after scoped commits; workstation repo returned to clean
  `main` with all temporary refs/worktrees/bundles removed.
- **T2 is next**: audio pipeline unit characterization (decode golden
  output, volume, stats).
- No numeric line/branch coverage is claimed; no honest coverage report
  exists until Phase T7 instrumentation.

The existing 432 unit tests (396 C + 36 Python) are a historical Phase 6
count and do NOT mean full production branch coverage.  Several suites
compile stubs, test copied models, or test retired implementations.
T1–T8 will replace weak tests with production-source tests and add coverage
for untested modules.

Manual connection after RF-switch fix (PR #3, merged
`20b37c405835e5c2c747fa7b072c4c0b752b29cd`) is recorded as hardware evidence
in the baseline but is not automated regression protection.

## Phase 3 — BlueZ/WirePlumber pairing and reconnect lifecycle — ACCEPTED (2026-07-31)

Phase 3 accepted with three autonomous strict stock playbacks on nRF54L15.
Full 12-step sequence (unpair, remove host device, pair, trust, connect,
playback, disconnect, reconnect, playback, reset, reconnect, playback) all
pass without repo harness. Three 30 s playbacks at 7.5 ms frame duration,
zero decode/I2S/offload faults each.

Preflight hardened with two corrections landed in final review:

- **SPA proof**: `_wait_for_bluez_spa()` now requires only `libspa-bluez5.so`
  mapped in the WirePlumber process (`/proc/<pid>/maps`). All fallback paths
  removed — `pw-cli info all` substring (final review) and `pw-dump`
  factory/device node (spa-proof-fix). Owned WP uses subprocess PID;
  active-seat WP resolves `MainPID` via systemd.
- **Remove fatal**: failed `bluetoothctl remove` is now fatal unless exact
  postcondition shows device object no longer exists and no `Paired`/`Bonded`
  state remains.

See `docs/development/phase3-results.md` for acceptance evidence,
`docs/development/bluez-wireplumber-phase3-final-review-handoff.md` for
execution handoff, and `docs/development/bluez-wireplumber-interoperability-plan.md`
for Phase 1–4 plan.

**Phase 4 compatibility expansion NOT needed.** Bare BAP passed with stock
WirePlumber main-systemwide playback. CAP/CAS remain disabled; no speculative
services or custom host policy required.

## Phase 2 — BlueZ/WirePlumber stock desktop gate — ACCEPTED (2026-07-31)

Phase 2 accepted with strict nonzero-audio/zero-fault evidence on nRF54L15,
stock WirePlumber main-systemwide playback:

- **30 s gate**: SDUs=4578, decoded=4729, decode_err=0, i2s_underrun=0,
  stream_reset=0 (~35.47 s at 7.5 ms / 133.3 fps).
- **120 s gate**: SDUs=16565, decoded=16722, decode_err=0, i2s_underrun=0,
  stream_reset=0 (~125.41 s at 7.5 ms / 133.3 fps).
- **Explicit runtime `I2S DMA started`** confirmed each run.
- **Canonical gate**: 20/20 gate tests pass.
- **BSim regression**: 10 ms hash `0xFE0D4245`, 7.5 ms hash `0x5853F445` —
  each scenario run twice with pairwise hash equality enforced; both
  fully deterministic across repeated runs.
- **Corrective fixes**: 10 ms missing-frame-duration fallback removed;
  I2S slab block count raised 12→16 (startup transient headroom);
  `INPUT_FRAMES` made dynamic for 7.5 ms stock PipeWire config.

See `docs/development/phase2-stock-desktop-gate-results.md` for full evidence
and `docs/development/bluez-wireplumber-interoperability-plan.md` for Phase
1–4 plan.

## Phase 1 — BlueZ/WirePlumber PACS availability — DONE (2026-07-30)

Sink Available Audio Contexts no longer cleared to `BT_AUDIO_CONTEXT_TYPE_NONE`
on ACL connect. ACL connection is not ASE ownership — stock desktop policy
(BlueZ/WirePlumber) reads PACS during connection and needs truthful contexts
to create audio devices. Regression test added to BSIM gate (PACS assertion
at PASS point verifies contexts non-NONE after connection + 100-frame stream).

Contexts persist from `bt_bap_init()` through connect/disconnect cycles.
Zephyr PACS restores default on ACL disconnect per spec — no manual restore
needed. See `docs/development/bluez-wireplumber-interoperability-plan.md` for
full Phase 1–4 plan and `docs/development/bluez-wireplumber-phase1-handoff.md`
for execution handoff.

## Stage 0 — PASS (2026-07-27)

Dongle compile-time identity fix landed (87b8d36). hci_ipc netcore firmware calls
`bt_ctlr_set_public_addr()` before `bt_enable_raw()` with lab-only address
`C0:AA:BB:CC:DD:EE` (see `dongle/hci_identity.h`). No more `btmgmt static-addr`
workaround — scanning and GATT discovery work natively. Build via `fw-build-dongle`.

SMP pairing fix landed (52abde1). `--peer-addr` raw-HCI path now uses
`own_address_type=public` (0x00) matching the dongle's compile-time identity.
Previously used Random (0x01) causing DHKey Check mismatch + SMP timeout.
ACL held open for `duration+120 s` so `Pair()` succeeds over existing ACL.

**Stage 0 gate closed**: 60 s Mode A stream (6000 frames, 100.0 fps) on nRF54L15.
Pairing + bond + 2 ASE config + CIS audio path fully verified on clean state
(no prior bonds). RX hex dump (chan_alloc 0x01+0x02) confirmed stereo content.
FLPR: healthy, errors zero, RX lost/dup/ooo/missed zero. Audio: push failures=0,
repeat fb=0, ASRC cap fail=0. Zero warnings, zero assertions, zero faults.

See `docs/development/phase6-stage0-results.md` for full verification evidence.

## BSIM Stage 1 — PASS + CLEANUP + REPEATED-RUN GATE (2026-07-31)

CONFIG_TEST decode bypass removed from `bt_bap.c`. BSIM now executes same
PLC/decode path as hardware.  Startup-zero/PLC oracle in `audio_sink_stub.c`
with local counters (not production `audio_stats`):
8 startup-zero pushes, 7 PLC frames (all before first nonzero PCM).  100
nonzero pushes, 104 client sends.  Fully deterministic across repeated runs.

10 ms (48_4_1) — two independent runs:
- `startup_zero=8`, `startup_plc=7`, `plc=7`, `total=108`
- `total == pushes + startup_zero` (108 = 100 + 8)
- `plc == startup_plc` (7 = 7, all PLC in startup)
- `hash=0xFE0D4245`, `energy=12480` (pairwise identical both runs)

7.5 ms (48_3_1) — two independent runs:
- `startup_zero=11`, `startup_plc=10`, `plc=10`, `total=111`
- `total == pushes + startup_zero` (111 = 100 + 11)
- `plc == startup_plc` (10 = 10, all PLC in startup)
- `hash=0x5853F445`, `energy=9636480..9637920` (pairwise identical both runs)

Repeated-run gate (`scripts/bsim-stage1-run.sh`): each scenario runs twice
with pairwise hash equality enforced AND known accepted values asserted
(10 ms → `0xFE0D4245`, 7.5 ms → `0x5853F445`).  Per-run unique logs with
all artifact paths and hashes printed.

Production `audio_stats` cleaned — `startup_zero`/`startup_plc` fields and
functions removed; startup accounting is local to sink stub.  Real-target
public API restored to pre-BSim shape.  Client `ASE_SRC_COUNT=2` (min viable;
upstream BUILD_ASSERT rejects 1; 0 compiles but `stream_tx_register` returns
-ENOMEM on zero-element `tx_streams[]`).  Official smoke exits non-zero →
**Baseline PARTIAL** (upstream teardown disable-race).  Both real-target builds
clean (nRF5340, nRF54L15).  Stage1 accepted as regular local gate; official
smoke remains PARTIAL.  Scope stops here: reconnect/Mode A/B/error injection
duplicate hardware coverage.  See `docs/development/bsim-stage1-results.md`.

## Phase 5 — COMPLETE (2026-07-27)

cpuapp fixed-point linear stereo ASRC accepted. Mode A (two mono ASEs) +
Mode B (single stereo ASE) each ran 600 s autonomous central streams on
nRF54L15 with zero faults. SAMPLE_ADJUST actuator removed from production
Kconfig; two actuators remain: APLL (nRF5340) and NONE (nRF54L15, ASRC
consumes ppm). All 20 ASRC unit tests pass (native_sim). nRF5340 builds
(hardware regression deferred — no E83 probe). See
`docs/development/phase5-hardware-acceptance-results.md`.

## Bottom line

**BLE ISO transport verified** — 3,000 ISO Data TX packets over 15 s through
nRF5340DK `hci_uart` central; two CISes (Mode A stereo), 48 kHz LC3 at 100
fps, zero flow-control stalls. Receiver SDUs arrive correctly.

**Standalone I2S20 hardware/DMA verified** — a standalone I2S20 tone test ran
20.001 seconds, fed 2,016 blocks, zero EIO/underrun. I2S20 register state
(ENABLE, PSEL, FRAMESTART) confirmed working. GPIO mapping D0/P1.4 (BCK),
D1/P1.5 (LRCK), D2/P1.6 (SDOUT) proven.

**Old DAC caused LRCK anomaly** — with the old DAC breakout connected and MUTE
low, D1/LRCK was held high (no toggling). With digital wires removed, D1
toggles. The old breakout/wiring assembly is incompatible or defective.

**Phase 4b.2 — PCLK feedforward + phase PI: HARDWARE PASS** (2026-07-26).
Refactored `audio_drift.c` to explicit PCLK frequency feedforward
(`audio_drift_frequency_error_update()`) and per-block buffer-phase PI
(`audio_drift_controller_update(slab_free)`).  Pure integer, no floating
point.  Corrected sign (phase error = setpoint - slab_free).  Directional
anti-windup: at saturation rail, same-direction phase increments are
blocked; opposite-direction increments are always allowed so the
integrator can unwind toward range.  Thread-safe: k_spinlock serialises
workqueue/audio-path/reset calls.  Configurable output clamp and phase
integral clamp via Kconfig.  nRF54L15 board sets output clamp=2000,
phase integral=150.  nRF5340 keeps defaults (500/500).  Removed
timestamp-based frequency estimation and `audio_sink_sdu_ref_update()`.
ISO timestamps now go ONLY to `audio_timing_sdu_ref_update()` for GRTC
scheduling.  nRF54 timing feeds every 1 s PCLK measurement (not just
diagnostics) to `audio_drift_frequency_error_update()` via the work
handler (ISR-safe).  Bounded runtime actuator evidence: in `audio_i2s.c`,
counts insert/drop adjustments; logs first and every 500th adjustment.
Closed-loop streaming verified: 4,500 frames / 45 s at 100 fps, PCLK
diagnostics +1,500..+1,757 ppm, channel-pair gate correct (drops-only
before first PCLK measurement), inserts dominate (186:1 ratio), clean
teardown.  Phase 4c (10-minute stability + listening test): **Technical
PASS** (60,000 frames / 600.00 s, zero disconnect, zero
slab-full/underrun/warning/error/fault, clean teardown).  Physical
audibility UNAVAILABLE (user did not provide listening report) — not failed,
not blocking further measurable work. Phase 5 ASRC is complete (see below).
Phase 4 evidence consolidated in `docs/development/phase4-acceptance-results.md`.

| Test | Result |
|------|--------|
| fw-build-5340 | PASS (8 Kconfig/CMake diagnostics; no compiler warnings) |
| fw-build-54l15 | PASS (5 Kconfig/CMake diagnostics; no compiler warnings) |
| ASRC unit tests | 20/20 PASS (native_sim) |
| drift unit tests | 18/18 PASS |
| actuator unit tests | 7/7 PASS |
| timing unit tests | PASS |
| lifecycle unit tests | PASS |
| decode unit tests | PASS |
| rate_convert unit tests | PASS |
| flpr unit tests | PASS (6 suites: handshake, protocol, ring, ring_mgr, runtime, audio_process) |
| audio_offload unit tests | PASS |
| offload_asrc unit tests | PASS |
| perf unit tests | PASS |
| Python gate tests | 36/36 PASS (gate + flpr_stall_gate) |
| BSIM Stage 1 | PASS (hash=0xFE0D4245 deterministic) |
| Total unit tests | 432 PASS (396 C + 36 Python) |

### Build warning diagnostics (2026-07-30)

Neither target produces compiler warnings in application or Zephyr source.
All printed diagnostics are Kconfig/CMake configuration messages.

**nRF5340 (8 diagnostics):**

| Diagnostic | Classification | Cannot remove because |
|---|---|---|
| Deprecated `PARTITION_MANAGER` / `_ENABLED` | NCS v3.3.0 SDK deprecation | Required for multi-image flash layout; no migration path in v3.3.0 |
| `__ASSERT()` statements globally ENABLED | Zephyr informational | Not a defect |
| `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice has no selection | Upstream Kconfig gap in SW Split LL | Both sub-options (`RELIABILITY`, `LOW_LATENCY`) depend on `BT_CTLR_CENTRAL_ISO`, which is disabled for peripheral-only; choice has no NONE fallback |
| Experimental `BT_LL_SW_SPLIT` | Required architecture | Only ISO-capable open-source controller for nRF5340 |
| Experimental `BT_CTLR_SET_HOST_FEATURE` | Required for ISO | Feature negotiation required |
| Experimental `BT_CTLR_PERIPHERAL_ISO` | Required for ISO | Peripheral ISO support required |
| `SB_CONFIG_PARTITION_MANAGER` sysbuild warning | Required sysbuild infrastructure | Partition manager required by NCS build system |

**nRF54L15 configuration diagnostics:**

| Diagnostic | Classification | Cannot remove because |
|---|---|---|
| FLPR `UART_CONSOLE=y` resolves to `n` | Upstream cpuflpr board defconfig conflict | Board enables UART console; FLPR image deliberately disables `SERIAL` and `CONSOLE` because it has no UART |
| FLPR/CPUAPP reserved-memory unit-address and `simple_bus_reg` warnings | Intentional downstream overlay of stock `nordic-flpr` memory nodes | Stock node names retain old unit addresses while repo overrides `reg` to reserve IPC and 64 KiB FLPR SRAM; resolved addresses and hardware operation are verified |
| FLPR stock RRAM `avoid_unnecessary_addr_size` warning | Upstream nRF54L15 DTS structure | Emitted from stock `rram@165000`; repo does not define that node |
| `__ASSERT()` statements globally ENABLED | Zephyr informational | Not a defect |
| `drivers__watchdog`: No SOURCES given | Zephyr internal: `CONFIG_WATCHDOG=y` but no DT node on nRF54L15 board | WD disabled on nRF54L15 would change firmware behavior (watchdog is desired) and is out of scope for closeout; DT node is board-level, not repo |

## Hardware in use

Probe identities resolved at runtime via `nrf-probes` — no static serials in docs.

| Role | Board | Console | Notes |
|------|-------|---------|-------|
| LE Audio central | nRF5340DK | none | runs `hci_uart`, attached to PC over J-Link VCOM |
| LE Audio receiver | nRF54L15 (Seeed Xiao) | `/dev/ttyACM0` @ 115200 (serial-mcp) | runs this repo's firmware |
| Logic analyzer | fx2lafw | — | D0=SCK (P1.4), D1=LRCK (P1.5), D2=SDOUT (P1.6) |

- PC-side BT controller: `hci0` = nRF5340DK `hci_uart` on
  **`/dev/ttyACM2`** (J-Link VCOM, USB iface 02 — not ttyACM1/iface-00)
  @ 1 000 000 baud, H4, HW flow control.
- Receiver advertises as "LE Audio Receiver".

## What works (verified)

- Discovery (raw unfiltered scan), connect (raw direct LE Extended Create
  Connection — kernel accept-list connect path is broken on SDC).
- JustWorks pairing, bonded, persists across reboots. Receiver logs
  `Pairing complete, bonded: 1`.
- BAP unicast server negotiation: 2× SelectProperties, 2×
  SetConfiguration, 2× Acquire (Mode A, FL/FR, SDU 120 @ 10 ms, 2M PHY).
- 2× CIS established, data path HCI both directions.
- ISO data TX: **3000 ISO Data TX packets over 15 s** (= 2 streams ×
  1500 frames) with **3024 Number of Completed Packets** events returned.
  No EAGAIN, no stall. `bap_central.py --duration 15` reports
  `Done: 1500 frames in 15.00 s (100.0 fps)`.
- ISO data RX at the receiver: valid SDUs arrive — `stream_recv tally:
  valid=1006 invalid=144` (climbing). BLE transport verified.
- Receiver-side recovery from the post-stream disconnect panic
  (`audio_sink_stop`: PREPARE before DROP in `src/audio_i2s.c`).
- **Clean ACL teardown** in `bap_central.py` (BlueZ Disconnect +
  raw-HCI helper termination) — three consecutive runs with no DK
  reset between them, no zombie-slot exhaustion. `fw-reset-dongle`
  helper exists for recovery from a crashed run that bypassed cleanup.

## What does NOT work / open

### I2S20 hardware evidence

Standalone I2S20 works. The old DAC breakout caused LRCK anomaly.

| Test | Result |
|------|--------|
| GPIO pin map | D0/P1.4 = BCK, D1/P1.5 = LRCK, D2/P1.6 = SDOUT. Confirmed. |
| PCLK32M clock source | Works. `PCLK32M_HFXO` UsageFault tracked separately. |
| Standalone I2S20 tone test | 20.001 s, 2,016 blocks fed, zero EIO/underrun. ENABLE=1, PSEL correct, FRAMESTART firing. |
| Old DAC digital wires connected, MUTE low | D1/LRCK held high — no toggling. Breakout/wiring incompatible or defective. |
| Old DAC digital wires removed | D1/LRCK toggles. GPIO toggling confirmed. |
| Main receiver with new DAC (Phase 4c) | **Technical PASS** — 60,000 frames / 600.00 s, zero disconnect, zero slab-full/underrun/warning/error/fault, clean teardown. See `docs/development/phase4-acceptance-results.md`. |
| External I2S analyzer | **PASS** — 24 MHz fx2lafw capture at DAC pins: BCK 1,525,637.347 Hz, LRCK 47,676.613 Hz, ratio 31.999701, SDOUT active. See `docs/development/phase4-acceptance-results.md`. |
| Phase 4a.2 rate conversion | **PASS** — 35 s stream, 0 slab-full, 0 underrun. Fixed-rate converter matches PCLK32M drain. See `docs/development/phase4-acceptance-results.md`. |

### Next actions (ordered)

 1. ~~**Phase 4b.1** — GRTC-referenced timing foundation~~ → PASS
 2. ~~**Phase 4b.2** — PCLK feedforward + phase PI~~ → PASS
 3. ~~**Phase 4c** — Hardware streaming verification~~ → Technical PASS
 4. ~~**Phase 5** — ASRC quality upgrade~~ → ACCEPTED (2026-07-27).
    Mode A + Mode B 600 s, zero faults. See
    `docs/development/phase5-hardware-acceptance-results.md`.
  5. ~~**Phase 6** — FLPR offload~~ → COMPLETE (2026-07-29). Stages 0–5 accepted.
     FLPR ASRC offload with cpuapp fallback. Stage 5: removed dead identity
     submit API + 1920 B scratch buffer, migrated lifecycle/recovery tests to
     ASRC, 432 unit tests pass, nRF54L15 CPUAPP FLASH 502904 B / RAM 152244 B.
     Hardware: Mode A 120 s + Mode B 120 s at 100 fps, zero faults.
     See `docs/development/phase6-stage5-results.md`.
   6. **BabbleSim** — cross-cutting verification track (research + implementation).
      Provision environment, fix sysbuild/harness, build smallest-useful
      nRF5340bsim dual-core scenario. See `docs/design.md` BabbleSim section.
   7. ~~**BabbleSim Stage 1** — ACCEPTED as regular local gate (2026-07-29).~~
      Production cleanup: startup accounting moved to local sink-stub counters;
      audio_stats.h/.c restored to pre-BSim shape.  Sink-only scenario, strict
      PCM oracle, hash=0xFE0D4245 deterministic across runs.  Client ASE_SRC_COUNT=2
      (min viable per upstream BUILD_ASSERT + stream_tx.c array sizing).  Official
      smoke remains PARTIAL.  Scope stops here: reconnect/Mode A/B/error injection
      duplicate hardware coverage under unmodeled I2S/FLPR.
      See `docs/development/bsim-stage1-results.md`.
   8. ~~**Phase 5 Final Gate (FLPR + cpuapp fallback)**~~ → COMPLETE (Phase 6 Stages 0–5).
      All gates met; 432 unit tests pass; nRF54L15 Mode A/B hardware proven.

### hci_usb firmware cannot do ISO (settled — don't revisit)

Zephyr's USB device_next BT HCI class
(`subsys/usb/device_next/class/bt_hci.c`) has **no ISO data path** — the
isochronous endpoints are descriptor stubs so Linux `btusb` binds (source
comment lines 85–90: "we do not implement isochronous endpoints
handling"). ISO TX (device→host) hits a `default:` case that drops the
packet **and leaks the net_buf**; ISO RX is never armed. The legacy USB BT
class (`subsys/usb/device/class/bluetooth.c`) has no isochronous
endpoints at all either. **No Zephyr USB BT transport can carry LE Audio
ISO in v3.3.0.** hci_uart is the only working transport. Patching hci_usb
for ISO would mean SDK surgery + nRF UDC EP8+ remap — declined.

### btattach not persistent

Runs as a background process from the session. Needs a udev rule /
systemd unit so it survives reboot and re-enumeration.

### LSP `gnu/stubs-32.h` not found warning

The C/C++ language server (clangd/editor) reports `gnu/stubs-32.h`
missing when parsing `src/*.c` and NCS headers — glibc on this system is
64-bit-only and the LSP falls back to the host sysroot instead of the
NCS toolchain's. It is **IDE noise only**; the firmware build
(`fw-build-*`) is unaffected (it uses the NCS toolchain's own sysroot).
Fix later by pointing the LSP/compiler-commands at the NCS toolchain
sysroot (e.g. clangd config with `--sysroot=` from `nix-nrf-dev`, or
generate `compile_commands.json` from the Zephyr build and let clangd
use it). Low priority — does not block builds or flashing.

## Reproduce

### 1. Build + flash the nRF5340DK central (hci_uart)

The dongle config lives in `dongle/hci_uart/{app,netcore}.conf`; the
build helpers build the upstream Zephyr hci_uart sample with those
fragments applied:

```bash
fw-build-dongle      # builds into build/dongle/
fw-flash-dongle      # flashes both cores via the DK's onboard J-Link
```

The netcore conf (`dongle/hci_uart/netcore.conf`) is the load-bearing
part: ISO central, 2 conns / 2 CISes, ext adv, no Coded PHY, no privacy.
See `dongle/README.md` for the full rationale.

### 2. Attach + set up hci0

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 3
sudo btmgmt --index hci0 power on
sudo btmgmt --index hci0 io-cap 3     # NINO — required for JustWorks receiver
sudo btmgmt --index hci0 sc on        # receiver requires SC pairing
# verify: settings should include "powered le secure-conn cis-central"
```

If `btmgmt` reports no adapter, btattach isn't running or the DK
re-enumerated — re-run the btattach line. If the dongle's netcore has
zombie connection slots from a crashed run (`Connection Rejected 0x0d`),
reset with `fw-reset-dongle` then re-attach.

### 3. Stream

```bash
python3 scripts/bap_central.py --duration 15
```

Expected: `ACL link up` → `ServicesResolved` → 2× SelectProperties →
2× SetConfiguration → 2× Acquired → `Streaming 1000 Hz sine` →
`Done: 1500 frames in 15.00 s (100.0 fps)`.

Receiver console (serial-mcp on `/dev/ttyACM0`) during a good run:
`Pairing complete, bonded: 1`, 2× `ASE Config`, `LC3 decoder[0/1]`,
`Stream[x] started`, `audio_i2s: I2S DMA started` — then steady-state
streaming with ASRC correction active; no slab-full or underrun events
in steady state.

### 4. Verify ISO actually crossed HCI (optional)

```bash
setsid sudo btmon -i hci0 -w /tmp/btmon.btsnoop </dev/null >/tmp/btmon.log 2>&1 &
python3 scripts/bap_central.py --duration 15
sudo pkill -f "btmo[n] -i hci0 -w"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "ISO Data TX"
sudo btmon -i hci0 -r /tmp/btmon.btsnoop 2>/dev/null | grep -c "Number of Completed Packets"
# expect: ~3000 ISO Data TX, ~3000+ Number of Completed Packets
```

## Why the BT transport works now (root causes fixed)

| # | Problem | Fix |
|---|---------|-----|
| 1 | hci_usb firmware had no ISO path at all — ISO packets died at the USB layer, no completions ever returned → 3-packet stall | Switched to `hci_uart` (app core is a plain H4 pipe; ISO passes as H4 type 0x05) |
| 2 | Dongle netcore had no ISO / ext-adv / coded-PHY tuning | Netcore conf with `BT_ISO_CENTRAL=y`, `BT_MAX_CONN=2`, `CONN_ISO_STREAMS=2`, `BT_EXT_ADV=y`, `BT_CTLR_PHY_CODED=n`, `BT_CTLR_PRIVACY=n` |
| 3 | Kernel LE connect uses accept-list filtered scan — broken on SDC (zero reports, even for legacy advertisers) | Raw-HCI direct `LE Extended Create Connection` via `scripts/hci_raw_connect.py`, wired into `bap_central.py` |
| 4 | BlueZ demanded MITM; receiver is JustWorks-only (`CONFIG_BT_SMP_ENFORCE_MITM=n`) | NINO agent + `btmgmt io-cap 3` (adapter-level IO cap must also be NINO — kernel uses it for auto-security SMP) |
| 5 | Pairing on a raw-HCI-created connection raced BlueZ ownership and rejected SMP confirmation | Current flow gives BlueZ ownership first, then calls asynchronous `Device.Pair()` before PACS/ASCS access. NINO agent accepts Just Works authorization. |
| 6 | Stale bond on PC vs wiped receiver keys → auth failure loop | Deleted `/var/lib/bluetooth/<adapter>/<receiver>/` bond dir, power-cycled hci0 (`btmgmt power off/on` flushes kernel key store — bluetoothd restart alone does not) |
| 7 | Receiver kernel panic on disconnect after stream (nrfx_i2s ASSERT on de-initialized instance) | `audio_sink_stop()` sends `TRIGGER_PREPARE` before `TRIGGER_DROP` (`src/audio_i2s.c`) |
| 8 | Zombie SDC connection slots on the dongle netcore after repeated raw-HCI connects without clean disconnect (`Connection Rejected 0x0d`) | `bap_central.py` cleanup now calls BlueZ `Device1.Disconnect()` (graceful HCI disconnect) then terminates the raw-HCI helper. Three consecutive runs with no DK reset. `fw-reset-dongle` helper for recovery. |

### Evidence for the hci_usb → hci_uart switch

The original hci_usb dongle carried a vanilla Zephyr hci_usb build
(SW-split LL). Connections hung ~90 s. Reflashing with the SDC netcore
tuning above fixed discovery but ISO data stalled after exactly 3
packets (SDC default ISO TX HCI buffer count): only 3 `ISO Data TX`
crossed HCI, no `Number of Completed Packets` ever returned, BlueZ's
userspace buffer filled (~445 writes ≈ 2.2 s) → EAGAIN. Root cause:
hci_usb has no ISO USB path (see "settled" above). Switching to hci_uart
made ISO flow as ordinary H4 type-0x05 frames — 3000 ISO TX / 3024
completions over 15 s.

### Evidence for the broken accept-list connect path

Linux 7.1 connects via accept-list + passive background scan. On this
SDC/hci_uart combo, **filtered scanning reports nothing** — verified
with raw HCI (bluetoothd stopped, btmon watching):
- unfiltered passive scan: 225 peer reports / 6 s (ext adv, 1M/2M)
- filter=accept-list, AR on: 1 report / 6 s
- filter=accept-list, AR off: 0 reports / 6 s
- same test against a legacy advertiser: also 0
- legacy scan interface (0x200B/0x200C): `Command Disallowed (0x0c)`

So BlueZ Pair/Connect hung; the raw-HCI direct-connect helper
(`scripts/hci_raw_connect.py`) issues `LE Extended Create Connection`
directly and holds the socket open (kernel reaps raw-socket connections
on close).

## Key implementation details (permanent reference)

| Component | File(s) | Note |
|-----------|---------|------|
| Audio pipeline | `audio_sink.h`, `audio_i2s.c`, `audio_decode.c` | Sink interface → I2S DMA (slab allocator), LC3 decode + channel routing |
| Clock recovery | `audio_drift.c`, `audio_drift.h` | PI controller: PCLK feedforward + phase term, ppm output |
| Actuators | `audio_clock_actuator_apll.c`, `audio_clock_actuator_none.c` | APLL (nRF5340) or NONE (nRF54L15, ASRC consumes ppm) |
| ASRC | `audio_asrc.{c,h}` | Fixed-point linear stereo ASRC (cpuapp + FLPR fallback) |
| Offload | `audio_offload.{c,h}`, `flpr_*.{c,h}`, `src/flpr/` | FLPR offload manager + firmware |
| Rate conversion | `audio_rate_convert.c` | Nearest-neighbor, 480→476/477 frames/block for PCLK32M mismatch |
| Timing (nRF54L15) | `audio_timing_nrf54.c` | TIMER20-vs-GRTC PCLK freq measurement, 1 s intervals |
| Central driver | `scripts/bap_central.py`, `scripts/hci_raw_connect.py` | Raw-HCI address bootstrap, BlueZ-owned connection, asynchronous `Device.Pair()`, NINO agent |
| Dongle firmware | `dongle/hci_uart/{app,netcore}.conf`, `scripts/bin/fw-build-dongle`, `scripts/bin/fw-flash-dongle` | nRF5340DK hci_uart central, ISO capable |

## Gotchas to remember

- **`pkill -f <pattern>` kills your own shell** when the pattern appears
  in the command line. Use a bracket: `pkill -f "btmo[n] -i hci0 -w"`,
  or `pkill -x btattach`.
- **UART0 on the nRF5340DK is on ttyACM2 (USB iface 02)**, not ttyACM1.
  ttyACM1 stays silent. Verified by sending HCI Reset manually:
  ttyACM2 replies `04 0e 04 01 03 0c 00`.
- **Raw HCI RX sockets are deaf on this kernel** — observe via
  `btmon -i hci0 -w <file>`, not by reading a raw socket.
- `hcitool lescan` fails with `I/O error` — legacy scan interface not
  supported by this controller build. Not a bug; use `btmon` or
  `bap_central.py` discovery.
- `bluetoothctl scan le` in the background exits instantly and stops
  discovery — useless for scan tests.
- `btmgmt io-cap 3` and `sc on` must be re-applied after adapter power
  loss / USB re-enumeration. Put them next to `btattach` in any
  persistent setup script.
- **Use serial-mcp** for the receiver console (`/dev/ttyACM0`), not
  `stty`/`cat` — serial-mcp holds the port exclusively and survives
  USB disconnects during reset.
- **Zephyr does NOT detect devicetree pinctrl overlaps** — two
  peripherals claiming the same pin produce no compile error. Verify pin
  assignments against all enabled peripherals by decoding the resolved
  `zephyr.dts` (psel encoding: `NRF_PSEL(fun, port, pin)` =
  `(fun << 24) | ((port*32+pin) & 0x1ff)`). AGENTS.md documents a past
  P1.10/P1.11/P1.12 conflict found this way.
