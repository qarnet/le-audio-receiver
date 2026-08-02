# Pre-refactor hardware baseline — T8 evidence (nRF54L15 complete; nRF5340 pending)

Status: **T8 IN PROGRESS — not accepted.**  nRF54L15 Stage 2 matrix run to
completion with one flagged row; nRF5340 Stage 3 not started (hardware
absent).  This document records exact commands, commits, counters, logs,
probe evidence, warning dispositions, and verdicts.

- Date: 2026-08-02 (workstation `thomas-workstation`).
- Branch: `handoff/workstation-transfer`.
- Firmware-under-test commit: **`ace13ff`** (fix: keep validated codec shape
  off the BT RX WQ stack) — the exact flashed build; docs-only parent
  `029bde4`; tooling commits `2988e1c`, `e8dbc1c`, `1d90873`.
- NCS: v3.3.0 (`/home/thomas-workstation/ncs/v3.3.0`).
- Final software gate commit: `1d90873` (scripts-only delta vs `ace13ff`).

## Worktree / commit history

```
1d90873 fix: gate reads status until quiet and diffs lifetime recovery counters
ace13ff  fix: keep validated codec shape off the BT RX WQ stack
e8dbc1c  fix: correct connect dispatch, HCI filter size, and helper liveness gate
2988e1c  fix: retry confirmed raw-HCI connect and gate bap_central on ready line
029bde4  docs: record T7 warning-fix gate evidence on 8f7bfca   (T8 handoff base)
```
Working tree clean at acceptance time except the untracked T8 handoff doc.

## Tooling fixes landed during T8 (all committed, all verified)

1. `2988e1c` — hci_raw_connect.py confirmed-connect retry helper
   (LE Extended Create Connection 0x2043, HCI event parsing, per-attempt
   timeout → LE Create Connection Cancel 0x200E → retry, bounded
   `--connect-deadline`, machine-readable `HCI_CONNECT_READY`/
   `HCI_CONNECT_FAIL` lines); bap_central.py gates on the ready line +
   helper liveness + BlueZ Device1 Connected; 34-test stdlib unittest
   suite wired into test-all.sh (python suites 9 → 10).
2. `e8dbc1c` — four live-test fixes: `_dispatch([begin_attempt()])` list
   wrap, 16-byte `struct hci_filter` pack, bap_central failure-path stdout
   drain, and `poll() is None` liveness wrapping (the `wait_for_helper_ready`
   is_alive contract).
3. `ace13ff` — firmware: `struct codec_shape` replaces the ~18.4 KB
   `struct bt_sink shape` locals on the BT RX WQ stack (see root-cause
   below).
4. `1d90873` — flpr_hang_gate.py: `_read_status()` (read until quiet, fixes
   truncated status capture) and baseline-diff of lifetime recovery counters
   (`recovery_attempts`/`runtime_restarts`/`probation_cleared` are preserved
   across stream sessions on the same boot; `== 1` checks failed on repeat
   runs).

## Root cause: BT RX WQ stack overflow (found by T8, fixed in `ace13ff`)

`src/bt_bap.c` lc3_config() and lc3_enable() declared local
`struct bt_sink shape;` + full `memset` purely to carry five validated
codec scalars out of validate_codec_cfg().  struct bt_sink embeds
`audio_decode_ctx`, which under CONFIG_LIBLC3 embeds two
`lc3_decoder_mem_48k_t` (~4.2 KB each).  The local therefore placed
**18,400 B / 18,344 B** of decoder work memory on the **4,096 B** BT RX WQ
stack; ASCS ase_config → lc3_config overflowed it (Zephyr FATAL ERROR 2)
whenever SetConfiguration arrived on a freshly-paired link.  The large
local was introduced by `779a76c` (2026-08-01), after the last hardware
acceptance (Phase 6, 07-29), so T8 was the first hardware run to expose it.

Observed twice, byte-identical signature (PC 0x00019888 = lc3_config
bt_bap.c:374, LR 0x000438c5 = ase_config ascs.c:1648, thread BT RX WQ,
r0 0x20004f00 / r1 0x200056cc):
- boot-01: external Intel-BT peer pairing attempt.
- modea-06: our controlled central, fresh pairing, 2.5 s after
  `Pairing complete, bonded: 1`.

