# PB-039/PB-038: all-nRF54L15 clean integration verification

## Scope and provenance

Clean tested checkpoint: `104e67ade0e361093a88d1832b5dcb41552e8e8d`.
Production code checkpoint: `e478e59df4597b6c7a6b4a56a3bb3e9995bdec16`.
The later commit adds partial IPC registration rollback/retry coverage and
documentation, not another firmware change. Validation clones are independent
of the primary implementation tree and its preserved PB-013 user edits.

Active SDK is NCS v3.4.1, toolchain `8285d8ad56`. Receiver is XIAO
nRF54L15 CPUAPP/FLPR; second XIAO alternates standalone source CPUAPP and
Linux HCI CPUAPP, never concurrently. Canonical simulation uses two integrated
SW Split nRF54L15BSim peers. Native/Python tests remain portable host tests.

This record proves local migration verification, not human PR acceptance,
hosted CI, active GitHub draft FR4, publication, analog measurement or audibility.
PB-013 360-frame FLPR support and PB-041 nonce identification remain separate.
Final repair/verification changed no SDK source, SAMD11 firmware, frozen
physical audio recipe or limit. Earlier approved BSim migration added measured
target-native startup recipes while retaining historical recipes and unchanged
payload/PCM/lifecycle contracts; it did not synthesize legacy startup loss.

## Integrated gates

| Boundary | Verdict | Retained evidence |
| --- | --- | --- |
| Clean canonical software | 80 PASS / 0 FAIL / 80 TOTAL | `/tmp/opencode/nrf54-104e67a-canonical-20261001-r1.log`, canonical root `/tmp/opencode/nrf54-104e67a-canonical-20261001-r1/` |
| Canonical BSim | 17 scenarios / 26 runs pass; strict TX hashes, PCM and lifecycle oracle unchanged | `/tmp/opencode/nrf54-104e67a-bsim-20261001-r1/` |
| Physical build shapes | Receiver CPUAPP/FLPR, standalone CPUAPP, HCI CPUAPP pass; resolved receiver contract 73/73 | `/tmp/opencode/nrf54-104e67a-physical/le-audio-receiver/build/`, `/tmp/opencode/nrf54-104e67a-artifacts-20261001-r1/validation.json` |
| HIL host suite | 340 passed / one intentional hardware-opt-in skip | Clean validation above; final primary host rerun `/tmp/opencode/nrf54-migration-final-host-20261001-r1.log` |
| Capture infrastructure | Real capture model/runner/analyzer host tests pass; immutable mono/stereo fixture snapshots retained | `/tmp/opencode/nrf54-capture-contract-evidence-20261001-r1/result.json` |
| Normal-image Linux HCI | Six 120-second fresh/bonded mono, Mode A and Mode B cases pass | `/tmp/opencode/nrf54-104e67a-hci-clean-20261001-r1/` |
| Frozen exact-local-artifact matrix | 20/20 pass, two complete passes; zero failed, cancelled or cleanup children | `/tmp/opencode/hil-runs/nrf54-104e67a-matrix-20261001-r1/` |

Canonical coverage enforces the existing 36-file population and surviving
per-file baseline. No ratio, denominator, recipe, test count, audio threshold
or source send margin was weakened. Receiver development builds retain the
specific documented `BT_CONN_TX_NOTIFY_WQ` experimental notice. Enabled-assert
CMake diagnostics and host-only unsupported-SoC notices remain raw; no general
compiler, Kconfig, runtime or CMake warning waiver is introduced.

## Exact image and archive identities

| Object | SHA-256 |
| --- | --- |
| Receiver archive | `ba7ebf6dbc7d9811d0fa6f2dd632305069b71c2ab127ff221af31d61ce532dfa` |
| Source archive | `1d0004d0c6fb73815e9dbc5d495b85377cb2bd57f94c23d5e34899b7787bc6b0` |
| Receiver CPUAPP HEX | `9adeae61e319bba1fe1f1ed529e2c98ade63d51bfb5bd56b35c8f6992da8e64f` |
| Receiver FLPR HEX | `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a` |
| Standalone source CPUAPP HEX | `57c836e3a70e6a1d3bbf6a15839fa879ef3548899322a05df4277e12b32a69c2` |
| HCI CPUAPP HEX | `243052e606c20ad65d79eee153521827102dcd76ecd297d3376cb7f99e0caefd` |

