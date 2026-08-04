# Refactor R1 results — ownership and concurrency hardening

Status: **ACCEPTED (2026-08-04)**.

## Exact commits

- Implementation (tests + source): **`bcd623b`** — `fix: R1 ownership and
  concurrency hardening (sink drain, lifecycle lock, FLPR ACK correlation,
  timing mutex)` on branch `handoff/workstation-transfer`.
- Contracts/metadata: **`4f426f3`** — `docs: record R1 contracts, matrix
  witnesses, and handoff`.
- Start: clean R0 evidence HEAD `f654b58` (untracked handoff preflighted, GO).

G1 was run on the clean exact implementation hash **`4f426f3`**
(`4f426f3d5fc0f518ae5b290cfd7d3cc583fbfc7c`), worktree clean.

## What changed

- Sink: `audio_sink_stream_open()`/`audio_sink_stream_close()` admission
  gate; `audio_sink_push()` admits under one short `k_mutex` with a local
  input-frame snapshot and one common exit (active-push decrement +
  broadcast); `audio_sink_stop()` closes admission, drains every admitted
  push (no timeout-and-proceed), finalizes once per overlapping caller
  cohort with PREPARE-before-DROP; sequential repeated stops keep current
  reset semantics with no extra triggers.
- BAP lifecycle: `lifecycle_lock` + audio-path transition generation;
  `stream_started()` opens sink admission under lifecycle→sink lock order
  and rechecks generation after outside-lock open work; centralized close
  closes gate + admission in one transition; shell `audio stop` routes
  through `bt_bap_audio_path_stop()` (force-close → drain → offload stop);
  `stream_lifecycle_force_close()` latches the gate for the configured slot
  set.
- nRF54 timing: one control mutex serializes `sdu_ref_update` anchor/compare
  commit against `reset`; GRTC ISR captures generation once at entry.
- FLPR: `ring_data_lock` serializes bulk ring ops against reset/reinit;
  reset/stall ACKs correlate by a monotonically incremented nonzero 16-bit
  request token (`flpr_control_ack_make`, FLPR main echoes request seq,
  `-EOVERFLOW` past 0xFFFF until `remote_restarted()`); handshake validates
  and snapshots under `flpr_lock`.

## Test-first (red recorded)

The concurrency tests were written against the pre-fix production surface:
`test_sink_concurrent.c` and the force-close/ACK/timing tests reference
`audio_sink_stream_open()`/`audio_sink_stream_close()`/
`stream_lifecycle_force_close()`/`flpr_control_ack_make()` which do not exist
at `f654b58` — the natural pre-fix red is a compile/link failure of the
closed-admission API (the exact red the handoff anticipated).  Implementation
followed; no failing state was committed.

## Pre-fix vs post-fix focused behavior

Pre-fix (at `f654b58`, no R1 tests): sink had no admission gate, stop raced
in-flight pushes, lifecycle had no shell force-close latch, FLPR reset/stall
ACKs were uncorrelated (`seq=0`), timing update/reset were unsynchronized.

Post-fix focused suites (all green, zero warnings):

| Suite | Result |
|---|---|
| `audio_i2s` (ASRC) | 117 PASS, 0 FAIL |
| `audio_i2s_identity` | 113 PASS, 0 FAIL |
| `audio_shell` | 13 PASS, 0 FAIL |
| `audio_shell_noperf` | 10 PASS, 0 FAIL |
| `audio_shell_nrf54` | 42 PASS, 0 FAIL |
| `lifecycle` | 29 PASS, 0 FAIL |
| `timing_nrf54` | 21 PASS, 0 FAIL |
| `flpr_ring_mgr` | 65 PASS, 0 FAIL |
| `flpr_handshake` | 45 PASS, 0 FAIL |
| `flpr_protocol` | 62 PASS, 0 FAIL |

New deterministic concurrency coverage: fake-driver write gate (entered/
release semaphores) pins stop-drains-push, two-stop single finalization,
closed rejection `-EBUSY` + reconnect, failure-exit admission release, open
waits for the full stop cohort, close nonblocking; lifecycle forced-close
latch matrix; timing control-mutex barrier + first-compare-failure unlock;
ring-manager data-lock barrier, epoch-0 produce rejection, ACK late-ack-by-
sequence retry, 16-bit token boundary (reset and stall); protocol ACK
constructor; handshake validation counters under concurrent status reads.