Fix (`ace13ff`): private five-scalar `struct codec_shape`; validate_codec_cfg
output parameter and both callbacks use it.  All validation, response codes,
mutation order, logs, and lifecycle calls unchanged.  `-fstack-usage`
diagnostic (compiler evidence, not a test):

```
lc3_config  18400 -> 192 B   (dynamic,bounded)
lc3_enable  18344 -> 144 B
```
Evidence files: `/tmp/t8/stack-evidence/bt_bap.su` (before/after), the
compile commands, and probe logs.

Live verification (fresh pairing, controlled central, post-fix): Mode A 120 s
completed — `ASE Config` at the exact conn/ep addresses from the fault
registers, both ASEs configured, streams started, 12000 frames @ 100.0 fps,
`decode_err=0`, zero faults.

## Probe identity evidence (raw, per flash)

`nrf-probes` immediately before each flash:
```
SERIAL 8EE9B3FF  Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  DPIDR 0x6ba02477  PART 0x00054b15  VARIANT AAC0
```
OpenOCD (fw-flash-54l15, each of flashes 01-04):
`CMSIS-DAPv2 VID:PID=0x2886:0x0066 serial=8EE9B3FF`, `SWD DPIDR 0x6ba02477`,
Cortex-M33 r1p0, cpuapp 508,968 B + FLPR 32,332 B downloaded and verified,
`reset run`.  AP IDR map: `nrf-probes` has no verbose option (`--help` shows
only `--find`) — recorded as a tool limitation; DPIDR cross-confirmed in
OpenOCD output.  No static probe→board table is recorded anywhere.

nRF5340/E83 probe: **absent** — `nrf-probes` lists only the Xiao probe;
`/dev/ttyUSB0` does not exist.  Stage 3 not run.

## Central / environment

- Central: nRF5340DK `hci_uart` attached as hci0 on /dev/ttyACM2 @ 1,000,000
  baud H4; BD_ADDR `C0:AA:BB:CC:DD:EE`; settings
  `powered le secure-conn cis-central`.
- External Intel-BT peer `64:49:7D:E3:53:40` (OUI 64:49:7D = Intel
  Corporate, MA-L) present throughout; it attempts a connect every ~2-4 s
  (auth-fail 0x05 cycles once unbonded).  It was the trigger for the
  original overflow (pre-fix) and is the dominant RF-interference source
  (8-14% ISO packet loss; see dispositions).

## Bond handling (authorized actions)

- `bt unpair` on the receiver shell (repo test command) cleared bonds before
  fresh-pairing sessions; exact output `All bonds cleared.` captured
  (`/tmp/t8/unpair-0{1,2,3,4,5,6}.log`).  In-progress pairing deletion
  (`bt_smp: The in-progress pairing has been deleted!`) observed once as the
  direct consequence of unpairing mid-pairing — documented, not a defect.
- Stale workstation BlueZ device record removed once with evidence
  (`bluetoothctl remove DB:A6:0C:05:A2:AA`; full GATT cache DEL dump shows
  the stale PACS/ASCS/VCS records) after a gate run failed with
  `Pair() → AlreadyExists` caused by BlueZ-side paired-flag vs receiver-side
  bond mismatch.
- No mass erase, no recovery, no probe-rs, no persistent-setting changes.

## nRF54L15 Stage 2 matrix

Flashes: `fw-flash-54l15` x4 (logs `/tmp/t8/flash-54l15-0{1,2,3,4}.log`).
Boot acceptance (boot-04 on `ace13ff`): `BLE ready`, `settings_load() OK`,
`VCP ready`, `Audio timing: GRTC+TIMER20+GPPI ready`, `I2S ready (48 kHz
nom, 16-bit, stereo, 16 blocks)`, `FLPR handshake init OK`, `FLPR READY`,
`FLPR READY_ACK sent`, PCM rings 481-frame capacity, `offload init OK`,
`FLPR runtime init OK`, `Advertising as "LE Audio Receiver"`.  Zero
warnings/faults at boot.