Public artifact extraction/revalidation passed before matrix launch. Archives
bind exact commit, NCS version, board, members and checksums. These are new
local candidates, not replacements for the existing private draft assets.

## Frozen physical matrix and independent evidence review

Unmodified `run-rh4-matrix` consumed both exact archives through the public
resolver. Ten frozen rows ran twice, including mono, Mode A, Mode B, preserved
bond, all three 7.5-ms modes, reconnect, FLPR hang and FLPR stall. All **20
children passed**; scheduled/attempted/passed are each 20, failed/cancelled are
zero and aggregate/child cleanup failures are empty. Source 3000/2000-us
margins, row recipes, transport/error/PLC limits and fault definitions stayed
unchanged. No row retry, skipped fault, diagnostic image or shortened duration
was substituted.

Independent read-only review:
`/tmp/opencode/nrf54-104e67a-matrix-evidence-review-20261001-r1.json`.
It rehashed every listed child payload plus 12 aggregate payloads, rechecked
archive identities and per-child exact commit/NCS/image inventories, verified
the same external session snapshot in every child, and verified **120 ordered
identity checkpoints**, six per child with distinct paired roles and retained
raw DP/AP/FICR evidence. Aggregate result SHA-256:
`112a69acddb1830bb1bcb4382621db4688216084da5def17745bf9bfbd6da51d`.
Supervisor ended `inactive/dead`, `ExecMainStatus=0`. Boards were left with
this exact receiver CPUAPP/FLPR and idle standalone source, not simultaneous
HCI/source roles. No runtime board mapping is reusable without fresh identity.

This closes the local exact-artifact transport/runtime migration gate. It does
not qualify the existing GitHub draft, ALSA capture hardware, MA1/SA1 analog
metrics or public release. Capture fixtures/model/runner migration has separate
host evidence above; digital I2S captures are not analog capture substitutes.

## HCI fixture qualification

Normal tracked `scripts/bap_central.py` ran through session-bound helpers,
stock SAMD11 bridge, UART20 H4 at 1,000,000 baud 8N1 without flow control.
Six cases emitted **72,000 writer frames**, with **PLC 428** and nonzero
loss inside unchanged limits. Decoder errors, I2S underruns, stream resets,
case alerts and bounded kernel HCI/SMP alerts are zero. Fresh Connect-led
bonding and preserved-bond reconnect both passed; no diagnostic CLI override,
RAM-trace firmware or altered UART sentinel substituted for production images.

Raw evidence includes bounded kernel journal, receiver console, source logs,
image hashes, fresh identity, system-manager containment and scoped adapter/
process cleanup. Runtime identity includes DPIDR `0x6ba02477`, AP0/AP1
`0x84770001`, AP2 `0x32880000`, AP3 `0x00000000`, PART `0x00054b15`,
VARIANT `0x41414330`, plus live USB identity and explicit paired roles.
No static probe or tty mapping is authority for future runs.

Qualification is bounded development-fixture proof. It is not generic
flow-control-free UART reliability, public consumer adapter support,
PipeWire/WirePlumber desktop acceptance or analog output qualification.

## Repairs and retained failures

- [Source peer-enqueue guard](nrf54l15-source-batch-guard-results-20261001.md)
  fixes cooperative TX scheduling interference without changing 3000/2000-us
  margins or receiver limits.
- [FLPR reload and lifecycle repair](nrf54l15-flpr-fresh-reload-results-20261001.md)
  restores fresh boot context/notification handling, drains heartbeat state and
  moves idle restart off the heartbeat workqueue. Native regressions and frozen
  trace-free physical hang/stall diagnostics pass.
- `2a0e792` exact matrix failed source under-lead after one passing row;
  `b21c7a7` failed FLPR hang recovery after eight passing rows. Both remain
  failures, not acceptance or rewritten evidence.
- Clean `e478e59` canonical 79/1/80 failed branch coverage only. Meaningful
  partial-registration rollback/retry regression restored clean `104e67a`
  80/0/80; production code and committed baseline remain unchanged.

[Reference audit](nrf54l15-final-reference-audit-20261001.md) classifies retained
history, unsupported-input regressions and external SDK context separately from
active target selection. Historical builds, results and archives are retained.