## G1 — canonical gate on clean `4f426f3`

```bash
./scripts/test-all.sh
  -> Gate complete: 47 PASS / 0 FAIL / 47 TOTAL, exit 0
     (28 twister + 4 exec + 12 python + coverage + matrix + bsim:stage1)
./scripts/test-coverage.sh --output /tmp/r1-coverage --clean-output
  -> exit 0 (default baseline enforcement; clean worktree)
fw-build-5340 / fw-build-54l15 / fw-build-dongle  -> PASS, zero compiler warnings
python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
  -> 76 assertions, 0 failed; BUILD CONTRACT PASSED
git diff --check -> clean
```

Coverage (clean-hash enforcement run, gcovr 8.4 / gcov (GCC) 14.3.0,
population **26 files**, no per-file/aggregate ratio regression):

| Metric | Covered/Total | Percent |
|--------|---------------|---------|
| numeric lines | 3514/3959 | 88.8% |
| numeric branches | 1475/2079 | 70.9% |
| numeric functions | 214/214 | 100.0% |

Per-file line ratios all >= baseline (audio_i2s 249/255, audio_shell
302/519, audio_timing_nrf54 142/143, flpr_handshake 389/401,
flpr_ring_mgr 688/735, stream_lifecycle 57/57).  No baseline rewrite.

BSim Stage 1 (16-scenario T4 matrix, first nine twice): **PASS** — hashes,
totals, observer and segment counts byte-identical (the runner enforces the
committed expected values; any change would have failed the child).

