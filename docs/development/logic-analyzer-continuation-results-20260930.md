# All-nRF54L15 analyzer continuation, 2026-09-30

## Scope and recovered checkpoint

Continuation of PB-035 physical diagnostics with passive I2S observation. No
firmware source, SDK, frozen row, transport threshold, or product acceptance
criterion changed. PB-041 remains Backlog; nonce-based fixture identification
was not started. No nRF5340 hardware, HCI role transition, publication, commit,
or PR was performed. Preserve pending PB-013 and other user edits.

Repository HEAD is `2a0e7927b9cd5041aa4b348685c1fa88abd00b5d` on
`feature/nrf54l15-only-continuation`. Earlier handoff is behind retained external
evidence, recovered rather than rerun in this session:

- `/tmp/opencode/nrf54-2a0e-clean-gate-r1/verification-summary.json`: clean
  canonical **80 PASS / 0 FAIL / 80 TOTAL**, HIL Python **340 passed / one
  intentional hardware-opt-in skip**, unchanged 36-file coverage baseline.
- Same root's build review and contract record: physical builds and **73/73**
  resolved contract. Receiver experimental TX-notify warning remains its exact
  reviewed target-specific condition, not a general warning waiver.
- `/tmp/opencode/nrf54-2a0e-hci-clean-r1/phase1-summary.json`: six HCI cases
  passed, 72,000 writer frames, PLC 685, no recorded SMP/hardware/opcode alerts.
  This supersedes the old statement that clean HCI qualification was unrun;
  it does not prove generic UART reliability or public release acceptance.
- `/tmp/opencode/nrf54-2a0e-artifact-matrix-20260928-r1/first-failure-summary.json`:
  full exact-local-artifact matrix attempted **two rows: one passed, one failed,
  18 skipped**. Failure is fresh Mode A `48_4_1`, source `first_errno=-62`,
  stream submissions 16/15, second-stream under-lead count one. No cleanup
  failures. Original failed matrix remains failed and immutable.

## Safety, identity, and images

User reconfirmed unchanged setup wiring, common ground, analyzer input ratings
including CH3 rail, and **downstream audio disconnected** before new streaming.
Current wiring remains solely in `docs/testing/logic-analyzer-setup.md`.
No wiring changes, GPIO challenges, or rail driving occurred.

Fresh `nix-nrf probes` found two nRF54L15 candidates. Bound runner performed
six ordered identity revalidations and opened consoles before flashing.
Raw `identity.json`, role-specific CMSIS-DAP files, USB/udev evidence, and
`session-revalidations.jsonl` reside in the new HIL root below. Both candidates
read DP `0x6ba02477`, AP0/1 `0x84770001`, AP2 `0x32880000`, AP3
`0x00000000`, FICR PART `0x00054b15`, VARIANT `0x41414330`. These common
fingerprints do not alone assign roles; bound USB/probe identities do. No
static probe-to-role table or reusable tty/USB address is declared here.

Used unchanged clean `2a0e792` local archives through real artifact resolver
and Runner interfaces, not primary dirty-tree builds:

| Input | SHA-256 |
| --- | --- |
| Receiver archive | `95402a1e40a7c0ad2121b2dbd199ede5287dc5f1fdf8742c0af87eda8dc8c40e` |
| Source archive | `72ff4cbd9ce08fc14f71c3d4b3cd17a9d2766a483c8f87926f692b4b21050111` |
| Receiver CPUAPP HEX | `69fe8bf8e0de4b24659afc9d168b67a01b45a82caa072b52fce54b1592df93c5` |
| Receiver FLPR HEX | `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a` |
| Standalone source CPUAPP HEX | `805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c` |

Archive hashes before and after match. Clean validation checkout stayed at
the exact commit; it was verification-only, not an implementation location.

## Physical row and passive windows

Command executed: `python3 /tmp/opencode/nrf54-analyzer-resume-20260930-r2.py`.
Run-specific supervisor and wrapper are external evidence, not reusable commands:
new attempts require unique paths and fresh identity. System-manager service
used ordinary user, `RuntimeMaxSec=600`, `TimeoutStopSec=60`, and
`KillMode=control-group`. Raw argv, console and UTC bounds are retained.

- Supervisor: `/tmp/opencode/nrf54-analyzer-resume-20260930-r2/`.
- HIL: `/tmp/opencode/hil-runs/nrf54-analyzer-resume-20260930-r2/`.
- Frozen row: `rh3.fresh_mode_a_48_4_1`, 120 s scored, **PASS**, no cleanup
  failures. Each CIS scored 12,000 and submitted/completed 12,644 SDUs.
- Source send failures, skips and under-lead counts all zero. Observed leads
  2867..2909 us / 2483..2661 us. These are software pre-send samples, not
  controller arrival or RF-transmission times.
- Receiver valid 12,645 / 12,644; global decoded 25,314, PLC 25. Second slot
  decoded/PLC zero values are shared/global-counter ownership, not no-PLC proof.
  Decode errors, I2S underruns, push failures, resets and offload fallback zero.
- Cleanup source `idle` response is retained in `source-records.jsonl`.
  Service became inactive/dead with empty control group; no OpenOCD or
  btattach processes remained at post-run check.