## Product and repository closure boundary

PB-019, PB-034 through PB-037 and PB-039 have checked evidence-backed criteria
and Final Summaries in **Review**, not accepted Done. PB-038 records aggregate
technical criteria and rationale without silently changing its product-owned
Backlog status or bypassing predecessor human acceptance. PB-018 remains
explicitly superseded, not falsely qualified or erased. Product PR/merge
lifecycle is separate from the completed local engineering verification.

Final closure changes documentation/backlog evidence only. Tested firmware
provenance remains exact `104e67a`; no newer documentation commit is substituted
as a firmware-build or hardware identity claim. User-owned PB-013 remains
byte-for-byte preserved and unstaged, and private graph/raw lab evidence stays
untracked/external. No push, PR creation, merge or publication was performed.

## 2026-10-08 preservation appendix: retired snapshot facts and negative methods

> The September checkpoint/continuation/observability documents, the
> SN_STRICT-era session-state snapshot, the PR wrap-up snapshot of
> 2026-10-02, and the workstation-transfer status were retired in the
> documentation reconciliation of 2026-10-08. Facts not already carried by
> the sections above are preserved here verbatim in meaning. All are dated
> historical evidence/chronology, not current status; the backlog and this
> report's sections above own current state.

### Negative methods and unresolved cause attribution (2026-09-24/25 HCI track)

- Rejected-experiment set that cannot establish a root cause (UART idle
  timeout 100 us vs 1000 us, NCP in-memory timestamps, 20-credit bridge
  queue/proxy sensitivity probe, ISO link-quality flush snapshot): preserved
  in the PB-019 report's 2026-10-08 appendix. "Trace perturbation" (the
  RAM-trace pass changes timing and cannot qualify) and "unknown cause" are
  separate historical findings; the generated driver-sentinel repair
  eliminates only the stale old-slot false-replacement class and resolves
  no per-historical-fault root cause. The final source-batch guard and FLPR
  fresh-reload repairs (2026-10-01) are documented separately in their own
  reports; no blanket root cause is claimed for the 2026-09-25 failure.
- The source-batch-guard sentinel replay and passive host/ring comparison
  fix **false replacement only**; they exclude no unique wire/DMA/driver
  cause, and the fault remains downstream of the Linux monitor and before
  the parser without isolating SAMD11, wire, UARTE, DMA, or driver copy.
- PR-era chronology (from the retired wrap-up snapshot): the 2026-10-02 PR
  prepared PB-019 and PB-033 through PB-040 as PR-gated Done transitions
  with human merge separate; the recorded `104e67a` lab results were
  recorded, not newly rerun, and the old raw lab roots were already absent
  in that session, so reviewers cannot reopen them from a checkout; fresh
  host checks (340 passed plus one intentional hardware-opt-in skip, and a
  127-test focused pass) differ from the full canonical/physical gate; two
  exact host-log hashes were retained; and hosted CI had real
  source-population, dynamic PHY plugin, and encrypted `libCryptov1` closure
  failures, repaired under PB-040/PB-034 without disabling encryption or
  strict policy. Retention boundaries: PB-040 remains software-upgrade
  proof only, and PB-033's historical legacy-retention disposition was later
  superseded by the 2026-10-08 reconciliation.

### 2026-08-22 RH3 snapshot unique per-run records (historical diagnostic era)

These are historical older-era observations: the 2026-08 rows ran on the
nRF5340DK source fixture under NCS v3.3.0, before the XIAO source migration
and before clean `104e67a`. They are not `104e67a` physical proof and not
current acceptance of any tuple. The retired 2264-line resume state retained
full per-run records for the early RH3 campaigns. The direct diagnostic runs
with their own named result documents (ModeA1 through ModeA17,
RH3-40/41/42) are preserved there; the
2026-08-15/2026-08-20/2026-08-22 matrix and direct-runner rows are preserved
in `system-hil-rh3-software-status.md`. Facts carried only here now:

- `rh3-20260815-01` failed fresh Mode A before streaming after QoS with
  `first_errno=-16` (bulk-enable submission diagnosis); child
  `...01.children.855cf5240f2e/rh3-20260815-01.p1.r2.rh3.fresh_mode_a_48_4_1.c8e167dc0e97`
  with `source-records.jsonl`. `rh3-20260815-02` failed fresh mono only at
  the receiver tail while live `flpr offload` showed
  `submit=12191 success=12190 fallback=0 busy=0`, the observation that drove
  the host tail-settle fix; child
  `...02.children.f5b5588ed7f5/rh3-20260815-02.p1.r1.rh3.fresh_mono_48_4_1.a165935bdb79/`.
  `rh3-20260815-03` added the state-callback
  versus local GATT busy-clear ordering finding (retry fix software-verified
  only). `rh3-20260815-04` proved CPUAPP hash
  `fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049` and
  identified synchronous two-CIS `bt_bap_stream_connect()` submission as
  that failure's root cause. All four are immutable failed evidence.
- `rh3-20260815-05` was cancelled by the host executor default `120000 ms`
  command timeout, not a device verdict; before cancellation the source
  reached `streaming` with active `first_errno=0`, controlled stop status
  `sub=5092 sc=4948 sf=0`, teardown cause `stop`, terminal `fail`; child
  duration 120.205855 s; source hash as RH3-04.
- `rh3-20260815-06` failed fresh mono at the receiver tail (`I2S underruns=1;
  Stream resets=1; Push failures=1`) with max RX callback gap `268003 us`,
  `8884` PLC frames, FLPR `submit=success=12650`; source terminal
  `sub=12644 sc=12000 cb=12644 sf=0 out=0`; images source
  `fea5ff44...`/CPUNET `4e4b82f5...`, receiver CPUAPP
  `4838edf3...`, FLPR `45ab8d15...`.
- `rh3-20260815-07` failed at boundary
  `pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: run row` with detail
  `active status snapshot invalid: state='teardown'; aborted=True;
  first_errno=-116`; source teardown cause `timeout`.
- `rh3-20260815-08` failed fresh Mode B at the receiver tail (`I2S
  underruns=1; Stream resets=1; Push failures=1; offload submit/success
  mismatch`) with retained `i2s_nrfx: Next buffers not supplied on time`,
  `Cannot write in state: 4`, `I2S underrun, restarting DMA`, decoded 28354,
  PLC 28290 (99%), settle snapshots `14223/14222`, `14253/14252`,
  `14278/14277`; source CPUAPP `b40d03d1...`, receiver CPUAPP
  `4838edf3...`; source terminal `sub=12320 sc=12000 sf=0 cb=12320 out=0`.
- `rh3-20260820-01` failed fresh Mode A at the receiver tail with one
  `audio_i2s: I2S slab full - dropping frame` warning, underrun 1,
  push failures 1, max RX callback gap `9234 us`, I2S write gap `13712 us`,
  offload `14040/14040`; source CPUAPP `b40d03d1...`, receiver CPUAPP
  `6d9caa89...`; both Mode A streams reached `sc=12000 sf=0`. The proposed
  post-start slab-backpressure repair has no hardware proof.
- `rh3-20260821-03-modea-depth-fix` failed receiver tail because the
  post-teardown `bt iso quality` query was unavailable; source
  `scored_complete` at 160547 ms, teardown 164611 ms, PASS terminal 165419 ms;
  cleanup record `command_id='parse-error' run_id='unbound'` (host-cleanup
  diagnostic, not a firmware root-cause claim); slot 0 `rx_valid=137
  rx_lost=14310`, slot 1 `rx_valid=135 rx_lost=14249`.
- `rh3-20260822-01-modea-tail-order-cleanup` failed `receiver tail` with
  `invalid ISO link quality: ISO link quality grammar malformed` while raw
  output contained accepted two-record layout (`rx_unreceived` 14317/14241,
  `crc_error` 0/0); aggregate `audio perf` `iso_recv=28754`,
  `lc3_decode=28740`, `sink_push=14369`, max RX callback gap 10112 us; FLPR
  `submit=success=14370`; retained `bt_conn ... failed to establish. RF
  noise?` warning; OpenOCD extra-erase-range warnings retained in flash logs.
- `rh3-20260822-02-modea-iso-parser-fix` accepted the repaired grammar
  (`rx_unreceived` 14293/14387, `crc_error` 0/89, `duplicate` 1/12), failed
  `receiver tail` on `offload state='STOPPED'`; Stream[0] `SDUs=130
  decoded=28750 plc=28620 rx_valid=130 rx_lost=14289 rx_no_ts=50`, Stream[1]
  all-zero with `rx_lost=14389 rx_no_ts=14389`.