Warning disposition: every build diagnostic matched the documented
non-actionable NCS v3.3.0 set (STATUS.md "Build warning diagnostics" +
R0 warning scan): nRF5340 `PARTITION_MANAGER`/sysbuild deprecations,
`BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice gap, experimental
`BT_LL_SW_SPLIT`/`BT_CTLR_SET_HOST_FEATURE`/`BT_CTLR_PERIPHERAL_ISO`,
`__ASSERT()` informational, `CONFIG_BT_CTLR_ADVANCED_FEATURES=y` notice
(pre-existing, R0-recorded); nRF54L15 FLPR `UART_CONSOLE` assigned-value,
`simple_bus_reg`/`avoid_unnecessary_addr_size` DT warnings,
`drivers__watchdog` "No SOURCES given"; dongle partition-manager
deprecations.  Zero actionable/new warnings.

## G2 — autonomous hardware smoke (both targets)

Central: repository nRF5340DK `hci_uart` dongle, attached at session start,
`btmgmt --index hci0 info` → `addr C0:AA:BB:CC:DD:EE`, `current settings:
powered le secure-conn cis-central`.  No human-operated central; all streams
via `scripts/bap_central.py`.

Probe identity (runtime, `nrf-probes` — no static mapping):
- nRF54L15 (Seeed Xiao): serial `8EE9B3FF`, DPIDR `0x6ba02477`, PART
  `0x00054b15`, VARIANT AAC0.
- nRF5340 (Ebyte E83): serial `E6635C08CB1F502B`, DPIDR `0x6ba02477`, PART
  `0x00005340`, VARIANT QKAA.

Receiver identities (boot logs): Xiao `DB:A6:0C:05:A2:AA` (random); E83
`E8:54:F0:E0:D9:42` (public).

### nRF54L15 (console /dev/ttyACM0 @ 115200)

Mode A 30 s (`bap_central.py --peer-addr DB:A6:0C:05:A2:AA --duration 30`,
EXIT 0, 3000 frames):
`Connected: C0:AA:BB:CC:DD:EE`, `Pairing accepted`/`bonded: 1`, two mono
ASEs configured, `Stream[1]/[0] started`, `Audio path gate OPEN (stream[0]
completed the set)`, `offload stream start: gen=71 state=PREPARING`,
`Coordinated reset: proposing epoch=783641216 to FLPR` →
`Coordinated ring reset: epoch=783641216`, `offload prep OK:
epoch=783641216 gen=72 state=ACTIVE` (FLPR healthy, stream offloaded),
`I2S DMA started`, `Timing anchor` + `PCLK timer diag[1..25]` (~2000 ppm
feedforward), `Audio path gate CLOSED`, `Stream[0] summary: SDUs=2834
decoded=6054 plc=386 decode_err=0 i2s_underrun=0 stream_reset=0`,
`Stream[1] summary: SDUs=2851 ... decode_err=0`, clean stop + disconnect +
advertising restart.

Mode B 30 s (`--stereo --duration 30`, EXIT 0, 3000 frames):
`ASE[0] configured chan_count=2` (single stereo ASE), `LC3 decoder ch=2`,
`Audio path gate OPEN`, `Coordinated reset: proposing epoch=1000489913`,
`offload prep OK: gen=85 state=ACTIVE`, `I2S DMA started`, PCLK diag
(~3400 ppm), `Stream[0] summary: SDUs=2919 decoded=6054 plc=216
decode_err=0 i2s_underrun=0 stream_reset=0`, clean stop/disconnect.

### nRF5340/E83 (console /dev/ttyUSB0 @ 115200)

Mode A 30 s (`--peer-addr E8:54:F0:E0:D9:42 --duration 30`, EXIT 0, 3000
frames): gate OPEN, `I2S DMA started`, `Stream[0] summary: SDUs=2833
decoded=5666 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0`, clean
close/disconnect.  APLL path stable.

Mode B 30 s (`--stereo --duration 30`, EXIT 0, 3000 frames): gate OPEN,
`I2S DMA started`, `Stream[0] summary: SDUs=2922 decoded=5844 plc=0
decode_err=0 i2s_underrun=0 stream_reset=0`, clean close/disconnect.

Required behavior observed on both targets: expected boot/advertising,
nonzero SDUs/decoded frames, I2S DMA start, clean stop/reconnect, zero
compiler/Kconfig/boot warnings, zero assertions, zero decode errors, zero
I2S underruns, zero stream resets, zero offload integrity faults, zero slab
errors, zero deadlocks, no unexplained fallback.  nRF54 FLPR healthy stream
remained offloaded (ACTIVE) in both runs; E83 APLL path stable.

Lab-environment notes (recorded, not faults): a second "LE Audio Receiver"
on the bench (the E83) initially captured the central's discovery-based
Mode A; the run was repeated against the intended receiver via `--peer-addr`
with the receiver's boot-log identity.  An unknown bench device
(`64:49:7D:E3:53:40`) repeatedly attempted connects to the Xiao with
authentication failures; it did not disturb the two accepted Xiao stream
runs.  The Xiao/E83 enter BONDED_ONLY after bonding with the dongle; each
target's pairing was reset (`bt unpair`, receiver shell) before its runs, and
the central-side device cache was cleared (`bluetoothctl remove`) when a
stale bond blocked fresh pairing.  These are pre-existing lab/BlueZ
behaviors, not R1 regressions.

### Raw logs (preserved under /tmp)

- `/tmp/r1-g2-5340-console.log` — E83 console: boot + Mode A + Mode B
- `/tmp/r1-g2-54l15-console.log` — Xiao console: boot
- `/tmp/r1-g2-54l15-modeA4.log`, `/tmp/r1-g2-54l15-modeB2.log` — central,
  Xiao Mode A/B
- `/tmp/r1-g2-5340-modeA2.log`, `/tmp/r1-g2-5340-modeB.log` — central,
  E83 Mode A/B
- `/tmp/r1-g1-testall.log`, `/tmp/r1-g1-coverage.log`, `/tmp/r1-g1-contract.log`,
  `/tmp/r1-g1-b5340.log`, `/tmp/r1-g1-b54l15.log`, `/tmp/r1-g1-bdongle.log` —
  G1 evidence

## Coverage matrix / matrix / contracts

Focused coverage report-only runs (pre-commit and clean-hash enforcement)
show no population drift (26 files) and no per-file/aggregate ratio
regression.  `tests/test-matrix.json` gained the R1 outcome ledger entries
and transitions; `docs/testing/behavior-contract.md` gained I2S-011 and the
R1 LIFE/I2S wording; `docs/testing/coverage-matrix.md` test descriptions/
counts updated to observed values.

## Final worktree status

Clean at `4f426f3` (after the evidence commit below).

## Deviations / blockers

None.  (Two lab-environment detours — competing receiver on discovery, and
stale-bond pairing — were resolved with the documented central-side and
receiver-side pairing resets; both are pre-existing lab behaviors and are
recorded above, not R1 defects.)
