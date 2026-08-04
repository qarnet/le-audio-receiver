# Pre-refactor hardware baseline — T8 evidence (ACCEPTED)

Status: **T8 ACCEPTED (2026-08-04).**  Final record — supersedes the earlier
in-progress draft (2026-08-02).  Both hardware matrices (nRF54L15 and
nRF5340/E83) pass on the exact final production code; every required stream
row has direct evidence; the only evidence limitation (per-CIS ISO
sequence-gap activation, see below) is documented explicitly and does not
block acceptance.

- Date: 2026-08-04 (workstation `thomas-workstation`).
- Branch: `handoff/workstation-transfer`.
- Exact accepted production code commit: **`971e6a4`** — "fix: conceal
  per-CIS ISO sequence gaps from omitted callbacks" (the flashed build on
  both targets).
- Coverage-baseline commit: `1a5842d` (refresh for `audio_iso_seq`, generated
  on clean `971e6a4`).
- Final docs commit (this record's HEAD): `3c29421`.
- NCS: v3.3.0 (`/home/thomas-workstation/ncs/v3.3.0`).
- Final software gate: **47 PASS / 0 FAIL / 47 TOTAL** on the exact final
  code (see "Final software gate" — exact runtime of the final run is not
  retained; closest retained full-gate log is 46/46 on `ac1fa06`).

## Worktree / commit history

Full T8 chain from the T7 base `029bde4` (oldest → newest):

```
029bde4  docs: record T7 warning-fix gate evidence on 8f7bfca    (T8 handoff base)
2988e1c  fix: retry confirmed raw-HCI connect and gate bap_central on ready line
e8dbc1c  fix: correct connect dispatch, HCI filter size, and helper liveness gate
ace13ff  fix: keep validated codec shape off the BT RX WQ stack   (stack overflow fix)
1d90873  fix: gate reads status until quiet and diffs lifetime recovery counters
3214ce4  docs: record T8 nRF54L15 baseline evidence; T8 in progress   (first draft)
8fd7bb0  feat: bonded-only controller filter with production pairing reset
6578a9c  chore: refresh coverage baseline for pairing-policy module
19bec75  fix: defer pairing-reset restart while a link is tearing down
46100a9  fix: preserve-bond reconnect via BlueZ Connect, no raw-HCI helper
a40f75e  fix: snapshot pairing-policy state, serialize reset under adv lock
4488f53  fix: fresh BlueZ reconnect when device already connected
c056936  fix: hang gate reads last status block, not first
7c1205b  fix: Mode A assembler with per-channel PLC for missing CIS SDUs
9b78d87  fix: keep central writer feeding during BAP teardown
ac1fa06  coverage: refresh baseline for Mode A assembler module
1a4d27f  fix: snapshot writer transports; pin hang-gate receiver address
4ef25b2  fix: hang-gate baseline poll reads status until quiet
3df6da8  fix: hang-gate baseline treats absent Runtime line as zero
971e6a4  fix: conceal per-CIS ISO sequence gaps from omitted callbacks   (FINAL CODE)
1a5842d  coverage: refresh baseline for audio_iso_seq module
3c29421  docs: record baseline refresh and iso_seq suite in coverage matrix   (HEAD)
```

Working tree clean at acceptance time (HEAD `3c29421`).

## Tooling and code fixes landed during T8 (all committed, all verified)

1. `2988e1c` — `hci_raw_connect.py` confirmed-connect retry helper
   (LE Extended Create Connection 0x2043, HCI event parsing, per-attempt
   timeout → LE Create Connection Cancel 0x200E → retry, bounded
   `--connect-deadline`, machine-readable `HCI_CONNECT_READY`/
   `HCI_CONNECT_FAIL` lines); `bap_central.py` gates on the ready line +
   helper liveness + BlueZ Device1 Connected.
2. `e8dbc1c` — four live-test fixes: `_dispatch([begin_attempt()])` list
   wrap, 16-byte `struct hci_filter` pack, `bap_central` failure-path stdout
   drain, `poll() is None` liveness wrapping.
3. `ace13ff` — firmware stack-overflow fix (root cause below).
4. `1d90873` — `flpr_hang_gate.py` `_read_status()` (read until quiet) and
   baseline-diff of lifetime recovery counters
   (`recovery_attempts`/`runtime_restarts`/`probation_cleared` persist
   across stream sessions on the same boot; `== 1` checks failed on repeat
   runs).  `c056936` (read last status block, not first), `4ef25b2` (poll
   status until quiet) and `3df6da8` (absent Runtime line treated as zero)
   complete the hang-gate baseline hardening.
5. `8fd7bb0` — production pairing filter: bonded-only controller filter with
   production pairing reset (see "Pairing filter phase").
6. `19bec75` / `46100a9` / `a40f75e` / `4488f53` — BlueZ preserve-bond and
   reconnect fixes (see "BlueZ preserve-bond fixes").
7. `7c1205b` — Mode A assembler with per-channel PLC for missing CIS SDUs
   (production `src/audio_modea.c` + `modea` suite).
8. `9b78d87` — central writer keeps feeding during BAP teardown (removes the
   teardown-boundary `i2s_nrfx: Next buffers not supplied on time` seen on
   the Aug 3 intermediate E83 run; see superseded rows).
9. `971e6a4` — per-CIS ISO sequence-gap concealment (production
   `src/audio_iso_seq.{c,h}` + 18-test `iso_seq` suite); see the evidence
   limitation below.

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
compile commands, and probe logs.  Live verification (fresh pairing,
controlled central, post-fix): Mode A 120 s completed — `ASE Config` at the
exact conn/ep addresses from the fault registers, both ASEs configured,
streams started, 12000 frames @ 100.0 fps, `decode_err=0`, zero faults.

## Pairing filter phase (2026-08-03, `8fd7bb0` + chain)

Production pairing-policy module `src/bt_pairing_policy.{c,h}` with the
bonded-only controller filter:

- When any bond exists the policy is `BONDED_ONLY`: the controller filter
  accept list (FAL, `CONFIG_BT_FILTER_ACCEPT_LIST=y`, see build contract
  `[54l15-034]`) is rebuilt at each advertising restart from the bonded
  entries only (`bt_bap.c` pairing-policy integration).  Unbonded peers are
  not on the FAL, so the controller filters their connect requests.
- Defense in depth: the `pairing_accept` callback rejects any unbonded peer
  with `LOG_WRN("Pairing rejected (BONDED_ONLY): unbonded peer …")` +
  `BT_SECURITY_ERR_PAIR_NOT_ALLOWED` (no HCI from the callback; pure policy).
- `bt unpair` on the shell now runs the production pairing reset
  (`Pairing mode reset: bonds cleared; open pairing enabled.`) → OPEN mode
  (no FAL) for fresh-pairing sessions; `a40f75e` snapshots the policy state
  and serializes the reset under the advertising lock; `19bec75` defers the
  pairing-reset restart while a link is tearing down.
- Unbonded Intel-BT peer `64:49:7D:E3:53:40` (OUI Intel Corporate MA-L):
  pre-filter sessions show it connecting and cycling `reason 0x05` auth-fail
  disconnects every ~2–4 s (e.g. `54l15-modea-06.log`, Aug 2).  In bonded
  (`BONDED_ONLY`) sessions its connect attempts are absent from the receiver
  console — the preserve-bond run evidence notes "no Intel peer connect line
  — filtered" (`54l15-preservebond-120-02-evidence.txt`).  This is the
  "unbonded peer blocked by BONDED_ONLY FAL" observation.  In OPEN-mode
  fresh-pair sessions the peer can connect and is rejected at pairing
  (reason 0x05 / 0x3e disconnect cycles; e.g. the t10 Mode A run at
  00:28:54 and the Phase 3 debug session) — expected open-mode behavior,
  not a defect, and it did not disturb the streams (zero underruns/faults,
  see the final matrix).

Evidence: `/tmp/t8/pairing-filter/` (fresh-pair runs, modea clean/preserve
runs, 5340/54l15 boots, `nrf-probes-0{1,2,3}.txt`, reset logs).

## BlueZ preserve-bond fixes (2026-08-03)

- `46100a9` — preserve-bond reconnect via BlueZ `Device1.Connect()`, no
  raw-HCI helper (`--preserve-bond` path).
- `4488f53` — fresh BlueZ reconnect when the device is already connected
  (disconnect + `Device1.Connect()`).
- `19bec75` / `a40f75e` — see pairing filter phase.

Preserve-bond reconnect evidence (`/tmp/t8/54l15-preservebond-120-02-evidence.txt`,
nRF54L15 on `a40f75e`+`4488f53`):
`bap_central.py --peer-addr DB:A6:0C:05:A2:AA --preserve-bond --duration 120`;
12000 frames @ 100.0 fps; receiver `Stream[0] summary: SDUs=12000
decoded=24038 plc=38 decode_err=0 i2s_underrun=0 stream_reset=0` (SDUs
12000/12000 — the RTN=2 retransmission duplicates are NOT losses); offload
`submit=11896 success=11896 fallback=0`, all fault counters 0; `Pair()`
skipped (bond already present); Warnings: NONE; VERDICT PASS.

## Probe identity evidence (raw, per flash — session evidence only)

`nrf-probes` immediately before each flash; OpenOCD output cross-confirms
DPIDR.  No static probe→board mapping is recorded anywhere (doc hygiene
rule); this is the evidence observed during the T8/T9/T10 sessions only.

nRF54L15 (Seeed Xiao, `fw-flash-54l15` flashes 01–05 + t10):
```
SERIAL 8EE9B3FF  Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  DPIDR 0x6ba02477  PART 0x00054b15  VARIANT AAC0
```
OpenOCD: `CMSIS-DAPv2 VID:PID=0x2886:0x0066 serial=8EE9B3FF`, `SWD DPIDR
0x6ba02477`, Cortex-M33 r1p0, cpuapp + FLPR downloaded and verified.

nRF5340 (Ebyte E83, `fw-flash-5340`):
```
SERIAL E6635C08CB1F502B  Pico CMSIS-DAP  nRF5340  DPIDR 0x6ba02477  PART 0x00005340  VARIANT QKAA
```
OpenOCD (t10 flash log): `CMSIS-DAPv2 VID:PID=0x2e8a:0x000c,
serial=E6635C08CB1F502B`, `SWD DPIDR 0x6ba02477`, Cortex-M33 r0p4, both
cores downloaded (`merged.hex` + `merged_CPUNET.hex`) and verified.

AP IDR map: `nrf-probes` has no verbose option (`--help` shows only
`--find`) — recorded as a tool limitation; DPIDR cross-confirmed in OpenOCD
output.  Central-side note (not a receiver defect): the DK netcore SDC
returned HCI 0x0d (Limited Resources) after abortive raw-HCI connects;
recovery required `btmgmt find` → `hciconfig hci0 reset` before each
connect session, and the dongle was reflashed via J-Link (serial
001050023938) twice during the session.

## Central / environment

- Central: nRF5340DK `hci_uart` attached as hci0 on /dev/ttyACM2 @ 1,000,000
  baud H4; BD_ADDR `C0:AA:BB:CC:DD:EE`; settings
  `powered le secure-conn cis-central`.
- Receiver consoles: nRF54L15 `/dev/ttyACM0`; nRF5340 `/dev/ttyUSB0`, both
  115200 8N1.  Boot logs captured via `scripts/read_acm.py` before
  flash/reset; stream-time console captured via serial-mcp (E83 connection
  `e223c773-d2d1-4c67-9e69-195da70b5968`, nRF54L15
  `ade859a0-a87d-4523-a26e-9d8fb5b270a7`).
- External Intel-BT peer `64:49:7D:E3:53:40` present in several sessions;
  see the pairing filter phase for its disposition.

## Final accepted hardware matrix (2026-08-04, code `971e6a4`)

### nRF54L15 (SDC controller)

| Row | Command | Result | Receiver evidence |
|---|---|---|---|
| Mode A 120 s fresh | `bap_central.py --peer-addr DB:A6:0C:05:A2:AA --duration 120` | **PASS** (central 12000 frames @ 100.0 fps; t10 log) | Stream[0] SDUs=9486 decoded=24044 plc=5073 decode_err=0 i2s_underrun=0 stream_reset=0 (21% RF loss — Intel peer active; **zero underruns**); Stream[1] SDUs=9498 |
| Mode B 120 s fresh | `--stereo --duration 120` | **PASS** (central 12000 frames; t10 log) | SDUs=8476 decoded=24062 plc=7110 decode_err=0 i2s_underrun=0 stream_reset=0 (30% RF loss; **zero underruns**) |
| Bonded reconnect Mode A 120 s | `--preserve-bond --duration 120` (t9 run; t8 preserve-bond evidence on `a40f75e`+`4488f53`) | **PASS** (central 12000 frames @ 100.0 fps) | receiver SDUs=12000 decoded=24038 plc=38 decode_err=0 i2s_underrun=0 stream_reset=0; offload submit=11896 success=11896 fallback=0; faults 0 |
| FLPR hang gate Mode A 180 s | `flpr_hang_gate.py --duration 180 --port /dev/ttyACM0 --log …` | **PASS — 16/16 checks, RESULT PASSED** (retained gate stdout `/tmp/opencode/hang3_stdout.log` → receiver log `/tmp/t9/flpr-hang-mode-a-180-accept3.log`; t10 rerun 16/16 per EVIDENCE-SUMMARY, I2S underruns 0) | ack 150 ms, ACTIVE after 851 ms, epoch changed, probation cleared, recovery attempts=1 / runtime restarts=1 (baseline-diffed), all fault counters 0, frame_count_plausible PASS, state ACTIVE, submit=18024 success=17979 fallback=45 |
| FLPR hang gate Mode B 180 s (earlier, Aug 2 on `ace13ff`) | `flpr_hang_gate.py --duration 180 --stereo …` | **PASS — 16/16 checks, exit 0** (FLPR hang path unchanged through final code) | ack, fallback, epoch change, probation cleared, recovery=1, restarts=1, all fault counters 0 |
| Phase 3 lifecycle full | `bluez-wireplumber-phase3-gate.py --stage full` | **PASS — 3/3 playbacks, zero faults** (Aug 3 session, receiver firmware `a40f75e`, pairing-filter era; phase3-54l15-05) | playback1 SDUs=16581 decoded=33468 plc=306; playback2 SDUs=16576 decoded=33444 plc=292; playback3 SDUs=4578 decoded=9458 plc=302; all decode_err=0 i2s_underrun=0 stream_reset=0; fresh pair + bonded reconnect + third playback |
| Status / offload / hang evidence | `audio status`, `flpr offload`, `flpr status` captured per run | captured | Decode errors 0; offload submit=success, fallback 0, all fault counters 0; flpr Healthy yes, RX dup/lost/missed/ooo 0 |
| Sequence-gap tracker on healthy path | all t10 runs | silent | Zero `ISO seq gap` / `seq discontinuity` / `i2s_nrfx: Next buffers` lines in all 54L15 final logs (tracker silent when delta == 1) |

Boot acceptance (t10 boot, `971e6a4`): `BLE ready`, `Identity:
DB:A6:0C:05:A2:AA (random)`, `settings_load() OK`, `VCP ready`,
`Audio timing: GRTC+TIMER20+GPPI ready`, `I2S ready (48 kHz nom, 16-bit,
stereo, 16 blocks)`, `FLPR handshake init OK`, `FLPR READY`, `FLPR
READY_ACK sent`, PCM rings 481-frame capacity, `offload init OK`, `FLPR
runtime init OK`, `Advertising as "LE Audio Receiver"`.  Zero
warnings/faults at boot.

### nRF5340 / E83 (SW Split; the ISO sequence-gap defect target)

| Row | Command | Result | Receiver evidence |
|---|---|---|---|
| Mode A 120 s fresh (integration recheck) | `bap_central.py --duration 120` | **PASS** (central 12000 frames @ 100.0 fps) | Stream[0] SDUs=11320 decoded=22640 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0; Stream[1] SDUs=11336 |
| Mode B 120 s fresh | `--stereo --duration 120` | **PASS** (central 12000 frames; t10 log) | SDUs=11659 decoded=23320 plc=2 decode_err=0 i2s_underrun=0 stream_reset=0 |
| Mode B 120 s bonded reconnect | `--stereo --preserve-bond --duration 120` | **PASS** (central 12000 frames; t10 log) | SDUs=11658 decoded=23318 plc=2 decode_err=0 i2s_underrun=0 stream_reset=0 |
| Mode B 300 s (extra) | `--stereo --duration 300` | **PASS** (central 30000 frames @ 100.0 fps; t10 log) | SDUs=29151 decoded=58302 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 |
| Mid-stream `audio status` | injected (Mode B 60 s session) | diagnostic | Frames decoded 5354, Decode errors 0, I2S underruns 0, Stream resets 0, **Drift state ACTIVE, Drift ppm −500 (APLL steering)**, Resampler identity |
| Mid-stream `audio perf` | injected | diagnostic | Repeat fb 0 (perf measurement compiled out on the E83 production build; the repeat path's observable effect is i2s_underrun=0 / stream_reset=0, which held everywhere) |
| Warning/error scan | all final E83 runs | **zero** | Zero `ISO seq gap` / `seq discontinuity` / `i2s_nrfx: Next buffers not supplied on time` lines across ALL E83 runs; underrun/reset/decode faults zero |

APLL evidence: `Drift state ACTIVE, Drift ppm −500` proves the APLL
actuator is steering (identity rate path; repeat fallback zero in steady
state per the counters above).

**No DAC was connected to the E83 and no audibility claim is made** for
either target — all evidence is measurable receiver/central counters.

## ISO sequence-gap concealment (`971e6a4`) — evidence limitation, not a hardware activation claim

The original T8 public hardware criteria — zero underruns/faults across the
required streams on both targets — are met by the final matrix above.  The
per-CIS ISO sequence-gap path (`src/audio_iso_seq.{c,h}`) could NOT be
deterministically activated on hardware through the available SW Split /
controller / BSim APIs:

- The E83 RF link was exceptionally clean all session (plc 0–2 per run
  across ~15 min of streaming; the Intel peer that caused 8–30% loss in the
  Aug 2/3 sessions was absent/quiet).  With zero RF loss the SW Split
  controller never omits callbacks, the sequence tracker stayed silent
  (delta always 1), and no gap concealment fired.  **Gap PLC was NOT
  observed on the E83.**
- The concealment behavior is proven by the **18-test `iso_seq` suite**
  (first, contiguous, single/multi gap, wrap, duplicate, backward,
  over-bound resync, Mode B exact-PLC pushes, Mode A
  simultaneous/one-sided omission, decode chronology,
  no-synthesis-on-resync) plus the integration unit tests covering the
  exact gap behavior, and BSim Stage 1 (hashes unchanged).
- Defect provenance is preserved by the prior T9 failing hardware logs
  (the 8–30% RF-loss sessions where omissions and gap concealment were
  originally exercised).
- New hardware runs prove the added code introduces **zero
  underruns/regressions** (final matrix; zero `ISO seq gap` /
  `i2s_nrfx` lines; BSim hashes unchanged).

This is recorded as an explicit **evidence limitation** — the sequence-gap
concealment path is covered by direct production-module tests and prior
failing-hardware provenance, not by a clean-session hardware activation.

## Superseded / historical rows (retained for provenance, not final evidence)

Clearly distinguished from the accepted final rows:

1. **Stack overflow (pre-`ace13ff`)** — two byte-identical FATAL ERROR 2
   faults on the BT RX WQ stack; root cause and fix above.  Superseded by
   `ace13ff`; live re-verification passed.
2. **Aug 2 nRF54 Mode A underruns (on `ace13ff`)** — 4 and 3
   `i2s_nrfx: Next buffers not supplied on time` + auto-recovered
   `I2S underrun, restarting DMA` events under 14.3% RF loss (dual-CIS
   starvation).  Superseded: the final t10 Mode A/B rows show **zero
   underruns** even with 21–30% RF loss (single-CIS Mode B and the
   post-filter link).
3. **Aug 2 FLPR hang gate Mode A 15/16 flag (on `ace13ff`)** —
   `frame_count_plausible` failed (submit shortfall ≈ 12–14 s) because the
   gate compared absolute counters across sessions and the link lost 8–14%
   of SDUs.  Superseded by the hang-gate baseline-diff commits (`1d90873`,
   `c056936`, `4ef25b2`, `3df6da8`) plus clean links: the final FLPR hang
   Mode A runs are **16/16** (retained stdout `/tmp/opencode/hang3_stdout.log`
   and the t10 rerun).
4. **Aug 3 E83 intermediate run (`5340-modea-120-pb.log`, 2026-08-03 20:33)**
   — one `i2s_nrfx: Next buffers not supplied on time` at the stream-end
   teardown boundary (summary counters still i2s_underrun=0).  Superseded by
   `9b78d87` (central writer keeps feeding during BAP teardown); the final
   T10 E83 logs contain zero `i2s_nrfx` error lines.
5. **Hang-gate `accept2` run (`/tmp/opencode/hang_stdout.log`)** — RESULT
   FAILED with BlueZ `AuthenticationFailed` during pairing (stale central
   bond state), before injection.  Central-side transient, not firmware;
   the immediate rerun (`accept3`) PASSED 16/16.
6. **Intermediate gate runs** — 42/42 on `1d90873` (Aug 2), 44/44 on
   `c056936` (Aug 3), 46/46 on `ac1fa06` (Aug 3, retained
   `/tmp/opencode/gate1.log`) — each at their own commit; the final gate is
   47/47 (below).

## Warning scan (final logs)

Zero `FATAL` / `USAGE FAULT` / `Assert` / overflow post-`ace13ff` on either
target.  All `<wrn>`/`<err>` lines in the final session logs classified:

- `Pairing rejected (BONDED_ONLY): unbonded peer …` — deliberate defense-in-
  depth rejection of the unbonded Intel peer (documented policy).
- Intel peer `Connected:` + `reason 0x05`/`0x3e` cycles in OPEN-mode fresh-
  pair sessions — expected rejection of the unbonded peer (see pairing
  filter phase); absent from bonded sessions.
- FAULT_HANG recovery lines (ring reset timeout → runtime restart) — the
  expected FLPR hang-gate recovery path; restart succeeded.
- No `i2s_nrfx` errors, no decode errors, no integrity faults in any final
  row.

## Final software gate (exact final code `971e6a4` + `1a5842d`)

Composition at HEAD: **47 children** = 28 twister C suites (incl. the new
`iso_seq`) + 4 exec-only C suites + 12 Python suites + coverage + matrix +
BSim Stage 1.

- **Final gate: 47 PASS / 0 FAIL / 47 TOTAL** on the exact final code,
  zero compiler warnings and zero Kconfig assigned-value warnings in the
  production builds — recorded in the committed coverage matrix at
  `3c29421` ("The T8-follow-up canonical gate on `1a5842d` is 47 PASS /
  0 FAIL / 47 TOTAL (28 twister suites including the new `iso_seq`, 4 exec
  suites, 12 python suites, coverage, matrix, BSim Stage 1)").  The full
  final run's log and elapsed runtime were **not retained** — the exact
  elapsed runtime of the 47-child run is **unavailable**; the closest
  retained full-gate log is `/tmp/opencode/gate1.log`: `Gate complete: 46
  PASS / 0 FAIL / 46 TOTAL` on `ac1fa06` (2026-08-03 23:36, 46 children
  before the `iso_seq` twister suite; no elapsed line in the retained
  tail).  Earlier retained gate logs: `/tmp/t8/gate-final-c056936.log`
  (44/44 on `c056936`), `/tmp/t8/gate-final-1d90873.log` and `-b.log`
  (42/42 on `1d90873`) — historical.
- **Coverage baseline ACCEPTED** at `1a5842d` (generated on clean
  `971e6a4`): 26-file numeric population — lines **3281/3722 (88.2%)**,
  branches **1433/2041 (70.2%)**, functions **205/205 (100.0%)**; the
  candidate `/tmp/opencode/baseline-new.json` matches the committed
  `tests/coverage-baseline.json` byte-for-byte in totals.  All three T8-era
  refreshes (`6578a9c`, `ac1fa06`, `1a5842d`) were honest upward additions
  (new direct-suite files only).
- **Builds 3/3 on final code** — pristine builds present for all three
  targets and flashed/verified in the t10 session:
  `build/nrf5340/le-audio-receiver/zephyr/zephyr.elf` (2026-08-04
  03:00:53), `build/nrf54l15/.../zephyr.elf` (03:01:15), dongle build
  (03:00); `fw-flash-5340` (03:59) and `fw-flash-54l15` (03:23) both
  flashed and verified (`ninja: no work to do` on the app images —
  identical to the final build; OpenOCD downloaded and verified both
  cores / cpuapp+FLPR).
- **Build contract 76/76** — `scripts/check-build-contract.py` registers
  **76 assertions** at HEAD (verified: the `build_contract` gate child on
  the final-era checker prints `76 assertions, 0 failed / BUILD CONTRACT
  PASSED` in `/tmp/opencode/gate1.log`).  A real dual-target contract run
  on the final builds is not among the retained logs; the closest retained
  real run is `/tmp/t8/build-contract.log` — 74 assertions, 0 failed, on
  the Aug 2 builds (T7-era count, superseded by the 76-assertion checker).
- **Zero actionable warnings** — final warning scans
  (`/tmp/opencode/build-54l15-warn.log`, `dongle-warn.log`, `b5340-final.log`)
  show only the documented non-actionable NCS v3.3.0 diagnostics
  (simple_bus_reg / avoid_unnecessary_addr_size / UART_CONSOLE
  assigned-n-got-y / PARTITION_MANAGER deprecation /
  BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY choice gap / experimental
  BT_LL_SW_SPLIT + BT_CTLR_SET_HOST_FEATURE + BT_CTLR_PERIPHERAL_ISO);
  zero compiler warnings.  (The Aug 4 01:34 `build-5340-warn.log` attempt
  failed board resolution from a wrong cwd and is not evidence.)
- `git diff --check` clean (final doc commit).

## Log inventory (preserved)

`/tmp/t8/`: flash-54l15-01..05.log, flash-5340-0{1,2,3}.log,
54l15-boot-01..04.log, 54l15-modea-0{1,2,3,4,6,7}.log,
54l15-modeb-01.log, 54l15-reconn-modea/b-01.log,
54l15-bonded-modea-01.log, 54l15-preservebond-01.log /
-120-01.log / -120-02.log + `54l15-preservebond-120-02-evidence.txt`,
modea/modeb/reconn central logs, flpr-hang-mode-a.log, flpr-hang-mode-b.log,
flpr-hang-mode-a-0{2,3}.log, phase3-54l15/ (3 playback logs + gate_test.wav),
phase3-54l15-0{2,3,4,5}/ (playback logs + gate_test.wav),
phase3-54l15-0{3,4,5}-full.log, phase3-playback-debug.log,
5340-modea-120-pb.log, 5340-flash-03-boot.log, status-*.log, unpair-0*.log,
pairing-filter/ (fresh-pair, modea-clean/preserve, boots, nrf-probes),
stack-evidence/, gate-final-1d90873{,b}.log, gate-final-c056936.log,
gate-cov-1d90873.log, build-*.log, build-contract.log, readacm-*.log,
nrf-probes-*.txt.

`/tmp/t9/` (Aug 3 23:54 – Aug 4 01:17): 54l15-modea-120-fresh.log,
54l15-modea-120-bonded.log, 54l15-modeb-120.log,
flpr-hang-mode-a-180{,b}.log, flpr-hang-mode-a-180-accept{,2,3}.log,
5340-modea-120{,b}.log, 5340-modea-120-bonded{,2,3,4}.log,
5340-modeb-120{,b}.log.

`/tmp/t10/` (final session, code `971e6a4`): 54l15-boot-t10.log,
54l15-modea-120-t10.log + -central.log, 54l15-modeb-120-t10.log +
-central.log + -120b-central.log, 54l15-flpr-hang-mode-a-180-t10.log,
flash-54l15-t10.log, flash-5340-t10.log, e83-modeb-120-central.log,
e83-modeb-120-bonded-central.log, e83-modeb-300-central.log,
e83-modeb-60-{midstream,perf,perf2,perf3}-central.log, btmon{,.2,.3}.log,
`EVIDENCE-SUMMARY.md`.

`/tmp/opencode/`: gate1.log (46/46 on `ac1fa06`), gate2.log (44/44),
hang_stdout.log / hang3_stdout.log (FLPR hang gate FAILED/PASSED stdout),
build-54l15-warn.log, dongle-warn.log, b5340-final.log, baseline-new.json,
cov-report/, cov-write2/.

## Matrix verdict summary

- nRF54L15: builds/flash/boot PASS; Mode A 120 fresh PASS (zero underruns,
  21% RF loss); Mode B 120 fresh PASS (zero underruns, 30% RF loss); bonded
  reconnect PASS; FLPR hang Mode A 16/16 PASS; FLPR hang Mode B 16/16 PASS
  (earlier); Phase 3 full 3/3 PASS; zero firmware faults.
- nRF5340/E83: Mode A 120 PASS; Mode B 120 fresh PASS; Mode B 120 bonded
  reconnect PASS; Mode B 300 PASS; APLL ACTIVE ppm −500; zero
  `i2s_nrfx`/underrun/reset/decode faults in all final rows.
- Sequence-gap activation: NOT observed on hardware (clean-link session);
  covered by direct production-module tests + prior failure provenance —
  documented evidence limitation, not a hardware activation claim.
- Final software gate: 47 PASS / 0 FAIL / 47 TOTAL (runtime of the final
  run not retained); coverage baseline accepted; builds 3/3; build
  contract 76/76 (checker + gate child; real-run log not retained); zero
  actionable warnings.
- **T8: ACCEPTED (2026-08-04).**