- `rh3-20260822-03-modea-critical-tail-snapshot` retained live ISO
  (`rx_unreceived` 14322/14248, `crc_error` 2/0) plus post-teardown
  `STOPPED / epoch=0 gen=3, submit=14377 success=14377`, RTT avg 1863 us,
  startup prep `epoch=1626819632 gen=2 state=ACTIVE`; failed `receiver tail`
  on the same offload-state rule.
- `rh3-20260822-04-modea-lifecycle-split` failed `log scan` on the `bt_conn`
  warning; live ISO `rx_unreceived` 14308/14233, Stream[0] `rx_valid=137
  rx_lost=14310 rx_no_ts=85`, Stream[1] `rx_valid=135 rx_lost=14249
  rx_no_ts=8`; FLPR active first snapshot `111/110`, settle retry
  `submit=131 success=131 retries=1`.
- `rh3-20260822-05-current-image-mono-control` (passed control): ISO
  `rx_unreceived=9 crc_error=4`, summary `rx_valid=12644 rx_lost=12
  rx_no_ts=9 decoded=12656 PLC=12`; PCLK diagnostics 1808-2293 ppm.
- `rh3-20260822-06-current-image-modeb-control` (passed control): summary
  `rx_valid=113 rx_lost=13591 decoded=27408 PLC=27182`, ISO
  `rx_unreceived=13267 crc_error=2`; PCLK diagnostics 446-632 ppm.
- `rh3-20260822-07-modeb-selected-layout` (passed control, telemetry image
  `d8e5708...`): full eight-field selected layout `iso_interval_1250us=8
  nse=6 cig_sync_us=8184 cis_sync_us=8184 c_max_pdu=240 c_phy=2 c_bn=1
  c_flush_1250us=8`, ISO `rx_unreceived=13267 crc_error=1`; active FLPR RTT
  avg 1849 us n=79; post-stop RTT avg 1844 us n=13704, FLPR cycles avg 996.
- `rh3-20260822-08-mono-selected-layout` (passed control, telemetry image):
  selected layout `nse=6 cig_sync_us=5304 c_max_pdu=120 c_flush_1250us=8`,
  ISO `rx_unreceived=9 crc_error=2 duplicate=0`, summary as mono control;
  active FLPR cycles min/max/avg 979/1015/999 n=46; post-stop RTT
  1819/1972/1840 n=12656, cycles 977/1035/996; sink/perf counters
  iso_recv=12656, lc3_decode=12656, volume=12656, sink_push=12655; slab
  free 1/8, output frames 476/478, max RX callback gap 80248 us, I2S write
  gap max 80145 us, I2S write time max 21 us; FLPR handshake epoch
  2968454096, TX seq 137 acked 136, RX seq 135; source flash erase tails
  `0x01023afc..0x01023fff` and `0x0005741c..0x00057fff`; bounded mono vs
  RH3-07 Mode B comparison table retained.
- `rh3-20260822-09-modea-selected-layout` (failed `log scan`, same warning):
  both CISes delivered `rx_valid` 137/135 with `rx_lost` 14310/14249,
  selected layout nse=3 with per-slot `cis_sync_us` 5304/2652,
  `c_flush_1250us=16`, slot comparison table with `tx_last_subevent`
  14323/14251 retained.
- `rh3-20260822-10-modeb-7p5-selected-layout` (failed `session end`; missing
  receiver summary slot 0): source hello `frame_samples_48_3_1=360`
  `octets_48_3_1=90` `sdu_modeb_48_3_1=180`, scored target 16000; receiver
  ASE `chan_count=2`, 7500 us, 90 octets, QoS `interval=7500 sdu=180 rtn=5
  latency=15 pd=40000`; selected layout `iso_interval_1250us=6 nse=6
  cig_sync_us=6744 cis_sync_us=6744 c_max_pdu=180 c_flush_1250us=6` (derived
  FT 1 is an observation, not a configuration request); `rx_unreceived=18850
  crc_error=0 duplicate=0`; FLPR active zero plane `submit=0 success=0`;
  receiver fatal before post-stop diagnostics: `i2s_nrfx: Next buffers not
  supplied on time` twice (00:24:54.361,273 and 00:27:20.667,425),
  `Cannot write in state: 4`, `I2S underrun, restarting DMA`, then teardown
  `ASSERTION FAIL [err == 0] @ .../hci_core.c:506`, `Controller unresponsive,
  command opcode 0x206f timeout with err -11`, `ZEPHYR FATAL ERROR 3: Kernel
  oops on CPU 0`, `Halting system`; source terminal `verdict=fail`; expected
  16859 submitted / 16000 scored SDUs. Missing summary, the fatal, and the
  missing post-stop FLPR snapshot are blockers for any acceptance
  interpretation.