| Row | Command | Result | Receiver evidence |
|---|---|---|---|
| Mode A 120 s #1 | `bap_central.py --peer-addr DB:A6:0C:05:A2:AA --duration 120` | **PASS** (exit 0, 12000 frames @ 100.0 fps) | SDUs 10284/10285, decoded 24061 (=SDUs+PLC), plc 3492 (14.3% loss), decode_err 0, stream_reset 4 |
| Mode A 120 s #2 (reconnect, no reset) | same | **PASS** (exit 0, 12000 frames) | SDUs 10284/10285, plc 3492, decode_err 0, stream_reset 3 |
| Mode B 120 s #1 | `--stereo --duration 120` | **PASS** (exit 0, 12000 frames) | SDUs 11001, decoded 24040, plc 2038 (8.5%), decode_err 0, stream_reset 0 |
| Mode B 120 s #2 (reconnect, no reset) | same | **PASS** (exit 0, 12000 frames) | SDUs 11001, decoded 24040, plc 2038, decode_err 0, stream_reset 0 |
| Status evidence | `audio status`, `flpr offload`, `flpr status` after each run (+ injected mid-stream) | captured | audio: Decode errors 0, ASRC linear; offload: submit/success equal, fallback 0, all fault counters 0, recovery 0, probation 0; flpr: Healthy yes, Ready yes, RX dup/lost/missed/ooo 0, Errors 0 |
| Mid-stream drift (Mode A reconnect, +60 s) | injected `audio status` | diagnostic | Drift state ACTIVE, Drift ppm −2000 (saturated at nRF54L15 clamp) |
| Mid-stream perf (Mode B reconnect, +65 s) | injected `audio perf` | diagnostic | Slab free 9/10, Output frames 476/478, Push failures 0 |
| FLPR hang gate Mode B | `flpr_hang_gate.py --duration 180 --stereo --port /dev/ttyACM0 --log …` | **PASS — 16/16 checks, exit 0** | ack, fallback, epoch change, probation cleared, recovery=1, restarts=1, all fault counters 0, frame_count plausible |
| FLPR hang gate Mode A | `flpr_hang_gate.py --duration 180 --port /dev/ttyACM0 --log …` | **15/16 — flagged** (see disposition) | all firmware-recovery checks PASS; only `frame_count_plausible` fails |
| Phase 3 lifecycle | `bluez-wireplumber-phase3-gate.py --receiver "LE Audio Receiver" --serial /dev/ttyACM0 --duration 120 --log-dir /tmp/t8/phase3-54l15 --stage full` | **PASS — exit 0** | 3/3 playbacks, fresh pair, disconnect, bonded reconnect, OpenOCD reset (no erase), bonded reconnect after reset, PACS/ASCS/VCS, WirePlumber sink restored; summaries decode_err 0 / i2s_underrun 0 / stream_reset 0 |

### FLPR hang gate Mode A — flagged row disposition

3 attempts (1 per run; stopped per two-attempt rule), all with every
firmware-side check PASSING: FAULT_HANG ack, cpuapp fallback (>0), epoch
change (restart), probation cleared, recovery attempts=1 (baseline-diffed),
runtime restarts=1, fails=0, relapses=0, exhaustion=0, all integrity fault
counters 0, state ACTIVE, resumed success.  The sole failing check,
`frame_count_plausible`, expects `|submit − duration×100| ≤ 100`; observed
submit 16599-16832 vs 18000 (shortfall ≈ 1200-1400 frames ≈ 12-14 s).
Cause: 8-14% RF packet loss on the current link interacts with the Mode A
TS-based half-pairing — a lost SDU on one CIS leaves its mate unpaired and
it is discarded, so the decode/submit path permanently loses ~7% of frames.
Mode B (single CIS, no pairing) passes the same check (submit within
tolerance).  BSim T4 (lossless simulated link) passes Mode A strictly
(pinned hashes), proving the pairing/decode path is correct.  The gate was
validated in its Phase-6 era on a near-lossless RF link (no peer present);
today's loss is environmental (external Intel-BT radio).  No firmware fault
identified; no test weakened.  Disposition: row flagged, needs orchestrator
decision (accept-with-documentation vs run in clean RF vs gate tolerance
review).

### I2S underruns (Mode A only) — disposition