Known analyzer `0925:3881` was discovered through current USB sysfs; explicit
FX2 connection scan/show used only that candidate. Refreshed identity after
scan. Eight-channel profile, physical names D0-D3, nominal 12 MS/s,
1,200,000 samples per window. No acquisition stderr errors. USB path is
session-local; generic serial is not unique.

| Capture | SHA-256 | Observed content after boundary alignment |
| --- | --- | --- |
| `i2s-0.sr` | `2752e65ea4f5b70ce5031910173878a832ea35fb4f18d9e5d3b2422f3fe7e2ac` | 4768 L/R pairs, all zero; early startup/preamble window |
| `i2s-1.sr` | `edf0013f6ac8808ffca348abed95c354e8dd64fa70797924e289796f25bf1a8d` | 4766 L/R pairs, 4765 different; nonzero left/right data |

Each complete raw halfword has 16 BCLK rising edges. First/last partial
words excluded; all raw complete-word geometry checked **before** making
an aligned analysis copy. Second raw decoder reported exactly
`90-217 i2s-1: Received 16-bit word, expected 10-bit word` from mid-word
capture start. Original capture and warning retained. Aligned decoder has
zero warnings; no generic warning filtering. CH3 digitally high throughout,
not voltage/power-good or DAC-presence evidence. Two windows total **0.2 s**,
not continuous 120 s wire proof. No WAV export or calibrated ppm claim.

## Offline negative controls and test boundary

Executed `python3 /tmp/opencode/nrf54-analyzer-analysis-20260930-r1.py` using
unchanged external geometry analyzer and installed real sigrok I2S decoder.
Results and analyzer snapshot:
`/tmp/opencode/nrf54-analyzer-analysis-20260930-r1/result.json`.

Both original archives pass complete-word observation. Four independently
saved altered `.sr` inputs fail through the same archive-analysis boundary:

1. Remove one interior BCLK high pulse: one right halfword has 15 edges.
2. Remove one raw sample: exact capture-length check rejects truncation.
3. Replace all observed wires with flat values: no WS/complete words.
4. Swap BCLK and WS bits: invalid halfword geometry.

Original `.sr` hashes verified unchanged after controls. These checks make
diagnostic interpretation stronger without substituting mocks or modifying
raw evidence. **Silence correctly passes geometry**; nonzero content needs a
separate verdict. The checker does not prove exact post-ASRC PCM, detect valid
content substitution/repetition, or reject a structurally valid stale replay.
Fresh wire nonce requires PB-041, still unimplemented.

Initial supervisor r1 failed host API preflight before Runner construction:
artifact resolver requires absolute **string** arguments, not `Path` objects.
No board reset/flash/stream occurred in r1. Preserve its error logs. r2 wrapper
converts those same validated archive paths to strings; real resolver and
Runner remain intact, no patched product source or mocked hardware boundary.

## Timing investigation and next scope

The unchanged Mode A repeat passing does **not** repair the prior `-ETIME`.
No blind full matrix rerun or claim that analyzer fixed timing follows it.
Installed v3.4.1 source exposes a concrete scheduling hypothesis:

- `hil/source/app/src/hil_source_app.c:654-756`: stream 0 commits before
  second-stream 2000-us lead check. Target is 3000 us; actual batch may start
  as late as 2000 us. `tx_batch_mutex` excludes status work, not Bluetooth work.
- `hil/source/app/src/hil_source_tx.c:110-117`: allocation is `K_NO_WAIT`, but
  `bt_bap_stream_send_ts()` enqueues and wakes TX work.
- SDK `zephyr/subsys/bluetooth/host/iso.c:892-906`, `host/conn.c:874-905`,
  `host/hci_core.c:5209-5215`, `kernel/work.c:393-406`: enqueue can reschedule
  cooperative TX processor before preemptible fixture worker resumes.
- Source worker is preemptible priority 4 (`hil_source_app.c:2307-2310`);
  resolved source TX processor is cooperative -1. SDK driver also takes
  shared MPSL mutex before controller ISO put (`nrf/subsys/bluetooth/controller/
  hci_driver.c:423-440`).
- SDK `nrfxlib/softdevice_controller/include/sdc_hci.h:79-83` requires
  **1000 us at controller arrival**, distinct from fixture's 2000-us public
  API-entry guard. Current I2S taps cannot timestamp source controller arrival.

Recommendation: bounded RAM first-fault timing records around final batch gate,
send0 entry/return, accounting locks and stream1 check, then actual controller
arrival when necessary. Drain outside measured path; no per-SDU UART formatting.
Keep trace/no-trace image identities and account for perturbation. Evidence must
distinguish reschedule, mutex, interrupt and driver costs before changing fixture
behavior. Earlier submission and prepared-buffer batch admission are candidates,
not implemented or accepted fixes. Never lower minimum lead, retimestamp only
one peer, switch to unsynchronized arrival mode, or weaken frozen matrix.

Full migration remains pending: fresh full 20-row exact-artifact matrix, remaining
PB-035/PB-036 evidence, aggregate PB-038 qualification, separate draft-artifact
FR4 and analog acceptance. PB-041 needs refinement/readiness before new diagnostic
protocol implementation. Historical documentation/tests may retain nRF5340 facts;
they are not an active hardware dependency or permission to erase history.