- Physical-run count after that campaign: twenty runs (fifteen failed, one
  cancelled - `rh3-20260815-05`, four passed controls); the target-three
  source image received eleven direct physical diagnostics.
- `rh3-modea1b-20260903-offload-disabled` rerun (as distinct from the
  original `rh3-modea1-20260903` fixture-defect baseline): derived diagnostic
  CPUAPP hash `4f2c5e5a...` under HEAD `eac880d5...`, double pristine builds
  byte-identical; source terminal PASS; runner failed `session end` because
  its summary scan retained neither required slot after its cursor despite
  raw console evidence containing both summary lines; `result.json` has
  `summary={}` with no frozen transport-limits verdict; triage class is
  fixture/runner evidence-order failure; integrity 24/24; post-stop FLPR and
  handshake commands were not reached, so the binary offload-vs-transport
  classification is inconclusive.
- H40/H41/H42 controls and the offload-disabled row must not be adopted or
  relabeled as accepted repairs; each has its own canonical result document
  (`system-hil-rh3-40/41/42-*.md`), and the session-end raw-fallback
  hypothesis (`rh3b`: 60 s delay) was later corrected by the RH3c raw-scan
  finding (wrong byte range after the tail collector consumed lines).
- Historical RH2 rows: `rh2-20260815-12` failed only on a serial
  close/read race (`'NoneType' object cannot be interpreted as an integer`)
  after a healthy `SDUs=764 decoded=777 PLC=13` exchange;
  `rh2-20260815-13` failed on real RF connection-establishment loss with
  `Security changed: level 1 err 9 bonded 0`, disconnect reason `0x3e`, and
  source `-116` after ~20 s, later asserting on HCI Disconnect `0x0406`
  timeout; `rh2-20260815-14` formally passed the `rh2.short_mono_48_4_1`
  witness with source `sub=764 cb=764 sc=120` and FLPR
  `submit=success=309`. Post-`rh2-20260815-12` host fixes (completion-driven
  `sem_tx_wake` pacing with 62/62 focused tests, serial_io close/pump-gate
  synchronization, receiver tail 0.5 s repoll, Mode A enable retry, CIS
  connect serialization, RH3-07 failed-CIS retry, source outstanding-target
  three, lifecycle-boundary evidence collection) are recorded in the
  software-status report.
- Historical fixture table (dated observation, never a current mapping):
  receiver probe `8EE9B3FF` console `/dev/ttyACM2`, source J-Link
  `001050023938` console `/dev/ttyACM1`; `/dev/ttyACM0` on that DK is the
  network-core VCOM, not the HIL source console. Last recorded runner-owned
  flash of that era (`rh3-matrix-status-batch-fix-20260909` pass-2 stall
  child): source `43bdef15...`/`2c3af526...`, receiver `e8f1bd19...`
  receiver CPUAPP with FLPR `45ab8d15...`; source at 0 dBm receiver-era RF
  power notes (`receiver 0 dBm, source +3 dBm`) are dated observations.
- Historical flash hash chains for the ModeA12/M13/14/15/16/17/18-era
  runner-owned flashes (tsmode `80d621d...`, tsnogate/rbdiag receiver
  `3e12402...`, txout6 `056614d...`/`f0e1c5ab...` with receiver
  `ea2853bb...`/`49d22c3a...`, airdiad source `d5e98618...` with receiver
  `58301eee...`) are retained in the ModeA11-M17 result documents.

### 2026-09-24 session-state correction facts (retired snapshot)