Mode A runs: 4 and 3 `i2s_nrfx: Next buffers not supplied on time` +
`audio_i2s: I2S underrun, restarting DMA` events (auto-recovered via
TRIGGER_PREPARE per documented policy).  Mode B runs: 0.  Underrun offsets
scattered (not fixed-period), consistent with loss-burst starvation from the
14.3% RF loss at 200 ISO pkt/s (Mode B 8.5% loss at 100 pkt/s → no
starvation).  Accepted Phase 4/6 baselines recorded zero underruns on a
clean link.  Not a decode/integrity fault (decode_err 0, push failures 0,
no assert); drift controller saturated at −2000 ppm matches the documented
PCLK range (+1500..+1837 ppm feedforward, Phase 4b.2/5 evidence).
Disposition: environmental (RF loss × dual-CIS), auto-recovered; flagged
for the same orchestrator review.

## Warning scan (all Stage-2 logs)

Zero `FATAL` / `USAGE FAULT` / `Assert` / overflow post-`ace13ff`.  All
`<wrn>`/`<err>` lines classified:
- `flpr_ring: RING_RESET_ACK timeout` + `offload recovery: short ring reset
  failed (-116), escalating to runtime restart` — expected FAULT_HANG
  recovery path; runtime restart succeeded (documented policy).
- `bt_conn: conn … failed to establish. RF noise?` — transient RF connect
  failure from the external peer.
- `I2S underrun, restarting DMA` — Mode A loss-burst starvation, auto
  recovery (see disposition).
- `bt_smp: The in-progress pairing has been deleted!` — direct consequence
  of the authorized `bt unpair` mid-pairing.
- Peer `reason 0x05` auth-fail cycles — normal rejection of the unbonded
  external peer.

## Software gate on final commit `1d90873`

```
./scripts/test-all.sh              → Gate complete: 42 PASS / 0 FAIL / 42 TOTAL, exit 0
./scripts/test-coverage.sh --output /tmp/t8/coverage-final → baseline enforcement PASS,
                                   lines 3070/3503 (87.6%), branches 1332/1921 (69.3%),
                                   functions 182/182 (100%) — identical to committed baseline
fw-build-5340 / fw-build-54l15 / fw-build-dongle → all exit 0
python3 scripts/check-build-contract.py → 74 assertions, 0 failed, BUILD CONTRACT PASSED
git diff --check                    → clean
```
Kconfig assigned-value warnings: 0.  `native_sim` entropy notices: 29
(documented informational).  The canonical gate was run twice on
`ace13ff`/`1d90873` (42/42 both times); one intermediate run failed on disk
exhaustion (coverage rebuild + BSim logs on a 100%-full filesystem) — root
cause: 3.9 GB ccache + stale gate temp dirs; purged caches, rerun clean.

## Log inventory (preserved)

`/tmp/t8/`: flash-54l15-01..04.log, 54l15-boot-01..04.log, 54l15-modea-07.log,
54l15-modeb-01.log, 54l15-reconn-modea-01.log, 54l15-reconn-modeb-01.log,
modea-01..07-central.log, modeb-01-central.log, reconn-*-central.log,
flpr-hang-mode-a.log, flpr-hang-mode-b.log, phase3-54l15/ (3 playback logs +
gate_test.wav), status-*.log, unpair-0*.log, gate-tooling.log,
gate-tooling-2.log, gate-fix-ace13ff.log, gate-final-1d90873.log,
gate-final-1d90873b.log, gate-cov-1d90873.log, stack-evidence/, nrf-probes-*.txt.

## Remaining work / blockers

1. FLPR hang gate Mode A `frame_count_plausible` + Mode A underruns —
   orchestrator disposition requested (RF-loss-related; no firmware fault).
2. nRF5340 Stage 3 — hardware absent (no E83 probe, no /dev/ttyUSB0).
   Evidence for the nRF54L15 target is complete and preserved.
3. T8 acceptance and final STATUS.md/plan/transfer-status updates withheld
   until the flagged row is disposed and Stage 3 runs.

## Matrix verdict summary

- nRF54L15: builds/flash/boot PASS; Mode A ×2 PASS (underrun flag);
  Mode B ×2 PASS; reconnects PASS; statuses captured; FLPR hang Mode B PASS;
  FLPR hang Mode A 15/16 (flag); Phase 3 PASS; zero firmware faults.
- nRF5340: NOT RUN (hardware absent).
- T8: **open**.