- The ModeA4 "scored-onset correlation" branch was wrong: HILRX per-second
  lines log `t=<k_uptime/1000>` (uptime since boot), not time since
  streaming start; streaming began at uptime ~9 s. Corrected profile:
  delivery was never healthy (first nine events LOST, 23% LOST in the best
  second), degrades monotonically to zero over ~2.5 s of streaming, and
  stays zero. The "last valid seq 140 vs scored onset seq 144" coincidence
  was a correlation artifact; the supported mechanism is progressive ISO-AL
  strict-sequencing payload expiry under a completion-paced host. The
  ModeA4 result doc itself carries this correction note (appended
  2026-09-07).
- Historical evidence-preservation rules carried as dated facts (they are
  historical era-specific rules, not a current permission chain; current
  hardware authority lives in `AGENTS.md` and the milestones plan): the
  64 MHz baseline and passing direct-row IDs were not to be rerun or
  overwritten; run directories and external JUnit files must stay
  preserved; identities were reconfirmed with raw probe evidence before
  target-changing work; the immutable run IDs were never to be
  reflashed/overwritten/claimed; the HIL source build and `tests/hil`
  were not run concurrently (both touch `build/hil-source`).
- The 2/10 QoS versus 5/20 QoS comparison (2026-09-24 pre-fix probe): under
  2/10, six 30-second runs delivered about 2917 mono/Mode B and 2830
  per-channel Mode A valid SDUs with ongoing PLC; under 5/20, mono improved
  to 3002/3007 valid with losses largely at startup. That source-QoS change
  stayed unfinalized and was superseded by the later standard 48_4_1
  qualification rows recorded above.

### Method and boundary notes carried forward

- `system-hil-capture-software` foundation: exact qualification-field
  semantics (finite complete metric set, `accepted_by`, canonical UTC,
  external regular non-symlink referenced evidence, runner never
  auto-accepts) are preserved as the capture contract origin; the
  milestones plan carries the phase-level contract.
- RH0-era lock/evidence mechanics (O_EXCL lock token, fsync, foreign-token
  non-removal, forbidden output-root/run-ID/no-symlink rules) and the RH1A
  byte-span parser contract (no allocation, no input mutation, simple
  escapes, unsupported Unicode rejection, round-2 multiplication and
  long-unknown-key lexical precedence) are historical implementation-detail
  records for the runner/parser evolution; the current implemented runner
  and parser are the behavioral authority.
- Historical RH1B-era defaults (eight 768-byte output queue entries, 1 s
  queue-failure abort, shell wait-for-ever rationale) were superseded by
  review rounds (line capacity later 1024); do not treat them as current
  defaults.
- The sent-callback enqueue/transmit/flush meaning, the SDC-counter
  stream-creation/flush semantics, the ISO AL PDU-not-SDU and PHY encoding
  field meanings, NSE/BN/FT interpretation limits without retransmission
  proof, the NCS `can_recv` under `BT_AUDIO_TX` eligibility defect record,
  the local-128/`bt_hci_get_conn_handle` -ENOTCONN/SDC 02→-EIO
  discriminator set, the disabled-parent Kconfig assignment-failure
  disposition, and the 4 KiB ring/runtime allocation-filter rationale are
  preserved as dated diagnostic-era API/semantics findings; each named fix
  or result document governs its own boundary, and none is a global
  warning waiver.
- Mode B startup reservoir history: the 47619 Hz/110 ms gap rationale, the
  nrfx pointer-size queue versus PCM-slab distinction, the
  161668/163840-byte RAM with 0x7840 slab diagnostics, and the eleven-block
  startup reservoir are historical fixture-era evidence; the later product
  state is governed by `src/audio_sink.c` and the migration gates above.
- Offload tail history: the 8 ms serialized lock/submit-before-commit
  rationale and the zero-wait queued-drain ownership rationale are retained
  as dated design-rationale records; current behavior proof is the later
  offload/fallback test evidence.
- Session-end raw fallback line: the earliest-summary-per-slot segment rule
  and the consumed-tail/wrong-offset cause belong to the RH3c raw-scan
  design; the named RH3c result file does not exist and must not be
  invented; the RH3b 60-second-delay hypothesis is preserved (not final
  root cause).
- RH4 artifact amendment: the NCS v3.4.1 and exact role-resolved
  helper/session-manifest amendment in the RH4 artifact handoff is later
  than the PB-036-dated v3.3 slice and cannot be erased as its duplicate.
