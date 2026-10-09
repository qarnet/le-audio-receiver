# Historical RH3 matrix software status

> [!WARNING]
> Historical snapshot from 2026-08-22. Do not use it as current execution
> guidance. Terms such as "current" below refer to that checkpoint. Current RH3
> status and evidence are in
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status at that checkpoint: evidence-review and planning phase after direct Mode
A and current-image mono, Mode B, selected-layout Mode B, selected-layout mono,
selected-layout Mode A, and selected-layout Mode B 7.5 ms physical diagnostic
completion, with twenty live RH3 runs recorded: fifteen failed evidence, one
cancelled evidence, and four passed direct controls. No RH3, RH4, release, or
analog acceptance verdict had occurred at that checkpoint.

Selected CIS-layout telemetry was build-verified before physical diagnostics,
then flashed in receiver CPUAPP image
`d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`:
`bt iso quality` emits strict selected C-to-P CIS fields
`iso_interval_1250us`, `nse`, `cig_sync_us`, `cis_sync_us`, `c_max_pdu`,
`c_phy`, `c_bn`, `c_flush_1250us` as layout evidence only. Current receiver
image hashes are CPUAPP `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`
with FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Source hashes are unchanged:
`f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` and
`4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`.
Host-only verification remains: `audio_shell_nrf54` 45/45, RH2 154/154,
capture 19/19, both receiver target builds passed, and build contract 96/96.
The selected-layout preflight also passed `py_compile`, fixture validation, all
four image hashes, and `git diff --check`; four selected-layout hardware/HIL
direct diagnostics used this receiver image. No HIL-source build was executed.
Physical-run count is twenty (15 failed, one cancelled, four passed controls).
No QoS/controller/RF/threshold or acceptance claims are made from this telemetry
emission change.
Host J-Link fingerprints use current OpenOCD port-command syntax: `gdb port
disabled`, `tcl port disabled`, and `telnet port disabled`. The selected-layout
source-J-Link evidence contains no legacy `gdb_port`, `tcl_port`,
or `telnet_port` deprecation line. Old immutable RH3 evidence still retains its
recorded deprecation messages.

RH0/RH1 source review and focused software verification are complete. RH2/RH3
fake orchestration is complete. RH2 has one formal passing witness. RH3 runs
`rh3-20260815-01` through `rh3-20260815-04`, `rh3-20260815-06`,
`rh3-20260815-07`, `rh3-20260815-08`, and `rh3-20260820-01` are immutable failed
evidence, and `rh3-20260815-05` is immutable cancelled evidence. Direct
diagnostics `rh3-20260821-03-modea-depth-fix`,
`rh3-20260822-01-modea-tail-order-cleanup`,
`rh3-20260822-02-modea-iso-parser-fix`,
`rh3-20260822-03-modea-critical-tail-snapshot`, and
`rh3-20260822-04-modea-lifecycle-split`,
`rh3-20260822-09-modea-selected-layout`, and
`rh3-20260822-10-modeb-7p5-selected-layout` are immutable failed evidence, not
acceptance. Direct diagnostics
`rh3-20260822-05-current-image-mono-control`,
`rh3-20260822-06-current-image-modeb-control`,
`rh3-20260822-07-modeb-selected-layout`, and
`rh3-20260822-08-mono-selected-layout` are immutable passed control evidence,
not acceptance. `rh3-20260822-01-modea-tail-order-cleanup` failed at
receiver-tail ISO link-quality grammar validation after raw output contained
two records; `rh3-20260822-02-modea-iso-parser-fix` accepted that grammar and
failed later receiver-tail offload-state validation; the new
`rh3-20260822-03-modea-critical-tail-snapshot` retained a live ISO snapshot and
a post-teardown `STOPPED` FLPR snapshot, but failed the same strict
offload-state validation. `rh3-20260822-04-modea-lifecycle-split` retained the
full active-to-stopped evidence sequence but failed the strict log scan on one
receiver warning. This note does not mark a milestone accepted.

The host-only lifecycle split now records active FLPR offload after source
`streaming`, live CIS ISO quality at `scored_complete`, and terminal audio,
performance, stopped-offload, and handshake diagnostics after receiver stream
summaries. The direct Mode A and 7.5 ms Mode B diagnostics below are immutable
failed evidence;
the current-image mono, Mode B, selected-layout Mode B, and selected-layout mono
controls are immutable passed control evidence. These controls are not
acceptance.

## MA0/MA1 and SA0/SA1 capture software foundation (2026-08-14)

**Host-only software foundation implemented, not fixture-qualified or hardware
accepted.** Optional mono and stereo fixtures require strict direct-ALSA
bindings, external electrical-fixture metadata, and independently accepted
external qualification evidence before a capture command can reach hardware.
Capture uses direct `hw:CARD,DEV` only, fixed 48 kHz S16_LE format, frozen mixer
state validation, no shell argv, bounded SIGINT cleanup, retained partial WAV on
failure, and pure NumPy source-contract analysis. Synthetic fixtures exercise
clean signals and named defects only. They are not hardware limits and cannot
be reused for qualification.

Capture process review also closed the failed-stop ownership gap. A failed
normal stop remains retryable by the runner cleanup owner while `arecord` is
live; confirmed process exit makes cleanup idempotent; no validation failure
can promote a retained partial WAV. Synthetic clean paths now cover explicit
gain variation and distinct bounded leading offsets for mono and stereo.

`run-ma1-matrix` emits `MONO_OUTPUT_SMOKE_ACCEPTED` only after every fixed RH3
row and every mono capture oracle passes accepted external limits. It makes no
physical channel-order or stereo claim. `run-sa1-matrix` emits
`STEREO_OUTPUT_ACCEPTED` only after every fixed row and stereo capture oracle
passes accepted external limits. It proves functional stereo output, not
calibrated fidelity. Failed or cancelled matrices emit verdict `none`. No live
ALSA, USB, audio, serial, probe, flash, Bluetooth, or RF operation occurred for
this software work.

Host verification passed: capture model 5/5, analyzer 6/6, capture runner
19/19, RH4 artifact 11/11, `tests/hil/rh2_test.py` 153/153,
`tests/hil/rh3_matrix_test.py` 14/14, and native source app Twister 68/68.
`fw-build-hil-source` passed. Python compileall and `git diff --check` also
passed.

## Current RH3 software fixes

Host receiver-tail fix: a healthy 10 ms snapshot with exactly one pending FLPR
submit is re-polled up to `0.5 s`; the final validator still requires
`submit==success`.

Existing source Mode A enable fix: strictly serial enable remains. After stream
0 remote enabled state, stream 1 retries only synchronous `-EBUSY` at `10 ms`
intervals, for at most `1000 ms`, checks stop/fatal state before and after
waits, then waits for real enabled completion. Remote `stream_ops.enabled` does
not prove NCS local long-GATT write busy flag cleared.

Source CIS-connect serialization fix: `kick_stream_connect` now accepts one
stream index. The coordinator serializes Mode A separate-CIS connects: kick
stream 0, wait for real public `.connected` completion or error, then kick
stream 1. NCS `bt_bap_stream_connect()` submits one CIS and rejects another
pending CIS with `-EBUSY`; no public BAP batch connect exists. No private API is
used.

RH3-07 exact diagnosis: CIS 0 started, then CIS 1 failed establishment and
emitted `stream_disconnected_cb()`. The source ignored that failed-CIS callback,
so it waited for the full `HIL_SOURCE_OP_TIMEOUT_STREAM_MS` timeout and reported
`first_errno=-116`. The source-fixture correction records that deferred event as
a retryable completion. The coordinator worker checks stop/fatal state, sleeps
exactly 10 ms without `app_mutex`, and retries only the same stream once. A
second failed-CIS outcome returns `-EIO`, producing prompt normal error teardown
instead of timeout teardown. No HIL1 record or protocol field changed.

The correction is based only on public NCS v3.3.0 behavior: public
`bt_bap_stream_connect()` accepts an endpoint in `QOS_CONFIGURED` or `ENABLING`,
the unicast client reports failed CIS establishment through
`stream_ops.disconnected()` while leaving the BAP endpoint state intact, and
the ISO layer enters `BT_ISO_STATE_DISCONNECTED` before that callback. The
worker performs the deferred retry; callbacks do not call BAP/ISO APIs.

Mode A source-depth diagnostic correction: the fixed outstanding target changed
from two to three per active stream. The six host ISO TX buffers and six
controller ISO TX buffers remain unchanged, allowing two active Mode A streams
to use the existing six-buffer source pool while preserving lockstep pair
submission and completion backpressure. NCS v3.3.0 HCI IPC central ISO
configuration and BAP unicast client pool-fill behavior motivated this
unproven source-fixture change. The focused native regression proved exactly
three sends per stream, ledger order `0, 1, 0, 1, 0, 1`, and no fourth send
before a sent callback.

2026-10-08 handoff-reconciliation note (predecessor evidence preserved): the
retired source-depth correction handoff retained the exact predecessor
observations motivating target three, with its NCS v3.3.0 sample-file
references (historical, not current v3.4.1 facts):
`hil/source/app/prj.conf:14` set `CONFIG_BT_ISO_TX_BUF_COUNT=6`,
`hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf:16` set
`CONFIG_BT_CTLR_ISO_TX_BUFFERS=6`, the resolved CPUNET configuration
confirmed both were six with two ISOAL sources and two streams in one CIG,
`zephyr/samples/bluetooth/hci_ipc/nrf5340_cpunet_iso_central-bt_ll_sw_split.conf`
lines 34-38 documented the completed-packet pipeline requirement (host and
controller ISO TX buffer counts equal or greater; Number of Completed Packets
returned one ISO interval later), and
`zephyr/samples/bluetooth/bap_unicast_client/src/stream_tx.c:69-123`
documented the six-buffer pool-fill producer behavior. Recorded
predecessor-run numbers: `rh3-20260821-02-iso-link-quality-fix` completed all
source submissions and callbacks (12644 per stream, zero send failures) but
took `139.556 s` from source `streaming` to `scored_complete` versus the
`126.440 s` target; receiver SDC link quality reported
`rx_unreceived=14041` and `13968` for the two CISes with one CRC error total
and only 119 and 115 valid SDUs (about 99 percent PLC); the older
`rh3-20260815-08` Mode A row showed the same approximately `139.434 s`
source duration and about 99 percent PLC without any failed-CIS warning, so
that one transient warning could not explain the persistent loss. Source
sent callbacks remained controller completion credit only
(`zephyr/include/zephyr/bluetooth/iso.h:765-775`: completion may mean
enqueue, on-air transmission, or flush), never receiver-delivery proof.

Current source artifacts after the failed-CIS retry and Mode A source-depth
corrections are
software-verified only, not RH3 acceptance or hardware proof:

- CPUAPP (`build/hil-source/app/zephyr/zephyr.hex`): `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- CPUNET (`build/hil-source/hci_ipc/zephyr/zephyr.hex`): `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- merged app (`build/hil-source/merged.hex`): `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472`;
- merged CPUNET (`build/hil-source/merged_CPUNET.hex`): `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0`.

Native source app Twister result: `68/68` test cases passed with no test
configuration warnings. `fw-build-hil-source` passed. The target-three source
image later received eleven direct physical diagnostics, recorded below. Existing RH3
evidence remains immutable evidence, not acceptance, and no acceptance claim
follows.

## Physical RH3 evidence

### `rh3-20260815-01`

Immutable failed evidence. Fresh mono passed. Fresh Mode A failed before
streaming after QoS with `first_errno=-16`, caused by the original bulk enable
submission diagnosis.

### `rh3-20260815-02`

Immutable failed evidence. Aggregate: scheduled `20`, attempted `1`, passed
`0`, failed `1`, cancelled `0`; cleanup failures empty. Fresh mono failed only
the receiver tail. Live `flpr offload` snapshot was
`submit=12191 success=12190 fallback=0 busy=0`; this drove the host tail-settle
fix.

- Exact child:
  `/tmp/opencode/hil-runs/rh3-20260815-02.children.f5b5588ed7f5/rh3-20260815-02.p1.r1.rh3.fresh_mono_48_4_1.a165935bdb79/`

### `rh3-20260815-03`

Immutable failed evidence. Aggregate: scheduled `20`, attempted `2`, passed
`1`, failed `1`, cancelled `0`; cleanup failures empty. Fresh mono passed.
Fresh Mode A again failed before streaming at boundary `run row`, with
`state='teardown'; aborted=True; first_errno=-16`.

- Exact child:
  `/tmp/opencode/hil-runs/rh3-20260815-03.children.898a3f645e81/rh3-20260815-03.p1.r2.rh3.fresh_mode_a_48_4_1.c11079bcf135/`

This revealed state-callback versus local GATT busy-clear ordering. The source
retry fix is software-verified only, not hardware proof.

### `rh3-20260815-04`

Immutable failed evidence, not acceptance. Aggregate `/tmp/opencode/hil-runs/rh3-20260815-04`:
scheduled `20`, attempted `2`, passed `1`, failed `1`, cancelled `0`; cleanup
failures empty. Fresh mono passed. Fresh Mode A failed before streaming at
boundary `run row`, with `state='teardown'; aborted=True; first_errno=-16`.

- Exact failed child:
  `/tmp/opencode/hil-runs/rh3-20260815-04.children.9f096d25eb41/rh3-20260815-04.p1.r2.rh3.fresh_mode_a_48_4_1.a6773c928931/`
- Artifact evidence proves CPUAPP hash
  `fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049`.

Failure timing was after QoS. NCS source inspection identified synchronous
two-CIS `bt_bap_stream_connect()` submission as root cause. Runs `01` through
`04` are immutable failed evidence. Never retry, delete, overwrite, or claim
any of them as acceptance.

### `rh3-20260815-05`

Immutable cancelled evidence, not acceptance. Aggregate
`/tmp/opencode/hil-runs/rh3-20260815-05`: outcome `cancelled`; scheduled `20`,
attempted `1`, passed `0`, failed `0`, cancelled `1`; cleanup failures empty.

- Exact child:
  `/tmp/opencode/hil-runs/rh3-20260815-05.children.cb825a82f91c/rh3-20260815-05.p1.r1.rh3.fresh_mono_48_4_1.c5bd52eb286a/`
- Boundary/detail: `cancelled during source operation`.
- Child duration: `120.205855 s`.
- Child artifact confirms current source CPUAPP hash
  `fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049`.

Cancellation came from host executor default `120000 ms` command timeout, not a
device or firmware verdict. The timed-out host wrapper emitted no command exit
code. Before cancellation, source reached `streaming` with active
`first_errno=0`; controlled stop status was `sub=5092`, `sc=4948`, `sf=0`, then
`teardown` cause `stop` and terminal `fail`, as expected for host stop.

### `rh3-20260815-06`

Immutable failed evidence, not acceptance. Aggregate
`/tmp/opencode/hil-runs/rh3-20260815-06/`: scheduled `20`, attempted `1`,
passed `0`, failed `1`, cancelled `0`; cleanup failures empty. The first fresh
mono child failed at the receiver tail.

- Exact aggregate result:
  `/tmp/opencode/hil-runs/rh3-20260815-06/result.json`
- Exact child:
  `/tmp/opencode/hil-runs/rh3-20260815-06.children.54d1839d896c/rh3-20260815-06.p1.r1.rh3.fresh_mono_48_4_1.c90ec37a4bfc/`
- Failure detail: `invalid receiver status: I2S underruns=1; Stream resets=1; Push failures=1`

The retained receiver status recorded one `i2s_nrfx: Next buffers not supplied
on time`, maximum RX callback gap `268003 us`, and `8884` PLC frames. FLPR was
healthy with `submit=success=12650`, zero fallback, zero busy, and zero faults.
Source terminal status was `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`,
`out=0`. RH3-06 used the same source and receiver image identities as the
passing RH3-04 fresh mono child:

- source CPUAPP: `fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

### `rh3-20260815-07`

Immutable failed evidence, not acceptance. The fixed matrix command exited `1`.
Aggregate `/tmp/opencode/hil-runs/rh3-20260815-07/`: outcome `failed`,
scheduled `20`, attempted `2`, passed `1`, failed `1`, cancelled `0`; cleanup
failures empty. First failed boundary was exactly
`pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: run row`.

- Aggregate result:
  `/tmp/opencode/hil-runs/rh3-20260815-07/result.json`
- Aggregate JUnit:
  `/tmp/opencode/hil-runs/rh3-20260815-07.junit.xml`
- Passed child:
  `/tmp/opencode/hil-runs/rh3-20260815-07.children.0e29ddf55210/rh3-20260815-07.p1.r1.rh3.fresh_mono_48_4_1.a75b9a15fc1e/`
- Failed child:
  `/tmp/opencode/hil-runs/rh3-20260815-07.children.0e29ddf55210/rh3-20260815-07.p1.r2.rh3.fresh_mode_a_48_4_1.b68994d9598b/`
- Failed child JUnit:
  `/tmp/opencode/hil-runs/rh3-20260815-07.children.0e29ddf55210/rh3-20260815-07.p1.r2.rh3.fresh_mode_a_48_4_1.b68994d9598b.junit.xml`

Failed child boundary was `run row`. Exact failure detail was
`active status snapshot invalid: state='teardown'; aborted=True; first_errno=-116`.
Source records reached QoS, then source entered teardown with `cause=timeout`;
no later child ran. Both attempted children recorded the expected source and
receiver image hashes in `images.json`: source CPUAPP
`fea5ff443c0feaeaf9b1133c60a79c1884dc280714981ca3f88a91cec68a0049`, source
CPUNET `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`,
receiver CPUAPP
`4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`, and
receiver FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Runs `01` through `04`, `06`, and `07` remain immutable failed evidence, while
`05` is immutable cancelled evidence. Never retry, delete, overwrite, or claim
any of them as acceptance. Run `08` is also immutable failed evidence.

### `rh3-20260815-08`

The exact fixed two-pass matrix command ran once with run ID
`rh3-20260815-08` and exited `1`. Aggregate result is `failed`:
`scheduled=20`, `attempted=3`, `passed=2`, `failed=1`, `cancelled=0`, with
empty cleanup failures. First failed boundary is exactly
`pass1 row3 rh3.fresh_mode_b_48_4_1 stopped matrix: receiver tail`.

- Aggregate result:
  `/tmp/opencode/hil-runs/rh3-20260815-08/result.json`
- Aggregate JUnit:
  `/tmp/opencode/hil-runs/rh3-20260815-08.junit.xml`
- Aggregate evidence root:
  `/tmp/opencode/hil-runs/rh3-20260815-08/`
- Child evidence root:
  `/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/`
- Passed child 1:
  `/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r1.rh3.fresh_mono_48_4_1.329da491e528/`
- Passed child 2:
  `/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r2.rh3.fresh_mode_a_48_4_1.71387203ea9b/`
- Failed child:
  `/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/`
- Failed child JUnit:
  `/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9.junit.xml`

Failed child result records `receiver tail` and exact failure detail
`invalid receiver status: I2S underruns=1; Stream resets=1; Push failures=1;
offload submit/success mismatch`. Its `images.json` records:

- source CPUAPP: `b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Relevant receiver evidence in
`/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/receiver-status.txt`
records `i2s_nrfx: Next buffers not supplied on time`,
`i2s_nrfx: Cannot write in state: 4`,
`audio_i2s: I2S underrun, restarting DMA`, `I2S underruns : 1`,
`Stream resets : 1`, `Push failures : 1`, and offload snapshots
`submit=14223 success=14222`, `submit=14253 success=14252`, and
`submit=14278 success=14277`. Source records at
`/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/source-records.jsonl`
reached `streaming` and `scored_complete`, then recorded `sub=12320`,
`sc=12000`, `sf=0`, `cb=12320`, `out=0`, teardown cause `stop`, and terminal
verdict `fail`.

Aggregate and all three attempted child directories retain `result.json`,
JUnit, `MANIFEST.md`, and `SHA256SUMS`. External JUnit has `tests=20`,
`failures=1`, `skipped=17`, and `errors=0`. Review criteria are not met.
Physical RH3 acceptance is absent. Do not diagnose or retry from this record.

## Current standing lab hardware authority (2026-08-23)

The no-manual-hardware and matrix-runner-only wording in the following
historical `## Hardware execution controls` section described diagnostic
execution controls that applied when those records were created. It does not
constrain future work after the user's standing authorization.

Agents working in this repository may use any attached Nordic nRF development
board without fresh per-action or per-run approval. Permitted actions include
read/debug access, serial interaction, reset, flash, full erase/recovery where
target and tooling support it, DTR/RTS control, RF/Bluetooth testing, and
firmware replacement. Before any target-changing action, resolve identity with
`nrf-probes` or the appropriate project identity resolver and retain raw identity
evidence. Never rely on a static probe-to-board mapping or operate on unknown or
non-Nordic hardware.

`scripts/hil-runner.py` remains useful when its end-to-end evidence lifecycle is
needed, but it is not the only permitted hardware owner. Direct debugger,
serial, and board testing are allowed when they provide clearer diagnosis or
validation. Simulator or host-test failure is evidence, not automatic proof of
a production firmware defect. Where practical, evaluate the suspected failure
on a physical nRF board before accepting a behavior-changing source fix, then
retain both simulator and board evidence.

Preserve prior immutable run directories and evidence. Erasure or reflashing
does not authorize alteration of prior evidence. Existing central-only
pairing/streaming requirements remain unchanged unless a later plan deliberately
changes them. Row acceptance, warning handling, artifact integrity, and evidence
requirements remain unchanged.

Clarification for remaining status text:

- `matrix-runner-owned` describes required ownership for a formal matrix
  execution when that runner is used. It does not prohibit direct manual
  nRF-board diagnostics.
- References below to `approval` describe evidence and acceptance governance,
  not per-action permission to flash, erase, reset, read, or debug an attached
  nRF board.
- Direct diagnostics retain identity proof, raw evidence, immutable-evidence
  preservation, and central-only requirements.

## Hardware execution controls

Matrix runner solely owns hardware. Do not use manual serial, flashing, reset,
or FLPR work, and do not rebuild source during execution. Do not retry RH3-07,
RH3-08, RH3-09, or direct diagnostics
`rh3-20260821-03-modea-depth-fix`,
`rh3-20260822-01-modea-tail-order-cleanup`,
`rh3-20260822-02-modea-iso-parser-fix`, or
`rh3-20260822-03-modea-critical-tail-snapshot`,
`rh3-20260822-04-modea-lifecycle-split`, or
`rh3-20260822-07-modeb-selected-layout`, or
`rh3-20260822-08-mono-selected-layout`, or
`rh3-20260822-10-modeb-7p5-selected-layout`, and do not launch another matrix from
this phase. Do not reuse IDs `01` through `09`.
Do not edit `STATUS.md` or claim RH3, RH4, release, or analog acceptance.

## External acceptance inputs still required

- Remaining RH3 live transport work remains gated on the real gitignored physical binding,
  resolved probe/serial identities, current-source hash preflight,
  matrix-runner-owned flash/reset/radio execution, and a new reviewed evidence-backed
  plan before execution.
- RH4 requires the same live binding and approval plus one immutable receiver
  FR1 factory ZIP and one deterministic HIL-source ZIP accepted by the strict
  artifact resolver. Local-build evidence cannot substitute for RH4.
- MA0 requires an electrically reviewed passive mono summing, attenuation, and
  DC-blocking network; exact DAC/capture hardware and USB identity; frozen
  direct-ALSA endpoint and mixer state with AGC off; at least two independent
  130-second WAV/analyzer evidence pairs; human-approved numerical limits; and
  a canonical external qualification JSON binding all hashes and identities.
- MA1 requires that accepted MA0 qualification plus the real mono binding and
  explicit approval for the fixed two-pass live matrix. Only a complete pass
  may emit `MONO_OUTPUT_SMOKE_ACCEPTED`.
- SA0 requires simultaneous stereo line-capture hardware, electrical review,
  exact stereo binding/mixer state, injected-defect qualification for mapping,
  separation, duplication, leakage, level, continuity, clipping, truncation,
  drift, and dropout, plus at least two independent 130-second evidence pairs
  and human-approved frozen limits in canonical external qualification JSON.
- SA1 requires that accepted SA0 qualification plus explicit approval for the
  fixed two-pass live stereo matrix. Only a complete pass may emit
  `STEREO_OUTPUT_ACCEPTED`.

No host-only result emits `TRANSPORT_RUNTIME_ACCEPTED`,
`MONO_OUTPUT_SMOKE_ACCEPTED`, `STEREO_OUTPUT_ACCEPTED`, or
`SYSTEM_AUDIO_ACCEPTED`.

Pristine `fw-build-hil-source` resolved the CPUNET quiet-console settings with
no compiler or assigned-value diagnostic. NCS still reports its existing
informational `__ASSERT()` notice and the SW Split ISO notices for
`BT_LL_SW_SPLIT`, `BT_CTLR_SET_HOST_FEATURE`, `BT_CTLR_CENTRAL_ISO`, and
`CONFIG_BT_CTLR_ADVANCED_FEATURES=y`. The checked-in central ISO hci_ipc base
configuration requires those settings. They are build-configuration notices,
not runtime HIL evidence.

## Physical RH3 run `rh3-20260820-01`

Immutable failed evidence, not acceptance. The exact fixed RH3 schedule stopped
after the fresh mono and fresh Mode A rows. Aggregate and child evidence are:

```text
aggregate: /tmp/opencode/hil-runs/rh3-20260820-01/
children:  /tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/

outcome=failed
scheduled=20 attempted=2 passed=1 failed=1 cancelled=0
cleanup_failures=[]
first_failed_boundary=pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: receiver tail
```

Passed fresh-mono child:

```text
/tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/
  rh3-20260820-01.p1.r1.rh3.fresh_mono_48_4_1.82541559ac27/
```

Failed fresh Mode A child:

```text
/tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/
  rh3-20260820-01.p1.r2.rh3.fresh_mode_a_48_4_1.ce1046d1e077/
```

Both attempted children recorded these exact image identities:

- source CPUAPP: `b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6d9caa89d272059858c2fce874776fa01b55ac0dfd8364a7ade8ddc43e7fe4cf`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The strict receiver-tail failure recorded one slab-full warning:

```text
[00:26:51.063,809] <wrn> audio_i2s: I2S slab full — dropping frame

I2S underruns  : 1
Stream resets  : 0
Push failures  : 1
RX callback gap: 9234 us (max)
I2S write gap  : 13712 us (max)
offload submit=14040 success=14040 fallback=0
```

Source reached `streaming`; both Mode A streams completed `sc=12000` with
`sf=0`. FLPR remained healthy with `submit=14040`, `success=14040`, and
`fallback=0`. The fixed schedule stopped before fresh Mode B. RH3-09 is
immutable failed evidence. Do not retry, reuse, overwrite, or claim RH3
acceptance from this run. The proposed post-start slab-backpressure repair has
no hardware proof.

2026-10-08 handoff-reconciliation note (nrfx evidence preserved): the retired
slab-backpressure handoff owned the historical descriptor-versus-memory
evidence for this failure, with exact NCS v3.3.0 source references
(historical, not current v3.4.1 facts):
`zephyr/drivers/i2s/Kconfig.nrfx` defines `CONFIG_I2S_NRFX_TX_BLOCK_COUNT` as
a descriptor-queue length; `zephyr/drivers/i2s/i2s_nrfx.c:503-564` transfers
slab-block ownership only after a successful `k_msgq_put()`;
`start_transfer()` at `:567-617` removes the first descriptor but keeps its
slab block in `last_tx_buffer`, so descriptor dequeue does not free PCM
memory; `data_handler()` at `:239-293` frees a completed TX block only after
nrfx reports it released; and `zephyr/include/zephyr/kernel.h:5833-5855`
specifies `k_mem_slab_alloc()` (`K_NO_WAIT` returns `-ENOMEM`, an expired
finite wait returns `-EAGAIN`). The handoff's sizing rationale: the first
post-start output can consume block 16 before any DMA release, so the second
output hits the existing no-wait allocation and drops; the reservoir was
sized to cover RH3-08's `142362 us` Mode B callback gap plus render work
(about 150 ms), so startup depth must not shrink; and the nRF54L15 build used
`161668/163840` bytes of RAM, so no extra 1924-byte PCM slab block was added.
The stable finite post-start wait / startup-and-repeat non-wait / ownership
behavior is carried by the I2S-002/I2S-003/I2S-006 contracts in
`docs/testing/behavior-contract.md`.

## Physical RH3 direct diagnostic `rh3-20260821-03-modea-depth-fix`

Immutable direct Mode A diagnostic, not acceptance. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260821-03-modea-depth-fix/`. Evidence integrity
passed `23/23` SHA-256 entries.

The strict receiver-tail failure was caused by the post-teardown `bt iso
quality` query being unavailable. Source records still reached
`scored_complete` at `160547 ms`, teardown at `164611 ms`, and a PASS terminal
at `165419 ms`. Cleanup recorded `command_id='parse-error' run_id='unbound'`,
an unresolved host-cleanup diagnostic, not a firmware root-cause claim.

Receiver loss remained high: slot 0 `rx_valid=137`, `rx_lost=14310`; slot 1
`rx_valid=135`, `rx_lost=14249`. This earlier run was one physical diagnostic
execution, but no acceptance result. A later lifecycle-split direct diagnostic
is recorded below; this earlier run remains non-acceptance.

2026-10-08 handoff-reconciliation note (input provenance preserved): the
retired depth-fix physical handoff pinned its exact four-image local-build
input tuple, to be matched before any hardware action and never rebuilt:
source CPUAPP `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`,
source CPUNET `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`,
receiver CPUAPP `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`,
receiver FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Matching identities proved reviewed local images only; they did not prove
RH3, transport, audio, analog, release, or root-cause acceptance. The
software phase behind this row had passed native source-app Twister `68/68`
and `fw-build-hil-source` with no hardware result yet. The named state
report records the failed receiver-tail query and severe loss; the planned
direct diagnostic must never be represented as successful acceptance, and
source completion callbacks remained credit, not delivery.

## RH3 direct runner diagnostic `rh3-20260822-01-modea-tail-order-cleanup`

Immutable direct runner diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup.junit.xml`.
`result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first boundary
`receiver tail`, exact detail
`invalid ISO link quality: ISO link quality grammar malformed`, and
`cleanup_failures=[]`. No runner process exit code is retained.
`SHA256SUMS` verification passed `23/23`.

The reviewed image hashes were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Source lifecycle reached `streaming`, `scored_complete`, `teardown`, terminal
PASS, and final `idle`. Stop status recorded both Mode A streams as
`sub=12644 sc=12000 sf=0 cb=12644 out=0`. The earlier active status recorded
stream 0 as `sub=24 sc=0 sf=0 cb=22 out=2` and stream 1 as
`sub=23 sc=0 sf=0 cb=22 out=1`. No parse-error/unbound cleanup record occurred.

The receiver query order was corrected in the retained evidence:
`bt iso quality` ran before audio/offload status and before stream disable.
Raw output was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14323 retransmitted=0 crc_error=0 rx_unreceived=14317 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14250 retransmitted=0 crc_error=0 rx_unreceived=14241 duplicate=0
```

Both records contain all seven counters, but the runner rejected the grammar
and stopped at `receiver tail`. This is a factual parser/result record, not a
root-cause conclusion or acceptance result. Receiver runtime also logged one
transient warning at `[00:31:31.290,950]`:
`<wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?`

Receiver startup recorded CIS 0 and CIS 1. No slot-specific callback summary
was emitted. Aggregate performance recorded `iso_recv=28754`,
`lc3_decode=28740`, `sink_push=14369`, zero push/write failures, maximum RX
callback gap `10112 us`, and maximum I2S write gap `11769 us`. FLPR offload
recorded `submit=14370 success=14370 fallback=0` with all faults and recovery
counters zero. FLPR handshake recorded Ready, ACKed, and Healthy `yes`, with
all error and RX-loss counters zero. Strict ISO loss remained high, with raw
`rx_unreceived=14317` and `rx_unreceived=14241`.

The retained `audio status` snapshot reported `Frames decoded=0`, `PLC=0`,
decode errors `0`, I2S underruns `0`, stream resets `0`, empty SDUs `0`, drift
state `INIT`, drift ppm `0`, and resampler `ASRC linear`. The separate aggregate
performance snapshot above retained the receive/decode/push counts.

The retained flash logs reported these OpenOCD warnings: receiver extra erase
range `0x01023afc .. 0x01023fff`; source extra erase ranges
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`. The source probe
scan also emitted the existing `DEPRECATED! use 'gdb port', not 'gdb_port'`,
`DEPRECATED! use 'tcl port', not 'tcl_port'`, and
`DEPRECATED! use 'telnet port', not 'telnet_port'` messages.

No assertion, source protocol error, or runner cleanup failure was recorded.
Physical RH3 acceptance remains absent. Parser repair is host-only, and no
physical retry occurs in this phase. Do not retry this diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-02-modea-iso-parser-fix`

Immutable direct Mode A diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix.junit.xml`.
`result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first boundary
`receiver tail`, exact detail `invalid receiver status: offload
state='STOPPED'`, and `cleanup_failures=[]`. No runner process exit code is
retained in this evidence root; do not infer one from later read-only commands.
Evidence integrity passed `23/23` SHA-256 entries.

Sequential preflight passed: both new output paths were absent, validation
returned `{"capture_capability": "none", "fixture_id":
"local-nrf54l15-receiver"}`, all four image hashes matched, and
`git diff --check` passed. Image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The retained receiver query ran at `receiver-status.txt` lines 75-78, before
stream disable. The repaired parser
accepted the real interleaved transcript with `header_seen=true`,
`malformed=false`, exactly two records, and zero validation errors:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14302 retransmitted=0 crc_error=0 rx_unreceived=14293 duplicate=1
  Stream[1] handle=0x0002 tx_unacked=0 tx_flushed=0 tx_last_subevent=14381 retransmitted=0 crc_error=89 rx_unreceived=14387 duplicate=12
```

The parser failure did not recur. Later receiver-tail validation failed only
because the final `flpr offload` snapshot reported `STOPPED / epoch=0 gen=3`.
That snapshot still had submit/success `14375/14375`, fallback/busy `0/0`, and
zero listed faults and recovery counters.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal PASS, and final
`idle`. Final idle status retained `seq=12644` and reset stream counters to
`sub=0 sc=0 sf=0 cb=0 out=0`. Active status recorded stream 0
`seq=17 sub=17 sc=0 sf=0 cb=17 out=0` and stream 1
`seq=17 sub=17 sc=0 sf=0 cb=15 out=2`.

Receiver final summaries were:

```text
Stream[0]: SDUs=130 decoded=28750 plc=28620 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=130 rx_error=0 rx_lost=14289 rx_unknown=0 rx_no_ts=50
Stream[1]: SDUs=0 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=0 rx_error=0 rx_lost=14389 rx_unknown=0 rx_no_ts=14389
```

The post-stop audio status had zero decoded/PLC/error, underrun, reset, and
empty-SDU counters, drift `INIT`, ppm `0`, and resampler `ASRC linear`. Audio
performance retained `iso_recv=28764`, `lc3_decode=28750`, `sink_push=14374`,
zero push/I2S write failures, maximum RX callback gap `10000 us`, and maximum
I2S write gap `10635 us`. FLPR handshake reported Ready/ACKed/Healthy all
`yes`, with handshake and RX loss/duplicate/out-of-order/missed counters zero.

Read-only scan facts: source warning hits `0`, source HIL1 protocol errors `0`,
receiver raw warning hits `0`, receiver shell-error hits `0`, and no runtime
assertion line occurred. Flash evidence retained source OpenOCD extra-erase-
range warnings and source probe-scan deprecation messages. High ISO loss is
retained without attribution: `rx_unreceived=14293` and `14387`, with
`crc_error=89` on slot 1. This result is not RH3 acceptance, an audio-health
claim, or a root-cause conclusion. Do not retry this diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-03-modea-critical-tail-snapshot`

Immutable direct Mode A diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot.junit.xml`.
The retained `environment.json` records runner command status `0`. Retained
`result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first boundary
`receiver tail`, exact detail `invalid receiver status: offload
state='STOPPED'`, and `cleanup_failures=[]`. `SHA256SUMS` verification passed
`23/23` entries.

The sequential preflight passed: output paths were absent, fixture validation
returned `{"capture_capability": "none", "fixture_id":
"local-nrf54l15-receiver"}`, all four image hashes matched, and
`git diff --check` passed:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Receiver evidence retained a live `bt iso quality` query followed by
post-teardown `flpr offload`, `audio status`, `audio perf`, and `flpr status`
diagnostics. The post-stop offload counters were equal, so the old settle
outcome was `equal` with `0` retries. The ISO parser retained the exact header
and two stream records:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14328 retransmitted=0 crc_error=2 rx_unreceived=14322 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14257 retransmitted=0 crc_error=0 rx_unreceived=14248 duplicate=1
```

The post-teardown offload snapshot was `STOPPED / epoch=0 gen=3`, with
`submit=14377 success=14377 fallback=0 busy=0`. All listed fault counters,
recovery counters, and probation counters were `0`; RTT was
min/max/avg `1827/2072/1863 us`, `n=14377`, and FLPR cycles were
min/max/avg `977/1019/986`, `n=14377`. FLPR handshake was Ready/ACKed/Healthy
`yes`, with handshake and RX sequence error counters `0`. Startup had earlier
logged offload prep `epoch=1626819632 gen=2 state=ACTIVE`; the retained
post-stop snapshot was `STOPPED / epoch=0 gen=3`. Strict validation failed on
that state, not on ISO grammar.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active status had both streams at `seq=25 sub=25 sc=0 sf=0 cb=24
out=1`. Stop status had both at `seq=12644 sub=12644 sc=12000 sf=0 cb=12644
out=0`; final idle reset the single idle stream counters to zero. No active
capture ran because `capture_capability=none`. No `summary.json` or receiver
stream-summary records were retained because receiver-tail validation stopped
first.

Post-stop audio status reported decoded/PLC/decode-error/I2S-underrun/reset/
empty-SDU counters all `0`, drift `INIT`, ppm `0`, resampler `ASRC linear`, and
volume `195/255`. Aggregate performance recorded `iso_recv=28768`,
`lc3_decode=28754`, `volume=14377`, `sink_push=14376`, ASRC `0`, slab free
`0/3`, output frames `476/477`, output blocks `14376`, push failures `0`, I2S
write failures `0`, DMA restarts `0`, maximum RX callback gap `10131 us`, and
maximum I2S write gap `11143 us`.

Runtime warning evidence retained:

```text
[00:27:06.510,949] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
```

No runtime assertion, shell error, source warning, or source HIL1 protocol error
was recorded. Source flash logs retained extra erase-range warnings at
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`; source probe scan
retained the existing `gdb_port`, `tcl_port`, and `telnet_port` deprecation
messages. High ISO loss remains unqualified evidence: `rx_unreceived=14322`
and `14248`, with `crc_error=2` on stream 0 and `duplicate=1` on stream 1.
This diagnostic is not RH3 acceptance, an audio-health claim, or a root-cause
conclusion. Do not retry this diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-04-modea-lifecycle-split`

Immutable direct Mode A diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split.junit.xml`.
Retained `environment.json` records direct runner command status `0`.
Retained `result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first
boundary `log scan`, receiver warning detail, and `cleanup_failures=[]`.
`SHA256SUMS` verification passed `26/26` entries.

Sequential preflight passed: output paths were absent, `rh2_test.py` reported
`153` tests OK, `capture_runner_test.py` reported `19` tests OK, `py_compile`
passed, fixture validation returned `{"capture_capability": "none",
"fixture_id": "local-nrf54l15-receiver"}`, all four image hashes matched, and
`git diff --check` passed. The image hashes were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Active FLPR was `ACTIVE / epoch=422562923 gen=2`; counters moved from
`submit=111 success=110` to `submit=131 success=131` after one settle retry,
with outcome `equal`. Fallback, busy, listed faults, recovery, exhaustion, and
probation counters were `0`. Post-stop FLPR was `STOPPED / epoch=0 gen=3`,
`submit=14370 success=14370 fallback=0 busy=0`, with the same zero fault,
recovery, exhaustion, and probation counters. Structured optional heartbeat
and runtime counters were retained as `hb_dedup=-1`, `runtime_fails=-1`,
`runtime_last_ms=-1`, and `runtime_restarts=-1`.

Receiver command order was active `flpr offload`, settle `flpr offload`, live
`bt iso quality`, then post-stop `audio status`, `audio perf`, `flpr offload`,
and `flpr status`. Live ISO quality retained exactly two records:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14314 retransmitted=0 crc_error=0 rx_unreceived=14308 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14242 retransmitted=0 crc_error=0 rx_unreceived=14233 duplicate=0
```

Post-stop `audio status` counters for decoded, PLC, decode errors, I2S
underruns, stream resets, and empty SDUs were all `0`; drift was `INIT`, ppm
`0`, and resampler `ASRC linear`. `audio perf` retained `iso_recv=28754`,
`lc3_decode=28740`, `volume=14370`, `sink_push=14369`, ASRC `0`, push/I2S/DMA
failures `0`, slab free `0/3`, output frames `476/477`, output blocks `14369`,
maximum RX callback gap `10127 us`, and maximum I2S write gap `11769 us`.
FLPR handshake was Ready/ACKed/Healthy `yes`, handshake epoch `2560148019`
(`ready=1 reboot=1`), and all handshake error and RX sequence counters were
`0`.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and `idle`.
Terminal source status recorded both Mode A streams at `sub=12644 sc=12000
sf=0 cb=12644 out=0`. Receiver summaries retained `rx_lost=14310` and
`rx_lost=14249`; live ISO quality retained `rx_unreceived=14308` and
`rx_unreceived=14233`. These loss counters remain unqualified evidence.

The exact retained runtime warning was
`[00:42:49.737,888] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?`.
Source warnings, source protocol errors, shell errors, and cleanup failures
were empty. Source flash extra erase-range warnings and probe-scan port-name
deprecation messages remain tool-log facts only. Lifecycle boundary evidence
was retained through stopped diagnostics; overall row result remains failed at
`log scan`. No RH3, hardware, audio, release, product, or root-cause
conclusion follows. Do not retry this diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-05-current-image-mono-control`

Immutable direct current-image mono control, not a matrix run and not
acceptance. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control.junit.xml`.
The direct runner command exited `0`. Retained `result.json`, JUnit, and
`MANIFEST.md` record outcome `passed`, `first_failed_boundary=null`,
`failure_detail=null`, and `cleanup_failures=[]`. JUnit records one test with
zero failures. Read-only `SHA256SUMS` verification passed all `26/26` entries.

The sequential preflight passed: both output paths were absent, `rh2_test.py`
reported `153` tests OK, `capture_runner_test.py` reported `19` tests OK,
`py_compile` passed, fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. The
image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Active FLPR was `ACTIVE / epoch=653787680 gen=2`. The first snapshot was
`submit=45 success=44 fallback=0 busy=0`; one settle retry ended at
`submit=65 success=65`, with settle outcome `equal` and `retries=1`. Active
fault, recovery, exhaustion, and probation counters were `0`. Post-stop FLPR
was `STOPPED / epoch=0 gen=3`, with `submit=12656 success=12656 fallback=0
busy=0`; all listed faults, recovery, exhaustion, and probation counters were
`0`. Optional structured fields retained `hb_dedup=-1`, `remote_epoch=-1`,
`runtime_fails=-1`, `runtime_last_ms=-1`, and `runtime_restarts=-1`.

The live ISO quality header and sole stream record were:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=9 retransmitted=0 crc_error=4 rx_unreceived=9 duplicate=0
```

The sole receiver summary retained `SDUs=12644`, `decoded=12656`, `plc=12`,
`decode_err=0`, `i2s_underrun=0`, `stream_reset=0`, `empty_sdu=0`,
`rx_valid=12644`, `rx_error=0`, `rx_lost=12`, `rx_unknown=0`, and `rx_no_ts=9`.
Post-stop `audio status` had decoded, PLC, decode-error, I2S-underrun,
stream-reset, and empty-SDU counters all `0`, drift `INIT`, ppm `0`, and
resampler `ASRC linear`. Performance retained `iso_recv=12656`,
`lc3_decode=12656`, `volume=12656`, `sink_push=12655`, and ASRC `0`.
Push, ASRC-capacity, I2S-write, and DMA-restart failures were all `0`; slab
free was `1/8`, output frames `476/478`, output blocks `12655`, maximum RX
callback gap `80220 us`, maximum I2S write gap `80144 us`, and maximum I2S
write time `22 us`.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active status recorded the sole stream as `seq=18 sub=18 sc=0 sf=0
cb=16 out=2`. Terminal status recorded `seq=12644 sub=12644 sc=12000 sf=0
cb=12644 out=0`, with `first_errno=0`, `security_error=0`, and
`disconnect_reason=0`. The final idle record retained `seq=12644` and reset
`sub=0 sc=0 sf=0 cb=0 out=0`.

FLPR handshake was Ready/ACKed/Healthy `yes`, epoch `2792268502`
(`ready=1 reboot=1`), with `len=0 ver=0 unk=0 send=0` and RX lost, duplicate,
out-of-order, and missed counters all `0`. Source warning hits, source HIL1
protocol errors, receiver runtime warning hits, receiver shell errors, and
assertion lines were all `0`. Cleanup failures were empty. Source flash logs
retained OpenOCD extra-erase-range warnings at `0x01023afc .. 0x01023fff` and
`0x0005741c .. 0x00057fff`; source probe evidence retained the existing
`gdb_port`, `tcl_port`, and `telnet_port` deprecation messages. These are
tool-log facts, not runtime receiver warnings or row failures. PCLK timing
diagnostics were informational, ranging from `1808` to `2293 ppm`.

Bounded comparison with RH3-04: this mono control passed strict row validation,
retained no runtime warning, and showed one stream with
`rx_unreceived=9`/`rx_lost=12` and `crc_error=4`. RH3-04 Mode A failed log scan
with the retained receiver warning and two-stream ISO loss of
`rx_unreceived=14308` and `14233`, with summary `rx_lost=14310` and `14249`.
Observed mono symptoms therefore differ materially from RH3-04 Mode A, while
mono still has nonzero loss and is not a zero-loss or audio-acceptance result.
This is bounded control evidence only. It establishes no RF, controller,
firmware, source, receiver, audio, or root-cause conclusion, and does not claim
RH3, hardware, audibility, release, product, or analog acceptance. Do not retry
this diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-06-current-image-modeb-control`

Immutable direct current-image Mode B control, not a matrix run and not
acceptance. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control.junit.xml`.
The direct runner command exited `0`. Retained `result.json`, JUnit, and
`MANIFEST.md` record outcome `passed` for
`rh3.fresh_mode_b_48_4_1`, `first_failed_boundary=null`,
`failure_detail=null`, and `cleanup_failures=[]`. JUnit records one test with
zero failures. Evidence integrity passed `26/26` SHA-256 entries.

The sequential preflight passed: both output paths were absent,
`rh2_test.py` reported `153` tests OK, `capture_runner_test.py` reported `19`
tests OK, `py_compile` passed, fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. The
image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The receiver retained one Mode B ASE with `chan_count=2`. Its QoS record was
`interval=10000`, `sdu=240`, `rtn=5`, `latency=20`, and `pd=40000`. Active FLPR
was `ACTIVE / epoch=1683906386 gen=2`. The first snapshot was
`submit=84 success=83`; one settle retry ended at `submit=104 success=104`,
with settle outcome `equal` and `retries=1`. Active fallback, busy, listed
fault, recovery, exhaustion, and probation counters were `0`. Post-stop FLPR
was `STOPPED / epoch=0 gen=3`, with `submit=13704 success=13704`,
`fallback=0`, and `busy=0`; listed faults, recovery, exhaustion, and probation
counters remained `0`. Optional heartbeat and runtime fields were retained as
`hb_dedup=-1`, `remote_epoch=-1`, `runtime_fails=-1`, `runtime_last_ms=-1`,
and `runtime_restarts=-1`.

The live ISO quality header and sole stream record were:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=11358 retransmitted=0 crc_error=2 rx_unreceived=13267 duplicate=0
```

The sole receiver summary was:

```text
Stream[0]: SDUs=113 decoded=27408 plc=27182 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=113 rx_error=0 rx_lost=13591 rx_unknown=0 rx_no_ts=9
```

Thus retained stream values were `rx_valid=113`, `rx_lost=13591`,
`rx_error=0`, `rx_no_ts=9`, `decoded=27408`, and `PLC=27182`. Post-stop audio
status had decoded, PLC, decode-error, I2S-underrun, stream-reset, and
empty-SDU counters all `0`, drift `INIT`, ppm `0`, and resampler `ASRC
linear`. Performance retained `iso_recv=13704`, `lc3_decode=27408`,
`volume=13704`, `sink_push=13703`, and ASRC `0`; push, ASRC-capacity,
I2S-write, and DMA restart failures were all `0`. Slab free was `1/8`, output
frames `476/477`, output blocks `13703`, maximum RX callback gap `80226 us`,
maximum I2S write gap `80142 us`, and maximum I2S write time `27 us`.

FLPR handshake was Ready/ACKed/Healthy `yes`, epoch `3822386881`
(`ready=1 reboot=1`), with `len=0 ver=0 unk=0 send=0` and RX lost, duplicate,
out-of-order, and missed counters all `0`. Source lifecycle reached
`configured`, `connecting`, `secured`, `discovered`, `qos`, `streaming`,
`scored_complete`, `teardown`, terminal `PASS`, and final `idle`. Active sole
stream status was `seq=17 sub=17 sc=0 sf=0 cb=16 out=1`. Terminal status was
`seq=12644 sub=12644 sc=12000 sf=0 cb=12644 out=0`, with
`first_errno=0`, `security_error=0`, and `disconnect_reason=0`. Final idle
retained `seq=12644` and reset `sub=0 sc=0 sf=0 cb=0 out=0`.

Source warning hits, source HIL1 protocol errors, receiver runtime warning
hits, receiver shell errors, and assertion lines were all `0`. No runtime
receiver warning was retained. Source flash logs retained OpenOCD extra
erase-range warnings at `0x01023afc .. 0x01023fff` and
`0x0005741c .. 0x00057fff`; source probe evidence retained the existing
`gdb_port`, `tcl_port`, and `telnet_port` deprecation messages. These are
tool-log facts, not runtime receiver warnings or row failures. No active audio
capture or audibility observation occurred because `capture_capability=none`.
PCLK timing diagnostics were informational, ranging from `446` to `632 ppm`.

Bounded comparison with RH3-04 Mode A: current Mode B evidence is materially
similar in severe transport loss, with one stream at
`rx_unreceived=13267`/`rx_lost=13591` versus Mode A records at
`rx_unreceived=14308` and `14233`, with summary `rx_lost=14310` and `14249`.
Mode A retained a receiver warning; Mode B retained no runtime warning and no
receiver audio fault counters. Bounded comparison with RH3-05 mono: current
Mode B evidence differs materially from the low-loss mono control, which had
`rx_unreceived=9`/`rx_lost=12` and `crc_error=4`. These are bounded control
observations only. They establish no RF, controller, firmware, source,
receiver, audio, or root-cause conclusion, and do not claim RH3, hardware,
audibility, release, product, or analog acceptance. Do not retry this
diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-07-modeb-selected-layout`

Immutable direct selected-layout Mode B diagnostic, not a matrix run and not
acceptance. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout/`. External JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout.junit.xml`.
The exact row was `rh3.fresh_mode_b_48_4_1`. The direct runner command exited
`0`. Retained `result.json`, JUnit, and `MANIFEST.md` record outcome `passed`,
`first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`. JUnit records one test with zero failures. Read-only
`SHA256SUMS` verification passed all `26/26` entries.

The sequential preflight passed: both output paths were absent, `rh2_test.py`
reported `153` tests OK, `capture_runner_test.py` reported `19` tests OK,
`py_compile` passed, fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. The
reviewed image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP telemetry image: `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The receiver retained one Mode B ASE with `chan_count=2`. Its QoS record was
`interval=10000`, `framing=0x00`, `phy=0x02`, `sdu=240`, `rtn=5`, `latency=20`,
and `pd=40000`.

The live ISO link-quality record was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=11358 retransmitted=0 crc_error=1 rx_unreceived=13267 duplicate=0 iso_interval_1250us=8 nse=6 cig_sync_us=8184 cis_sync_us=8184 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=8
```

The selected C-to-P layout values were `iso_interval_1250us=8`, `nse=6`,
`cig_sync_us=8184`, `cis_sync_us=8184`, `c_max_pdu=240`, `c_phy=2`, `c_bn=1`,
and `c_flush_1250us=8`. Existing link-quality counters were
`handle=0x0001`, `tx_unacked=0`, `tx_flushed=0`, `tx_last_subevent=11358`,
`retransmitted=0`, `crc_error=1`, `rx_unreceived=13267`, and `duplicate=0`.

The sole receiver stream summary retained `SDUs=113`, `decoded=27408`,
`PLC=27182`, `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`,
`empty_sdu=0`, `rx_valid=113`, `rx_error=0`, `rx_lost=13591`,
`rx_unknown=0`, and `rx_no_ts=9`. The requested summary values are therefore
`rx_valid=113`, `rx_lost=13591`, `rx_error=0`, `rx_no_ts=9`,
`decoded=27408`, and `PLC=27182`.

Active FLPR was `ACTIVE / epoch=90801254 gen=2`. The first snapshot was
`submit=75 success=75 fallback=0 busy=0`; settle outcome was `equal` with
`initial=75/75`, `final=75/75`, and `retries=0`. Active timeout, full, stale,
sequence, frame, CRC, payload, recovery, exhaustion, probation, and busy/fallback
counters were all `0`. Active top-level RTT was `min=1827 us max=1959 us
avg=1849 us n=75`; ASRC offload retained `submit=79 success=79 fallback=0`,
all timeout/full/stale/sequence/frame/CRC/state/verify faults `0`, RTT
`min=1827 us max=1959 us avg=1849 us n=79`, and FLPR cycles
`min=985 max=1019 avg=998 n=79`.

Post-stop FLPR was `STOPPED / epoch=0 gen=3`, with
`submit=13704 success=13704 fallback=0 busy=0`. All listed timeout, full,
stale, sequence, frame, CRC, payload, recovery, exhaustion, and probation
counters were `0`. ASRC offload also retained `submit=13704 success=13704
fallback=0`, with timeout/full/stale/sequence/frame/CRC/state/verify faults all
`0`; RTT was `min=1821 us max=2084 us avg=1844 us n=13704`, and FLPR cycles
were `min=977 max=1019 avg=989 n=13704`. Optional structured fields were
`hb_dedup=-1`, `remote_epoch=-1`, `runtime_fails=-1`, `runtime_last_ms=-1`,
and `runtime_restarts=-1`.

Post-stop audio status reported decoded, PLC, decode-error, I2S-underrun,
stream-reset, and empty-SDU counters all `0`, drift `INIT`, ppm `0`, and
resampler `ASRC linear`. Performance retained `iso_recv=13704`,
`lc3_decode=27408`, `volume=13704`, `sink_push=13703`, and ASRC `0`.
Push, repeat-feedback, ASRC-capacity, I2S-write, and DMA-restart failures were
all `0`; I2S write failures were `total=0 eio=0 enomsg=0 last=0`.
Slab free was `1/8`, output frames `476/477`, output blocks `13703`, maximum
RX callback gap `80233 us`, maximum I2S write gap `80173 us`, and maximum I2S
write time `21 us`.

FLPR handshake was Ready/ACKed/Healthy `yes`, epoch `81743773`
(`ready=1 reboot=1`), with `len=0 ver=0 unk=0 send=0`, TX sequence `147`
(`acked=146`), RX sequence `145` (`last=146187 ms`), and RX
lost/duplicate/out-of-order/missed all `0`.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active source status for the sole stream was
`seq=18 sub=18 sc=0 sf=0 cb=17 out=1`. Terminal/final source status was
`seq=12644 sub=12644 sc=12000 sf=0 cb=12644 out=0`, with
`first_errno=0`, `security_error=0`, and `disconnect_reason=0`; final idle
retained `seq=12644` and reset `sub=0 sc=0 sf=0 cb=0 out=0`. Source warning
hits, source HIL1 protocol errors, receiver runtime warning hits, receiver
shell errors, and assertion lines were all `0`. No runtime receiver warning
was retained.

Tool logs retained the existing source OpenOCD extra-erase-range warnings at
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`, plus source probe
scan deprecation messages for `gdb_port`, `tcl_port`, and `telnet_port`. These
are retained tool-log facts, not runtime warning, assertion, shell-error, or
row-failure counts. No active audio capture or audibility observation occurred
because `capture_capability=none`.

Bounded comparison with RH3-06 Mode B: selected layout was captured with all
eight requested fields positive. The current run retained
`rx_unreceived=13267`, `rx_lost=13591`, `decoded=27408`, and `PLC=27182`, the
same loss and stream-summary values retained by RH3-06; `crc_error` was `1`
here versus `2` in RH3-06. Loss evidence therefore remains materially severe.
This run uses the telemetry receiver image and is not a retry of RH3-06. These
are bounded observations only. They establish no RF, controller, firmware,
source, receiver, audio, or root-cause conclusion, and do not claim RH3,
hardware, audibility, release, product, or analog acceptance. Do not retry
this diagnostic.

## RH3 direct runner diagnostic `rh3-20260822-08-mono-selected-layout`

Immutable direct mono selected-layout diagnostic, not a matrix run and not
acceptance. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-08-mono-selected-layout/`. External JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-08-mono-selected-layout.junit.xml`.
The exact row was `rh3.fresh_mono_48_4_1`. The direct runner command exited `0`.
Retained `result.json`, JUnit, and `MANIFEST.md` record outcome `passed`,
`first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`. JUnit records one test with zero failures. Read-only
`SHA256SUMS` verification passed all `26/26` entries.

The sequential preflight passed: both output paths were absent, `rh2_test.py`
reported `154` tests OK, `capture_runner_test.py` reported `19` tests OK,
`py_compile` passed, fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. The Nix
dirty-tree notice was retained environment state. No HIL-source build ran.
The image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP telemetry image:
  `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The receiver retained one mono ASE with `chan_count=1`. Its QoS record was
`interval=10000`, `framing=0x00`, `phy=0x02`, `sdu=120`, `rtn=5`, `latency=20`,
and `pd=40000`. The live ISO link-quality record was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=9 retransmitted=0 crc_error=2 rx_unreceived=9 duplicate=0 iso_interval_1250us=8 nse=6 cig_sync_us=5304 cis_sync_us=5304 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=8
```

The selected C-to-P layout values were `iso_interval_1250us=8`, `nse=6`,
`cig_sync_us=5304`, `cis_sync_us=5304`, `c_max_pdu=120`, `c_phy=2`, `c_bn=1`,
and `c_flush_1250us=8`. Link-quality counters were `handle=0x0001`,
`tx_unacked=0`, `tx_flushed=0`, `tx_last_subevent=9`, `retransmitted=0`,
`crc_error=2`, `rx_unreceived=9`, and `duplicate=0`.

The sole receiver stream summary retained `SDUs=12644`, `decoded=12656`,
`PLC=12`, `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`, `empty_sdu=0`,
`rx_valid=12644`, `rx_error=0`, `rx_lost=12`, `rx_unknown=0`, and `rx_no_ts=9`.
The requested summary values were `rx_valid=12644`, `rx_lost=12`,
`rx_error=0`, `rx_no_ts=9`, `decoded=12656`, and `PLC=12`.

Active FLPR was `ACTIVE / epoch=830263975 gen=2`. The active top-level snapshot
was `submit=42 success=42 fallback=0 busy=0`; settle outcome was `equal` with
`initial=42/42`, `final=42/42`, and `retries=0`. Active timeout, full, stale,
sequence, frame, CRC, payload, recovery, exhaustion, probation, and busy or
fallback counters were all `0`. Active ASRC offload retained
`submit=46 success=46 fallback=0`, all timeout/full/stale/sequence/frame/CRC/
state/verify faults `0`, and FLPR cycles `min=979 max=1015 avg=999 n=46`.

Post-stop FLPR was `STOPPED / epoch=0 gen=3`, with
`submit=12656 success=12656 fallback=0 busy=0`. All listed timeout, full,
stale, sequence, frame, CRC, payload, recovery, exhaustion, and probation
counters were `0`. ASRC offload also retained `submit=12656 success=12656
fallback=0`, with timeout/full/stale/sequence/frame/CRC/state/verify faults all
`0`; RTT was `min=1819 us max=1972 us avg=1840 us n=12656`, and FLPR cycles
were `min=977 max=1035 avg=996 n=12656`. Optional structured fields were
`hb_dedup=-1`, `remote_epoch=-1`, `runtime_fails=-1`, `runtime_last_ms=-1`,
and `runtime_restarts=-1`.

Post-stop `audio status` reported decoded, PLC, decode-error, I2S-underrun,
stream-reset, and empty-SDU counters all `0`, drift `INIT`, ppm `0`,
resampler `ASRC linear`, and volume `195/255`. Performance retained
`iso_recv=12656`, `lc3_decode=12656`, `volume=12656`, `sink_push=12655`, and
ASRC `0`; push, repeat-feedback, ASRC-capacity, I2S-write, and DMA-restart
failures were all `0`. I2S write failures were `total=0 eio=0 enomsg=0 last=0`.
Slab free was `1/8`, output frames `476/478`, output blocks `12655`, maximum RX
callback gap `80248 us`, maximum I2S write gap `80145 us`, and maximum I2S
write time `21 us`.

FLPR handshake was Ready/ACKed/Healthy `yes`, epoch `2968454096`
(`ready=1 reboot=1`), with `len=0 ver=0 unk=0 send=0`, TX sequence `137`
(`acked=136`), RX sequence `135` (`last=136172 ms`), and RX
lost/duplicate/out-of-order/missed all `0`.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active source status for the sole stream was
`seq=15 sub=15 sc=0 sf=0 cb=13 out=2`. Terminal source status was
`seq=12644 sub=12644 sc=12000 sf=0 cb=12644 out=0`, with `first_errno=0`,
`security_error=0`, and `disconnect_reason=0`. Final idle retained `seq=12644`
and reset `sub=0 sc=0 sf=0 cb=0 out=0`.

Source warning hits, source HIL1 protocol errors, receiver runtime warning
hits, receiver shell errors, and assertion lines were all `0`. No runtime
receiver warning was retained. Source J-Link evidence used current commands
`gdb port disabled`, `tcl port disabled`, and `telnet port disabled`; it
contained no legacy `gdb_port`, `tcl_port`, or `telnet_port` deprecation line.
Source flash logs retained exactly the two expected page-tail erase extensions:
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`. No other tool
warning was retained. No active audio capture or audibility observation
occurred because `capture_capability=none`. PCLK timing diagnostics ranged
from `1733` to `2036 ppm`.

Bounded factual comparison with RH3-07 fresh Mode B selected layout:

| Selected field | Mono selected layout | RH3-07 Mode B |
| --- | ---: | ---: |
| `iso_interval_1250us` | 8 | 8 |
| `nse` | 6 | 6 |
| `cig_sync_us` | 5304 | 8184 |
| `cis_sync_us` | 5304 | 8184 |
| `c_max_pdu` | 120 | 240 |
| `c_phy` | 2 | 2 |
| `c_bn` | 1 | 1 |
| `c_flush_1250us` | 8 | 8 |
| `rx_unreceived` | 9 | 13267 |
| `rx_lost` | 12 | 13591 |
| `crc_error` | 2 | 1 |

The mono row is mono and RH3-07 is one stereo ASE, so these are bounded
observations only. This run uses receiver telemetry image
`d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`, is not a
retry of `rh3-20260822-05-current-image-mono-control`, and is not acceptance
evidence. No RF, controller, firmware, source, receiver, audio, or root-cause
conclusion follows.

## RH3 direct runner diagnostic `rh3-20260822-09-modea-selected-layout`

Immutable direct selected-layout Mode A diagnostic, not a matrix run and not
acceptance. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout/`. External JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout.junit.xml`.
The exact row was `rh3.fresh_mode_a_48_4_1`. The runner command was executed
once and `environment.json` records command status `0`. Retained `result.json`,
JUnit, and `MANIFEST.md` record `outcome=failed`, first failed boundary `log
scan`, and `cleanup_failures=[]`. The retained failure detail has empty source
warning and protocol lists, an empty shell-error list, and this receiver
warning:

```text
[01:11:23.831,720] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
```

Read-only `SHA256SUMS` verification passed all `26/26` entries. The sequential
preflight passed: both output paths were absent, `rh2_test.py` reported
`154/154`, `capture_runner_test.py` reported `19/19`, `py_compile` passed,
fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. No
HIL-source build ran.

The four image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP telemetry image:
  `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The receiver retained two Mode A sink ASEs, each with `chan_count=1`. Both QoS
records were `interval=10000`, `framing=0x00`, `phy=0x02`, `sdu=120`, `rtn=5`,
`latency=20`, and `pd=40000`. The exact selected C-to-P ISO records were:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14323 retransmitted=0 crc_error=2 rx_unreceived=14317 duplicate=1 iso_interval_1250us=8 nse=3 cig_sync_us=5304 cis_sync_us=5304 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=16
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14251 retransmitted=0 crc_error=0 rx_unreceived=14242 duplicate=0 iso_interval_1250us=8 nse=3 cig_sync_us=5304 cis_sync_us=2652 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=16
```

The selected fields were therefore positive on both records. Slot 0 retained
`handle=0x0001`, `tx_unacked=0`, `tx_flushed=0`, `tx_last_subevent=14323`,
`retransmitted=0`, `crc_error=2`, `rx_unreceived=14317`, and `duplicate=1`.
Slot 1 retained `handle=0x0006`, `tx_unacked=0`, `tx_flushed=0`,
`tx_last_subevent=14251`, `retransmitted=0`, `crc_error=0`,
`rx_unreceived=14242`, and `duplicate=0`.

The two receiver stream summaries were:

```text
Stream[0]: SDUs=137 decoded=28740 plc=28468 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=137 rx_error=0 rx_lost=14310 rx_unknown=0 rx_no_ts=85
Stream[1]: SDUs=135 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=135 rx_error=0 rx_lost=14249 rx_unknown=0 rx_no_ts=8
```

Requested summary values were slot 0 `rx_valid=137`, `rx_lost=14310`,
`rx_error=0`, `rx_no_ts=85`, `decoded=28740`, `PLC=28468`; and slot 1
`rx_valid=135`, `rx_lost=14249`, `rx_error=0`, `rx_no_ts=8`, `decoded=0`,
`PLC=0`.

Active FLPR was `ACTIVE / epoch=2136656776 gen=2`. The active top-level
counters were `submit=109 success=109 fallback=0 busy=0`; settle was `equal`
with `initial=109/109`, `final=109/109`, and `retries=0`. Active timeout, full,
stale, sequence, frame, CRC, payload, recovery, exhaustion, probation, and
other listed fault counters were all `0`. Active ASRC offload retained
`submit=114 success=114 fallback=0`, all timeout/full/stale/sequence/frame/
CRC/state/verify faults `0`, RTT `min=1823 us max=2072 us avg=1864 us n=114`,
and FLPR cycles `min=981 max=1020 avg=996 n=114`.

Post-stop FLPR was `STOPPED / epoch=0 gen=3`, with top-level
`submit=14370 success=14370 fallback=0 busy=0`. Timeout, full, stale, sequence,
frame, CRC, payload, recovery, exhaustion, and probation counters were all
`0`. ASRC offload retained `submit=14370 success=14370 fallback=0`, all
timeout/full/stale/sequence/frame/CRC/state/verify faults `0`, RTT
`min=1823 us max=2072 us avg=1862 us n=14370`, and FLPR cycles
`min=977 max=1020 avg=986 n=14370`. Optional structured fields were
`hb_dedup=-1`, `remote_epoch=-1`, `runtime_fails=-1`, `runtime_last_ms=-1`,
and `runtime_restarts=-1`.

Post-stop `audio status` reported decoded, PLC, decode-error, I2S-underrun,
stream-reset, and empty-SDU counters all `0`, drift `INIT`, ppm `0`, and
resampler `ASRC linear`. Performance retained `iso_recv=28754`,
`lc3_decode=28740`, `volume=14370`, `sink_push=14369`, and ASRC `0`; push,
repeat-feedback, ASRC-capacity, I2S-write, and DMA-restart failures were all
`0`; I2S write failures were `total=0 eio=0 enomsg=0 last=0`. Slab free was
`0/3`, output frames `476/477`, output blocks `14369`, maximum RX callback gap
`10130 us`, maximum I2S write gap `11767 us`, and maximum I2S write time
`22 us`.

FLPR handshake was Ready/ACKed/Healthy `yes`, epoch `4274219537`
(`ready=1 reboot=1`), errors `len=0 ver=0 unk=0 send=0`, TX sequence `155`
(`acked=154`), RX sequence `153` (`last=154158 ms`), and RX
lost/duplicate/out-of-order/missed all `0`.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active source status was `seq=24 sub=24 sc=0 sf=0 cb=23 out=1` on both
streams. Terminal status before idle was `seq=12644 sub=12644 sc=12000 sf=0
cb=12644 out=0` on both streams, with `first_errno=0`, `security_error=0`, and
`disconnect_reason=0`. Final idle retained `seq=12644` and reset
`sub=0 sc=0 sf=0 cb=0 out=0`.

Source warning hits, source HIL1 protocol errors, receiver shell errors, and
assertion lines were all `0`. One receiver runtime warning was retained, the
`bt_conn` warning above. No active audio capture or audibility observation
occurred because `capture_capability=none`.

Source J-Link evidence contained no legacy `gdb_port`, `tcl_port`, or
`telnet_port` deprecation line. Source flash logs retained exactly the two
documented page-tail erase extensions, `0x01023afc .. 0x01023fff` and
`0x0005741c .. 0x00057fff`. No other tool warning was retained.

Bounded selected-layout comparison:

| Selected field | Mode A slot 0 | Mode A slot 1 | RH3-07 Mode B | RH3-08 mono |
| --- | ---: | ---: | ---: | ---: |
| `iso_interval_1250us` | 8 | 8 | 8 | 8 |
| `nse` | 3 | 3 | 6 | 6 |
| `cig_sync_us` | 5304 | 5304 | 8184 | 5304 |
| `cis_sync_us` | 5304 | 2652 | 8184 | 5304 |
| `c_max_pdu` | 120 | 120 | 240 | 120 |
| `c_phy` | 2 | 2 | 2 | 2 |
| `c_bn` | 1 | 1 | 1 | 1 |
| `c_flush_1250us` | 16 | 16 | 8 | 8 |
| `tx_last_subevent` | 14323 | 14251 | 11358 | 9 |
| `crc_error` | 2 | 0 | 1 | 2 |
| `rx_unreceived` | 14317 | 14242 | 13267 | 9 |
| `duplicate` | 1 | 0 | 0 | 0 |

The control records retained `tx_unacked=0`, `tx_flushed=0`, and
`retransmitted=0`, as did both new Mode A records. Summary comparison was:

| Summary value | Mode A slot 0 | Mode A slot 1 | RH3-07 Mode B | RH3-08 mono |
| --- | ---: | ---: | ---: | ---: |
| `rx_valid` | 137 | 135 | 113 | 12644 |
| `rx_lost` | 14310 | 14249 | 13591 | 12 |
| `rx_error` | 0 | 0 | 0 | 0 |
| `rx_no_ts` | 85 | 8 | 9 | 9 |
| `decoded` | 28740 | 0 | 27408 | 12656 |
| `PLC` | 28468 | 0 | 27182 | 12 |

These are bounded observations only. The new Mode A row uses telemetry
receiver image `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`,
is not a retry of RH3-04, and is not acceptance evidence. No RF, controller,
payload-size, scheduling, source, receiver, audio, or root-cause conclusion
follows. Do not retry this diagnostic.

2026-10-08 handoff-reconciliation note (provenance preserved): the retired
selected-layout physical handoff retained the historical preset and
packing provenance and one otherwise unnamed scratch build root (historical
facts, not current v3.4.1 claims): the NCS v3.3.0
`BT_BAP_LC3_UNICAST_PRESET_48_4_1` macro supplied each Mode A
front-left/front-right stream with unframed 10,000 us interval, 120-byte
SDU, RTN 5, 20 ms transport latency, 40,000 us presentation delay, and 2M
PHY preference; the HIL source constructs the group with
`BT_ISO_PACKING_SEQUENTIAL`; and `bt_iso_chan_get_info()` reports selected
values after CIS establishment. Its exact input identities table
(`6607e71c...` receiver-era versus `d8e5708...` telemetry-image two-tuple)
proved reviewed local inputs only, never hardware or acceptance evidence.
The preflight's expected host test counts were RH2 `154/154` and capture
`19/19`; the Nix dirty-tree notice was retained environment state, not a
source/compiler diagnostic; and the scratch build root
`/tmp/le-audio-receiver-iso-selected-shell54.jpGre5` was named as retained
environment state and not to be removed.

## RH3 direct runner diagnostic `rh3-20260822-10-modeb-7p5-selected-layout`

Immutable direct selected-layout Mode B 7.5 ms diagnostic, not a matrix run,
retry, acceptance run, or causal conclusion. Exact evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout/`. External
JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout.junit.xml`.
The exact row was `rh3.fresh_mode_b_48_3_1`. The runner command was executed
exactly once. `environment.json` records direct command status `0`; retained
`result.json`, JUnit, and `MANIFEST.md` record `outcome=failed`, first failed
boundary `session end`, failure detail `missing receiver stream summary slot(s):
[0]`, and `cleanup_failures=[]`. JUnit records one test and one failure.
Read-only `SHA256SUMS` verification passed all `26/26` entries. Do not retry
this ID or reuse either output path.

The complete sequential preflight passed: both output paths were absent,
`rh2_test.py` reported `154/154`, `capture_runner_test.py` reported `19/19`,
`py_compile` passed, fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. The Nix
dirty-tree notice was retained environment state. No source or receiver build
ran.

The four image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP telemetry image:
  `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The real runner profile semantics were retained. Source `hello` reported
`frame_samples_48_3_1=360`, `octets_48_3_1=90`, and
`sdu_modeb_48_3_1=180`; the configured source record reported `mode=mode_b`,
`profile=48_3_1`, `scored_target=16000`, and one stream. Receiver logs retained
one stereo ASE (`chan_count=2`) with `Frequency=48000 Hz`, `Frame Duration=7500
us`, `Octets per frame=90`, and QoS `interval=7500 framing=0x00 phy=0x02 sdu=180
rtn=5 latency=15 pd=40000`. This validates the existing 360-frame profile
selection. Per the fixed 480-frame FLPR input contract, CPUAPP ASRC fallback was
expected; no FLPR ASRC data-plane operation occurred.

The exact selected C-to-P ISO record was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=16869 retransmitted=0 crc_error=0 rx_unreceived=18850 duplicate=0 iso_interval_1250us=6 nse=6 cig_sync_us=6744 cis_sync_us=6744 c_max_pdu=180 c_phy=2 c_bn=1 c_flush_1250us=6
```

All eight selected fields were positive. Link-quality counters were
`handle=0x0001`, `tx_unacked=0`, `tx_flushed=0`, `tx_last_subevent=16869`,
`retransmitted=0`, `crc_error=0`, `rx_unreceived=18850`, and `duplicate=0`.
Because `c_flush_1250us=6` divides evenly by `iso_interval_1250us=6`, derived
FT is `1`; this is an observation, not a configuration request.

No receiver stream summary was retained. `result.json` has `summary={}`, and
the runner rejected session end because slot `0` was missing. Therefore
`rx_valid`, `rx_lost`, `rx_error`, `rx_no_ts`, `decoded`, and `PLC` are not
available for this row and are not inferred from `rx_unreceived`.

Active FLPR retained the expected CPUAPP-ASRC zero plane:
`ACTIVE / epoch=1494090630 gen=2`, top-level
`submit=0 success=0 fallback=0 busy=0`, timeout/full/stale/seq/frame/CRC/payload
faults `0`, recovery `attempts=0 fail=0 relapses=0 exhaustion=0`, probation
`active=0 success=0 cleared=0`, and RTT `(none)`. ASRC offload retained
`submit=0 success=0 fallback=0`, with timeout/full/stale/seq/frame/CRC/state/
verify faults all `0`. The receiver fatal occurred before post-stop diagnostics,
so no post-stop FLPR snapshot was retained; post-stop zero-plane evidence is
therefore unproven in this run. No post-stop audio/performance/handshake
snapshot was retained either.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `verdict=fail`, and
final `idle`. Active sole-stream status was `seq=26 sub=26 sc=0 sf=0 cb=25
out=1`. Final idle status retained `seq=16859 sub=0 sc=0 sf=0 cb=0 out=0`.
The configured row expected `16859` submitted SDUs and `16000` scored SDUs;
no terminal source counter record beyond the final idle reset was retained.

Receiver runtime fault evidence was retained exactly: at `00:24:54.361,273`,
`i2s_nrfx: Next buffers not supplied on time`; at `00:24:54.667,976`,
`i2s_nrfx: Cannot write in state: 4`; at `00:24:54.667,987`,
`audio_i2s: I2S underrun, restarting DMA`; and at `00:27:20.667,425`, a second
`i2s_nrfx: Next buffers not supplied on time`. Teardown then retained
`ASSERTION FAIL [err == 0] @ WEST_TOPDIR/zephyr/subsys/bluetooth/host/hci_core.c:506`,
`Controller unresponsive, command opcode 0x206f timeout with err -11`,
`ZEPHYR FATAL ERROR 3: Kernel oops on CPU 0`, and `Halting system`. Source
warning hits, source HIL1 protocol errors, receiver shell errors, and cleanup
failures were empty. FLPR boot reached READY and READY_ACK before streaming.

Source J-Link evidence retained current `gdb port disabled` output and no
`DEPRECATED`, `gdb_port`, `tcl_port`, or `telnet_port` token. Source flash
retained exactly the documented page-tail erase extensions:
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`. No other tool
warning was retained. No active capture or audibility observation occurred
because `capture_capability=none`.

Bounded comparison, with no cause inferred:

| Observation | Mode B 7.5 ms | RH3-07 Mode B 10 ms | RH3-08 mono | RH3-09 Mode A slots 0 / 1 |
| --- | ---: | ---: | ---: | ---: |
| `iso_interval_1250us` | 6 | 8 | 8 | 8 / 8 |
| `nse` | 6 | 6 | 6 | 3 / 3 |
| `cig_sync_us` | 6744 | 8184 | 5304 | 5304 / 5304 |
| `cis_sync_us` | 6744 | 8184 | 5304 | 5304 / 2652 |
| `c_max_pdu` | 180 | 240 | 120 | 120 / 120 |
| `c_flush_1250us` | 6 | 8 | 8 | 16 / 16 |
| `crc_error` | 0 | 1 | 2 | 2 / 0 |
| `rx_unreceived` | 18850 | 13267 | 9 | 14317 / 14242 |
| `rx_lost` | not retained | 13591 | 12 | 14310 / 14249 |

The new row is one stereo ASE at 7.5 ms, RH3-07 is one stereo ASE at 10 ms,
RH3-08 is one mono ASE, and RH3-09 is two mono ASEs. These are bounded
transport observations only. The new row uses receiver telemetry image
`d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`, is not a
retry, and is not acceptance evidence. The retained physical-run count after
this diagnostic is twenty: fifteen failed, one cancelled, and four passed
direct controls. The target-three source image has received eleven direct
physical diagnostics. The unexpected receiver fatal, missing summary, and
missing post-stop FLPR snapshot are blockers for any acceptance interpretation.

## Historical ISO RX trace mechanisms and interpretation limits

Historical NCS v3.3.0 records for the RH3-30 through RH3-39 ISO RX trace
diagnostics: mechanism behavior, exact identities, and interpretation limits
observed at the time. The historical handoff texts remain provenance sources
for retained observations and stay recoverable at Git rev `a94f010`, for
example
`git show a94f010:docs/development/system-hil-rh3-30-sdc-hci-receive-disposition-trace-execution-handoff.md`.
Every fact in this section is NCS v3.3.0-historical diagnostic evidence. It is
not current v3.4.1 production behavior, acceptance, or approval to replay any
configuration, and the cited `conn.c`, `sched.c`, `sem.c`, `work.c`, and
`iso.c` line anchors refer to the installed NCS v3.3.0 source of that time,
not the current SDK tree.

The trace gates remain in this repository's `Kconfig` (default off) and the
trace fragments remain in `tests/hil/`, but quoted SHA-256 values bind the
historical fragment versions recorded by the listed runs, not the present
files. The disposition fragment is the concrete example: H34/H35 recorded
`8f945656ea4f2ed6801a7a24166a01ec40f6183b15695b74358a31e5a3634dd8` and RH3-36
later updated the same path to the schema-16 content
`aa8d87a615516433fb1d8e59b7b79a189e7bb34a909551144799c05d35d71ab8`. Historical
handoffs record former fragment identities; recover matching fragment bytes
from their actual historical commits, not from the current file. The historical
gate-name chain behind the per-run fragments is
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE`, then
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION`,
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME`,
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION`,
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH`,
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE`,
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND`,
and
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE`,
with `CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT` and
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK` left unset in every
retained trace config of this chain. The historical
normal nRF54L15 build shape for the whole chain was
`CONFIG_BT_RECV_WORKQ_BT=y`, `CONFIG_BT_RX_PRIO=8`, `CONFIG_BT_CONN_TX=y`,
`CONFIG_SYSTEM_WORKQUEUE_PRIORITY=-1`, `# CONFIG_BT_CONN_TX_NOTIFY_WQ is not
set`; trace fragments additionally pinned
`CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y` and
`CONFIG_LOG_RUNTIME_FILTERING=n`, the H39 fragment temporarily enabled
`CONFIG_TRACING=y`, `CONFIG_TRACING_USER=y`, and `CONFIG_TRACING_THREAD=y`
with normal builds remaining tracing-disabled, and the trace runs prohibited
shell `log` commands (`CONFIG_LOG_CMDS`) and evidence mutation.

### RH3-30 first receive-work disposition outcomes

RH3-29 (`rh3-20260823-29-sdc-hci-yield-switch-trace`)
proved MPSL switched in twice during the sender's first post-unlock yield while
target receive work cleared, and did not observe `hci_internal_msg_get` or
command completion. RH3-30 (run
`rh3-20260824-30-sdc-hci-receive-disposition-trace`) then observed exactly one
first receive-work
transaction with four target-owned outcome names: retained ISO message cannot
obtain `BT_BUF_ISO_IN`; HCI fetch returns an error before allocation; fetched
EVT, DATA, or ISO message cannot obtain its matching host buffer; or normal
fetched and delivered target completion. A generic zero-status retrieval marker
inside `fetched_buffer_unavailable` records controller event retrieval only; it
is not successful host command delivery. Proof requires cross-object
disassembly, not wrapper symbol listing alone, and no wrapper may be added for
`hci_driver_receive_process` because its same-object work-handler call is not
an eligible GNU ld `--wrap` reference.

Grounded NCS v3.3.0 lifetime facts: `nrf/subsys/bluetooth/controller/hci_driver.c:522-548`
allocates one `BT_BUF_ISO_IN` buffer before delivering an ISO HCI packet to the
host; `hci_driver.c:690-713` retains that ISO packet and stops fetching later
messages when allocation returns NULL, and a later ISO-buffer free resubmits
MPSL receive work; `zephyr/subsys/bluetooth/host/iso.c` defines the fixed
`iso_rx_pool` with exactly `CONFIG_BT_ISO_RX_BUF_COUNT` entries; the retained
ISO retry wakes only after a `BT_BUF_ISO_IN` free notification. The normal
resolution was `CONFIG_BT_ISO_RX=y`, `CONFIG_BT_ISO_RX_MTU=251`,
`CONFIG_BT_ISO_RX_BUF_COUNT=3`, with ELF pool sizes `_net_buf_iso_rx_pool` 0x54,
`net_buf_data_iso_rx_pool` 0x318, and `iso_info_data` 0x18.

Identities: historical normal receiver baseline CPUAPP
`07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0`, FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`; trace
fragment `tests/hil/receiver-sdc-remove-iso-path-receive-disposition.conf`
at the version recorded for this run, SHA-256
`32c8ff503a1bf84d1c6120d31a05f76f0d0ccd2668e93e238ae605eaf6f48660`.
The trace CPUAPP hash was deliberately not predeclared: uncommitted dirty-tree
diagnostic source was built once, its SHA-256 recorded before hardware, and the
runner retained the exact image hash in `images.json`.

### RH3-31 six-buffer headroom experiment

Run `rh3-20260824-31-sdc-hci-iso-rx6-trace` tested whether extra host pool
headroom lets a pending `0x206f` completion past the retained ISO packet.

For that NCS build each additional ISO RX pool entry cost 300 bytes (28-byte
`net_buf`, 264-byte data chunk, 8-byte ISO info); six entries added 900 bytes
relative to the normal three. The candidate fragment
`tests/hil/receiver-sdc-remove-iso-path-receive-disposition-rx6.conf`
at the version recorded for this run, SHA-256
`33e61b0a5fa90b9b6b0d8e1c0e184f3c54f66c386a6318d8219a439f9d9c1187`,
produced candidate receiver CPUAPP
`6b7db600ab63047588e18f0543a4b04ac2724d5c698efe0eb4468209f0255795` with FLPR
unchanged. `scripts/check-build-contract.py` was not run against
temporary trace builds; it correctly pins the production three-buffer value.
The 20 ms nRF54L15 post-start I2S slab wait can delay BT RX work, but
RH3-30 did not prove that wait is the sole exhaustion cause; the wait, I2S
logic, host driver, controller configuration, and production ISO RX count were
not changed.

Bounded interpretation rules: a first scoped
`kind=rx type=32 buffer_available=1` with no retained-unavailable, a complete
receiver summary, and no `0x206f` timeout/fatal would support the headroom
hypothesis for that row only; `buffer_available=1` with another failure proves
only the immediate allocation changed, not a repair; another
`retained_iso_buffer_unavailable` result disproves six-buffer sufficiency for
the sampled transaction; and a missing later completion marker alone does not
prove command failure. RH3-31 used the reviewed six-entry pool and still failed
at session end with `missing receiver stream summary slot(s): [0]`, then hit
the `0x206f` timeout and halted, disproving six buffers as sufficient
headroom for that transaction without proving why all entries were held.

### RH3-32 bounded lifetime tracking and pre-stream failure

RH3-32 (run `rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace`, failed at session
end like RH3-31) moved back to the normal three-buffer configuration and
tracked every
post-session-start successful ISO RX allocation plus only the matching final
`net_buf_unref()` release: bounded fixed atomic counters and pointer CAS only,
never dereferencing a buffer after its real final unref, `outstanding` never
deliberately driven below zero or above capacity, and at most four diagnostic
logs per session (arm, disable snapshot, unavailable snapshot, first free after
unavailable). Adjacent counter samples are explicitly not a lock-held
transaction and must not be presented as one. Schema 13 keeps all lifetime data
optional so legacy no-lifetime traces stay valid. `CONFIG_NET_BUF_LOG=n` was
required because NCS exposes `net_buf_unref` as an external wrappable symbol
only in that configuration, and proof required the disassembled
`bt_conn_reset_rx_state -> __wrap_net_buf_unref` edge, not symbol listing.

Identities: reproduced normal CPUAPP
`e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f` (two pristine
trace-disabled builds) differs from the historical `07fdbe...` RH3-30-era
baseline; neither hash proves hardware or artifact acceptance. Trace fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf` at the version
recorded for this run, SHA-256
`484ed01b6323b264206430d56142cc689ffac1d662b2c358e87cbd58ebda1b15`.

Unarmed-wrapper physical run `rh3-20260824-32-sdc-hci-iso-rx-lifetime-trace`
failed before stream and before lifetime arm: the source stayed `connecting`
for 20.031 seconds, then reported `first_errno=-116` (`-ETIMEDOUT`), while the
receiver logged `Security changed: level 1 err 9` and disconnected with reason
`0x3e` (`BT_HCI_ERR_CONN_FAIL_TO_ESTAB`; err 9 is
`BT_SECURITY_ERR_UNSPECIFIED`); no arm or allocation marker exists. The
follow-up functional unarmed-forwarding native test (reset, init one buffer
with `ref == 1U`, one `__wrap_net_buf_unref()` call forwarding exactly once
with the observer seeing one call and ref dropping to zero, zero arm/snapshot/
first-free/final-unref/tracking-error counts) closed that forwarding-proof gap
only. It does not prove zero timing cost and RH3-32's failure cannot assign
cause to trace code, source image, receiver image, RF, or pairing.

### RH3-33 lifetime sample at full occupancy

RH3-33 (run `rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace`) re-observed the
same lifetime question with a new output ID, run once
only, with the unchanged trace image
`1ca02b604cb9baee0837aee72cb3aecb2f6f039120acea83444beafbc6864bd0` (reproducible
because the added native test does not change the receiver image; the same
unchanged trace image must match before any hardware work). The H33 sample
keeps the failed pre-stream H32 failure as separate immutable evidence rather
than replacing it, and it cannot assign a cause to the trace
code, the source image, the receiver image, RF, or pairing. Its valid
schema-13 sample reached fresh Mode B streaming and recorded at disable and at
the later target allocation:

```text
capacity=3 outstanding=3 high_water=3 allocations=19488
final_unrefs=19485 callbacks_active=0 callbacks_total=19485
```

The subsequent SDC receive-disposition marker reported
`kind=rx type=32 buffer_available=0 target_busy=0x1`, and no first
final-unref marker arrived before the `0x206f` command timeout and fatal. This
proves full tracked ISO RX occupancy and no active project callback at those
samples only. It does not prove queue position, per-buffer owner, a leak,
callback-to-unref pairing, or a production bug.

### RH3-34 four-category wrapper-stage observation

RH3-34 (run `rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace`)
added a HIL-only wrapper around the internal, non-static
`bt_conn_recv(struct bt_conn *, struct net_buf *, uint8_t)` declared in
`zephyr/subsys/bluetooth/host/conn_internal.h:409-414`. The wrapping decision
was grounded by the existing ISO object's undefined `R_ARM_THM_CALL
bt_conn_recv` relocation from `hci_iso`, not by symbol presence. Schema-14
records four mutually exclusive monotonic per-slot categories:
`undispatched` (no wrapper observation), `host_dispatched` (wrapper entered,
project callback not started), `app_callback_seen` (`stream_recv()` started),
and `unclassified` (adjacent atomic slot-state race at allocation or final
release). No category names a specific NCS dispatch branch: `undispatched`
means only that this wrapper did not observe `bt_conn_recv()` for that tracked
pointer, never a specific queue location or owner. The scan is not a lock-held
transaction; each category and their sum
stay bounded by capacity, and their sum need not equal the regular lifetime
`outstanding` counter. Fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf`
at its H34/H35-era version, SHA-256
`8f945656ea4f2ed6801a7a24166a01ec40f6183b15695b74358a31e5a3634dd8`;
RH3-34 trace CPUAPP `2c5616a26ab425cf34f4c97c736f8a41270893f4c08db7065df5f655367f8361`;
software review 15/15 native, 220 parser, with field and aggregate disposition
capacity rejection. Its valid schema-14 unavailable snapshot recorded
`undispatched=2 host_dispatched=1 app_callback_seen=0 unclassified=0` with no
first-final-unref marker, and it cannot distinguish a call still inside
`bt_conn_recv()` from one that returned without the project callback.

### RH3-35 host_returned stage

RH3-35 (run `rh3-20260824-35-sdc-hci-iso-rx-dispatch-return-trace`; its
unavailable snapshot recorded `undispatched=2 host_dispatched=1
app_callback_seen=0` in four-category grammar) added the monotonic post-return
stage `host_returned` (wrapper returned
from `bt_conn_recv()` without project `stream_recv()` start) to separate a
live dispatch from a returned call. The post-real advancement must preserve any
higher stage: an `APP_CALLBACK_SEEN` advance made inside real `bt_conn_recv()`
stays after wrapper return, and a slot cleared by final release before return
is not revived. Schema-15 parses the new integer field and keeps schema-14
compatibility: a schema-14 grammar that omits `host_returned` parses with
`host_returned: None`, while a malformed supplied `host_returned` field is a
parser error and is not treated as a legacy omission. The trace fragment is the
same `8f945656...` H34/H35-era file; RH3-35 trace CPUAPP
`0e96798809047cd862735b829a34a0562fbf2ce8b0c20ca4f2a2a4923e8390bf`. The H34
disable snapshot had three undispatched and zero host-dispatched; the
unavailable snapshot had two undispatched and one host-dispatched. An observed
live `host_returned` slot narrows the question to NCS host behavior after
`bt_conn_recv()` returned without an app callback; it does not prove the
specific `bt_iso_recv()` branch, an owner, a leak, or a repair.

### RH3-36 scoped TX-notify flush observation

RH3-36 (run `rh3-20260825-36-sdc-hci-iso-rx-tx-notify-flush-trace`,
SHA256SUMS 25/25) wrapped only the cross-object `k_work_flush()` (residing
in `zephyr/kernel/work.c`) while the outer `bt_conn_recv()` wrapper holds a
live same-thread context (tracked buffer pointer, current thread ID, active
bit, set and cleared in a fixed order). Its original eight-stage design that
wrapped `bt_conn_tx_notify` directly was explicitly superseded before
completion: GNU `--wrap` cannot redirect the same-object
`bt_conn_recv() -> bt_conn_tx_notify()` call inside NCS
`zephyr/subsys/bluetooth/host/conn.c`, and that superseded recipe is a
historical failed design, never an executable or current recommendation.

The initial RH3-36 implementation passed host tests, parser tests, config
proof, and produced trace CPUAPP
`b1a91236e72d6499ea413429d2ed2b44098e5406df54747c1cc409bd28af3395`; it then
failed the required linkage proof. `--wrap=bt_conn_tx_notify` appeared in
`build.ninja` and the wrapper symbols existed, but target disassembly retained
the same-object call `bt_conn_recv: bl <bt_conn_tx_notify>`. No trace image was
flashed, and the local normal build was not restored at that checkpoint. The
retained requirement: preserve the proven edges
`hci_iso -> __wrap_bt_conn_recv`,
`bt_conn_tx_notify -> __wrap_k_work_flush`, and
`bt_conn_reset_rx_state -> __wrap_net_buf_unref`, and prove that
`bt_conn_recv` still calls the real `bt_conn_tx_notify`, not a nonexistent
wrapper. The same-object call must not be interposed by an NCS patch,
production-code change, or linker flag.

Schema-16 has seven bounded categories (`undispatched`, `host_dispatched`,
`tx_notify_flush_entered`, `tx_notify_flush_returned`, `host_returned`,
`app_callback_seen`, `unclassified`); schema-15 five-field lines keep both new
fields `None`, and schema-14 four-field lines keep `host_returned` plus both
new fields `None`. At execution time the disposition fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf` had
been updated to schema-16 content, SHA-256
`aa8d87a615516433fb1d8e59b7b79a189e7bb34a909551144799c05d35d71ab8`, and the
expected trace CPUAPP was `cedc7fecbc09cc25af74e05f6515ddeff3db22e847792c14108ea26616d96825`.
A foreign-flush native-test repair retained the rule that a
cached pre-foreign-call snapshot proves forwarding but not the post-call stage:
a fresh session must allocate one tracked buffer, must not invoke
`__wrap_bt_conn_recv()`, then a foreign flush outside the outer context must
leave the subsequent disable snapshot at `undispatched=1` with every
flush/return stage zero, with exact `work`, `sync`, and Boolean forwarding
retained.

RH3-36's physical run reached source `streaming` and `scored_complete`, armed
at capacity three, and its unavailable snapshot recorded `outstanding=3
high_water=3 allocations=19488 final_unrefs=19485 callbacks_active=0
callbacks_total=19485 undispatched=2 host_dispatched=0
tx_notify_flush_entered=1 tx_notify_flush_returned=0 host_returned=0
app_callback_seen=0 unclassified=0`: one scoped flush entered without an
observed return. The run ended at session end with missing receiver summary
slot zero, then the receiver showed controller command `0x206f` timeout -11,
the `hci_core.c:506` assertion, and a kernel oops. The stage
does not identify a work item owner, prove the flush slept, establish a cycle,
name a NCS branch, prove a leak, or authorize a repair. The grounded NCS
contract: `bt_conn_recv -> bt_conn_tx_notify(conn, true) ->
k_work_submit_to_queue(tx_notify_workqueue_get(), &conn->tx_complete_work) ->
k_work_flush(&conn->tx_complete_work, &sync)`, and `k_work_flush` calls
`z_impl_k_sem_take(&sync.flusher.sem, K_FOREVER)` only when `work_flush_locked()`
reports need_flush. Historical NCS v3.3.0 source anchors: the pre-callback
TX-notify call sits
at `conn.c:492-506`, the caller-not-on-TX-workqueue flush branch at
`conn.c:340-355`, and `k_work_flush()` resides in `zephyr/kernel/work.c`, so
its cross-object call is link-wrappable.

### RH3-37 generated semaphore-take entry observation

RH3-37 (run `rh3-20260825-37-sdc-hci-iso-rx-tx-notify-flush-semaphore-trace`,
SHA256SUMS 25/25, source reached `streaming` and `scored_complete`, runner
failed at session end only) wrapped the generated `z_impl_k_sem_take`, not an
inline or public
`k_sem_take` symbol: exact normal ELF disassembly proved the cross-object call
`k_work_flush: bl z_impl_k_sem_take`. Matching required all of: an active outer
HIL `bt_conn_recv()` context, the current thread equal to the captured outer
receive thread, the same tracked ISO RX buffer, the exact captured
`&sync->flusher.sem` pointer captured by the scoped flush wrapper (never
dereferencing a null `sync`), and a `K_FOREVER` timeout. Foreign semaphore,
wrong pointer, or non-`K_FOREVER` calls forwarded exactly once with no stage
change. Entering the wrapper does not prove a sleep: the semaphore may have a
positive count, and `z_impl_k_sem_take()` pends only after a zero count and a
non-`K_NO_WAIT` timeout; absence of wrapper return at a snapshot does not
identify why completion did not occur. Its unavailable snapshot recorded
`capacity=3 outstanding=3 high_water=3 allocations=19485 final_unrefs=19482
callbacks_active=0 callbacks_total=19482 undispatched=2 host_dispatched=0
tx_notify_flush_entered=0 tx_notify_flush_semaphore_entered=1
tx_notify_flush_semaphore_returned=0 tx_notify_flush_returned=0 host_returned=0
app_callback_seen=0 unclassified=0`: the H37 wrapper observed semaphore entry
without observing its return before the snapshot; it did not establish
semaphore count, `z_pend_curr()` selection, a completed pend, `pend_locked()`
execution, thread state, handler state, a deadlock, ownership, a controller
cause, a leak, or a repair. Identities: HEAD
`c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1`; fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore.conf`
at the version recorded for this run, SHA-256
`28b5bd26b1e4826e16a067cd766db7e1f45266281a963ff54d192b434f5c2320`; trace
CPUAPP `d5766415e92b953b5e45c5474d0e6c2062c71fdb3f90c47b3420b30bf00a362d`;
software acceptance 15 native and 222 RH2 parser passed; link proof included
`k_work_flush -> __wrap_z_impl_k_sem_take`. Schema-17 adds the semaphore
entered/returned fields to the disposition grammar.

### RH3-38 z_pend_curr entry observation

RH3-38 (run `rh3-20260825-38-sdc-iso-rx-pend-trace`, `25/25` artifacts,
schema 18, empty parser/validation errors, source reached streaming and
scored_complete, runner failed at session end only) wrapped `z_pend_curr()`
(installed signature
`__wrap_z_pend_curr(struct k_spinlock *, k_spinlock_key_t, _wait_q_t *,
k_timeout_t)`) after exact normal receiver disassembly proved the direct call
target `z_impl_k_sem_take at 0x58e18 ... bl 0x5a88c <z_pend_curr>`. Matching
required the captured flusher semaphore non-NULL, `wait_q ==
&captured_sem->wait_q`, `K_FOREVER`, the live exact tracked buffer and thread,
and the buffer still occupying a tracked lifetime slot; real `lock`, `key`,
`wait_q`, `timeout`, and result were preserved unchanged, and foreign,
no-scope, or non-forever calls forwarded untouched. Wrapper entry is not real
body entry: observing `tx_notify_flush_semaphore_pend_entered=1` with
`pend_returned=0` does not prove entry to the real `z_pend_curr()` body,
`pend_locked()` execution, a completed pend, thread state, a handler cause, a
deadlock, a controller defect, or a repair.

A first 67-character run ID
`rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace` failed
`lifecycle.validate_run_id()` (accepts only
`[A-Za-z0-9][A-Za-z0-9._-]{0,63}`) at `_step_validate()` before fixture-lock
acquisition, run-directory creation, identity resolution, or any target
interaction. `scripts/hil/lifecycle.py`'s `HilLifecycleError` was caught by
`Runner.run()`, so the failed command returned status 1, not the CLI status 2.
No hardware, identity resolution, flash, reset, serial access, source control,
or evidence directory was created, and the old ID was never invoked against
hardware. The executed `rh3-20260825-38-sdc-iso-rx-pend-trace` (37 characters)
is therefore a new no-hardware-predecessor execution, not a physical retry.

Identities: fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend.conf`
at the version recorded for this run, SHA-256
`a1b1b4205e92f7506a81a203a36577dcac5522f02f1bee4b99909b76072eade8`;
trace CPUAPP `158ac6ea96a32ecd50629f5af7c8b4f919f51f42f8247c8ff7b2059ae3eaa32a`;
direct-link proof chain `hci_iso -> __wrap_bt_conn_recv; bt_conn_recv ->
bt_conn_tx_notify; bt_conn_tx_notify -> __wrap_k_work_flush; k_work_flush ->
__wrap_z_impl_k_sem_take; z_impl_k_sem_take -> __wrap_z_pend_curr`; software
acceptance 18 native and 224 RH2 parser passed; schema-18 adds the two pend
fields and schemas 14 through 17 still parse with both new fields `None`.

### RH3-39 scheduler-pend hook and give entry

H39 ran as `rh3-20260826-39-sdc-iso-pend-sched-give` with integrity passing
`25/25` listed artifacts. Its source proof used `add_to_waitq_locked`, not a
direct `z_pend_curr` disassembly call, because the then-current toolchain kept
that static helper separate in the linked image.

RH3-39 added two bounded observations without modifying NCS: (1) the built-in
scheduler-pend user hook via `CONFIG_TRACING_USER=y` (trace-only, no
production use; the installed NCS v3.3.0 has no user-tracing work-handler
execution hook), which fires after `z_mark_thread_as_pending()` and before the
`pended_on` write and wait-queue insertion, because
`add_to_waitq_locked()` calls `z_mark_thread_as_pending(thread)`, then
`SYS_PORT_TRACING_FUNC(k_thread, sched_pend, thread)`, then writes `pended_on`
and inserts into the wait queue, all with `_sched_spinlock` held
(`zephyr/kernel/sched.c:574-605`; `zephyr/kernel/sched.c:664-684` shows
`z_pend_curr()` acquiring `_sched_spinlock`, calling `pend_locked()`, releasing
the caller lock, then calling `z_swap()`; `zephyr/kernel/sem.c:139-158` holds
the file-static semaphore lock and calls `z_pend_curr(&lock, key,
&sem->wait_q, timeout)` only after a zero count and a non-`K_NO_WAIT` timeout).
`zephyr/subsys/tracing/user/tracing_user.c:211-214` maps
`sys_trace_thread_pend()` to the weak `sys_trace_thread_pend_user()` hook; and
(2) entry to generated `z_impl_k_sem_give()` for the exact captured
`&sync.flusher.sem` (source-grounded in
`zephyr/kernel/work.c:108-116`, where `finalize_flush_locked()` gives the
stack-local `k_work_sync` flusher barrier semaphore, and
`zephyr/kernel/sem.c:95-121`, where a give unpends a present waiter, readies
it, then reschedules or unlocks).

Hook constraints (scheduler-lock context): it may advance a tracked buffer to
`tx_notify_flush_semaphore_pend_thread_marked_pending` only when armed by a
matching H38 `z_pend_curr()`, the thread equals the captured outer receive
thread, the outer context stays active for the captured tracked buffer, and
that buffer remains in tracked lifetime slots. The hook runs with the scheduler
lock held and may use only fixed atomics, fixed pointer identity, and bounded
slot scanning: no log, allocation, locking, work submission, wait, scheduler
call, semaphore API, queue or field inspection, or `k_current_get()` call. A
marked-pending observation does not establish `pended_on`, wait-queue
insertion, `z_swap()`, or sleep completion.

Give-wrapper constraints: the match must not require current-thread
equality because the expected give occurs in a workqueue thread, not the
blocked receive thread; it must not dereference `sem`, inspect the semaphore
count, inspect a work field/handler/queue, or infer a work owner. On an exact
match it advanced the tracked buffer to
`tx_notify_flush_semaphore_give_entered`, then called the real give exactly
once. No give-returned category exists: `z_impl_k_sem_give()` may reschedule
the higher-priority waiter before a post-real wrapper check, allowing the outer
context or slot to disappear, so the entry marker is bounded evidence only.
Static helpers (`pend_locked()`, `add_to_waitq_locked()`,
`finalize_flush_locked()`, `handle_flush()`, `work_queue_main()`) must never be
wrapped. Snapshot stages are exclusive and monotonic: a later give stage can
supersede a visible marked-pending stage, so an absent lower bucket does not
mean that lower event never occurred.

Identities: fragment
`tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition-flush-semaphore-pend-sched-give.conf`
at the version recorded for this run, SHA-256
`052b98cae2123f79ad01ca3870f94a8711a7670666a37eeced133e38ab61ff45`;
trace CPUAPP `cd4b569523621a3192f23cb0b1841fcd3c6f3a9909bcd41cdaab90814d4c9675`.

Schema-19 adds both fields. H39's physical run reached `streaming` and
`scored_complete`, failed only at session end with `missing receiver stream
summary slot(s): [0]`, and its valid schema-19 unavailable snapshot recorded
`capacity=3 outstanding=3 high_water=3 allocations=19485 final_unrefs=19482
callbacks_active=0 callbacks_total=19482 undispatched=2 host_dispatched=0
tx_notify_flush_semaphore_pend_thread_marked_pending=1
tx_notify_flush_semaphore_give_entered=0` with all flush, semaphore, and pend
enter/return stages zero, then the receiver reported `Controller
unresponsive, command opcode 0x206f timeout with err -11` with the current
thread on `sysworkq`. No outcome proves wait-queue insertion, handler
identity or ownership, give return, waiter resumption, deadlock, controller
defect, or a production repair.

## Historical early RH0/RH1 review findings

All identities below refer to Git rev `a94f010`-era handoff provenance and
historical NCS v3.3.0 / nRF5340DK fixture implementations. No number is
freshly revalidated raw evidence, an accepted test total, or a current
recipe. The RH0/RH1 contract invariants live in `system-hil-milestones.md`
("Historical RH0/RH1 implementation invariants"); FixtureLock tests and
BAP/APP source comments stay primary in-source contracts.

### Historical RH0 review findings

First RH0 review: 52 RH0 tests, pytest 8.4.2, and pyserial 3.5 reported
passing, yet the focused review reproduced real acceptance defects. Retained:

1. Logical fixture schema accepted an extra role beyond the exact
   `receiver`/`source` (plus capability-dependent `capture`) contract.
2. Rejected HIL1 transition mutated `firmware_id` and `last_monotonic`; a
   status record at segment 9 followed by segment 0 was accepted, violating
   segment monotonicity; future-segment records were not rejected once a
   current segment existed.
3. A cleanup callback raising `KeyboardInterrupt` prevented older callbacks
   from running; body `KeyboardInterrupt` plus cleanup failure would be
   invalid inside `ExceptionGroup` rather than `BaseExceptionGroup`.
4. A pre-existing `<output-root>/.locks` symlink was followed, allowing lock
   file creation outside the selected output root.
5. Four `ResourceWarning` diagnostics from unclosed evidence files, forbidden
   by the project warning policy.
6. No test covered a write-stage failure after one metadata file was updated;
   `finalize_evidence()` could leave a new `SHA256SUMS` beside an old or failed
   `MANIFEST.md`.

False positive proofs in the original RH0 tests (retained so future test work
does not repeat them):

1. Cleanup tests registered the failing callback before the successful older
   callback, so LIFO executed success before failure; the `['ok']` assertion
   proved success without proving execution order.
2. The staging-failure test failed the first staging call, never proving the
   first staged file is removed when a second staging call fails.
3. The snapshot-failure test claimed snapshot-boundary coverage, but replacing
   `SHA256SUMS` with a directory failed earlier during evidence enumeration,
   not through the snapshot I/O seam.

Later-round checkpoint totals (not freshly revalidated): 68 tests after round
1, 72 warning-free after round 2, RH0 accepted at 72 on completion.

### Historical RH1A review findings

Initial RH1A review: 32 control and 20 signal tests passing, nine defects
reproduced; combined with round-2 boundary corrections:

| # | Finding (historical wording, condensed) | Class |
|---|---|---|
| 1 | Parser wrote command, IDs, peer/config fields into caller output before all validation; valid header plus invalid peer/mode/profile/count/seed/reconnect returned failure with partially mutated output | partial output mutation |
| 2 | Parser placed roughly 6 KiB decoded string storage on stack (`str[512]` per known key), unsuitable for target shell reuse | stack budget |
| 3 | Nested object/array skipping counted brackets equally without validating inner JSON grammar, so malformed nesting classified `wrong_type` instead of `syntax` | malformed nested JSON misclassified |
| 4 | State counter wrappers formed `&st->counters[stream]` before stream-index validation | pre-bounds address formed |
| 5 | `counter_submit_scored()` incremented scored but not total submitted count; passing run possible with scored greater than total | scored greater than total false pass |
| 6 | `configure()` mutated configuration after terminal despite immutable post-terminal snapshot contract | terminal config mutation |
| 7 | Signal stage could enter scored before exact preamble completion on every channel, so tests exercised fresh encoder state, not the real preamble lifecycle | premature stage / fake fresh state versus real preamble |
| 8 | Scored entry chose first target from an unadvanced derived PRNG, advancing only after the first block; contract requires one xorshift update per block consumed by the first block | PRNG first update wrong |
| 9 | Generator accepted `--write --check` with write silently winning; doc said I/O error exits 2 while implementation returned 1 | generator write-over-check / IO exit mismatch |
| 10 | Round 2: `hil_source_signal_render()` multiplied `samples * sizeof(int16_t)` unguarded; on 32-bit target `samples=UINT32_MAX` wrapped the size calculation before capacity rejection | 32-bit size overflow; reject samples greater than SIZE_MAX/sizeof(int16_t) before multiply |
| 11 | Round 2 repair contract: return `-ENOSPC` with encoder and output unchanged on that rejection | 32-bit overflow repair contract |
| 12 | Round 2: unknown key longer than 16 decoded bytes returned `range` from key buffer before unknown-key classification; schema promises `unknown_key` for every unknown key within the 511-byte line | long unknown keys |
| 13 | Round 2 repair contract: consume and validate the long key without large per-key storage, parse its value fully, then return `unknown_key` within bound; malformed value still returns `syntax` (lexical validity precedes schema class) | long-unknown-key repair contract |

Later-round checkpoint totals (not fresh evidence): 38 control, 21 signal, 76
Python tests after round 1; inventory 65 across rounds. Historical NCS v3.3.0
note preserved: upstream `json_obj_parse()` accepted unknown, duplicate, and
trailing content, motivating the repository parser; that installed-SDK claim
concerns the historical NCS tree and is not current-SDK verified.

### Historical RH1B observed fault catalog

First RH1B review: fake native path passed while the real production firmware
could not transmit because TX streams were never attached. Full thirteen-item
observed-fault catalog (condensed from the `## Grounded defects` list, lines
38-71 of the first review-fix handoff at the cited rev):

| # | Fault (condensed) | Class |
|---|---|---|
| 1 | `hil_source_tx_attach()` had no caller; `tx_stream_count` stayed zero; first production send returned `-EINVAL`; cleanup set `tx_stopped` with no segment reactivation | missing TX attach / reactivate |
| 2 | Raw `printk("DBG TS ...")` in every sent callback raced queued HIL1 output, violating single-writer ownership | printk/log race |
| 3 | `CONFIG_SHELL_STACK_SIZE=2048` while dispatch nested strict-parser state plus status/hello buffers beyond the budget | shell stack budget |
| 4 | Output line capacity 768 could not honor accepted 63-byte command IDs and 64-byte run IDs; two-stream status already exceeded 768 with maximum IDs | max-ID status above 768 |
| 5 | `CONFIG_NCS_BOOT_BANNER=y` remained resolved, creating non-HIL1 serial text | boot banner |
| 6 | One global Mode A TX activity time: healthy callbacks on one CIS could mask a stuck outstanding queue on the other forever | one CIS masks stall |
| 7 | `sem_configured`/`sem_disabled`/`sem_released` signaled by ASCS response listeners rather than successful endpoint-state callbacks; release received both listener and `stream_ops.released` signals, so one stream could consume another's completion token | listeners duplicate completion tokens |
| 8 | Sink ASEs are server-started; code swallowed `-EINVAL`/`-EBADMSG` from `bt_bap_stream_start()` as success | start errors swallowed |
| 9 | Backend callback/status fields unsynchronized across Bluetooth RX, worker, and shell threads | backend races |
| 10 | Stop-aware wait helper short-circuited cleanup waits after stop; cleanup could reset backend state and return terminal while disable/release/disconnect were still pending | stop skipped cleanup wait |
| 11 | `idle` reset `sem_run_done` after requesting stop, losing a racing worker completion (15-second stall); on timeout it also started a second cleanup concurrently with worker cleanup | idle completion / concurrent cleanup |
| 12 | Worker state/record helpers called without `app_mutex`, racing status dispatch; record submission failure jumped to abort with `err == 0`, losing first errno | unlocked app state / lost record errno |
| 13 | `hil_source_bap_init()` ignored both auth callback registration return values | ignored auth register return |

Historical capacity notes, superseded within the same phase, not current
recipes: 8-line queue with 768-byte lines was the review-era baseline; the
first correction set 1024-byte lines; Mode A outstanding target later changed
from two to three per active stream (depth notes earlier in this file).

### Historical RH1B second- and third-review findings

Second review (after first correction build and 49-case native suite):

| # | Finding (condensed) |
|---|---|
| 1 | `connected_cb()` gave `sem_connected` even when the callback connection was not `default_conn` (wrong connection recorded as success) |
| 2 | PASS state committed before terminal record submission; on submission failure FAIL terminalization was rejected because the snapshot was already an inactive PASS; later status could report pass without a terminal record |
| 3 | Group pointer cleared before `bt_bap_unicast_group_delete()`; failed delete leaked the group, blocked retry, and falsely reported the group absent |
| 4 | Configure, stop, and unpair handlers discarded status-emission errors and returned success (response failures ignored) |
| 5 | Discovery endpoint/completion callbacks did not filter the exact active connection (unfiltered discovery) |
| 6 | Security did not verify level at least L2 plus the exact configured peer bond; `bt_conn_set_security()` may return zero as a no-op on bonded reconnect without a new callback, causing a false 20-second timeout |
| 7 | Cleanup did not reset/read `op_error` around disable/release; an ASCS rejection could look like successful completion, losing `-EBADMSG` as first cleanup error |
| 8 | Several backend operations copied `default_conn` under spinlock, then called a Bluetooth API after unlock without a temporary connection reference; disconnect callback could unref concurrently |
| 9 | Cleanup-order test checked counts, not cross-phase order; record-submission test started from a scripted backend `-EIO`, not from record submission; no test covered PASS terminal submission failure, so failure causes were misattributed |

Third review (after second correction passed 56 native cases and build):

| # | Finding (condensed) |
|---|---|
| 1 | Bonded reconnect with no `security_changed` callback: coordinator woke on the stale security semaphore but rejected level zero, because `security_level_now` was segment-reset and only updated by the callback; the fake's always-L2 default masked this (cached level not updated) |
| 2 | Start-ACK failure path re-acquired `app_mutex` while dispatch already held it, causing permanent deadlock when the queue was full or the formatter failed |
| 3 | Two idle branches cast status emission to void; idle/output failure left responses unpropagated |
| 4 | Parse-error handling could read/write `run_state.active`/`runtime_error` outside the app mutex |
| 5 | Object unrefs under the backend spinlock remained in two configure/QoS branches; the ownership rule's sole exception is the intentional atomic `bt_conn_ref()` acquisition guard that makes a post-unlock call safe |

Historical accuracy notes: the claim that NCS v3.3.0 `json_obj_parse()` accepts
unknown, duplicate, and trailing content was grounding for the repository
parser, not a current-SDK verified fact. The historical "header generation
wording" observation was a wording mismatch without proof of an actually
missing generation token; the dead generation variable was removed as a
write-only pseudo-guard, not a source fix.

## Historical early RH3 diagnostic rationale

This section preserves the August 2026 NCS v3.3.0 investigation, not current
SDK behavior or permission to replay retired configurations. Original texts are
recoverable from Git revision `a94f010`. Raw `/tmp/opencode/hil-runs` evidence is
absent in this session; measurements and checksum counts below are
document-attributed observations, not newly rehashed evidence.

### Startup, retry and callback evidence

The RH3-09 reservoir change used fifteen prefilled blocks without growing the
PCM slab. At the then-measured 47619 Hz drain rate, eleven blocks did not cover
an observed roughly 110 ms Mode B startup gap. Driver descriptors store
pointer/size pairs, not more PCM; transactional ownership still matters.
Historical RAM reached 161668/163840 B with slab size `0x7840`. Dirty report-only
coverage could diagnose the change but could not replace the frozen baseline.

The later 20 ms slab wait was finite backpressure, not real-time safety proof:
it could delay Bluetooth RX work. RH3-10 source completed both streams at
`sub=12644 sc=12000 cb=12644 sf=0`; offload reported `submit=success=14035`,
with zero decode/I2S/reset/push failures. Yet the raw receiver warning
`[00:51:08.410,510] <wrn> bt_conn: conn 0x2000f6d8 failed to establish. RF noise?`
fell between Stream[0] start and Stream[1] start at `00:51:08.719,081`.
The bounded same-CIS retry recovered establishment; strict log scanning correctly
failed. Recovery granted no warning exemption or RF-cause conclusion.

ISO `sent` completion can follow enqueue, transmission or flush, not peer
delivery. Flags classify only delivered callbacks: historical HCI INVALID mapped
to ERROR and NOP to LOST. Absent callbacks cannot be counted by that adapter.
The earlier Mode A record had 12348 submissions per stream, 116 valid receiver
SDUs and `decoded=28006 plc=27890`; one assembler overflow did not explain it.

### Runtime-filtered HCI trace failures

RH3-12 `rh3-20260822-12-runtime-filtered-hci-remove-iso-path-trace` retained
23/23 checksums but failed before trace configuration with `log: command not
found`. Firmware lacked LOG_CMDS. Pre-stream DEBUG risked overflowing the 4 KiB
deferred ring. RH3-13 armed core/driver DEBUG on first `lc3_disable`, requiring
nonnegative source IDs, one arm, both levels 4, no post-arm drops and a post-arm
`0x206f` send. A pre-arm send cannot satisfy that transaction's evidence gate.

Historical normal CPUAPP changed from
`d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723` to
`27df0345258b9d410b102f75f0b6b614e7fbc50856de9d23b946802e0411a0df`;
RH3-13 trace CPUAPP was
`f32fd4452f943bedbacaba4bb8a305c1489ac7d9e4c5c39faa874b9d410f813c`,
fragment `receiver-hci-remove-iso-path.conf` SHA-256
`33f7c2fb49773e8811dfabf09471b6670414bfb5fc2d3fdee1a4f46182b50dc9`.
Source CPUAPP stayed
`f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`,
source CPUNET `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`,
receiver FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Local build restoration was not board restoration.

RH3-13 later failed session end with a missing summary, retaining 25/25
checksums. The exact UART prompt hid drop records from the earlier parser;
arbitrary foreign prefixes are not eligible. A relative SHA256SUMS check outside
its evidence root was an interpretation error, not corrupt evidence. Native
weak entropy imply produced an empty driver library when fake entropy was
disabled; the helper needed no runtime entropy. These were test-local repairs,
not production entropy changes or general warning waivers.

RH3-14 preflight stopped on reserved-memory bus/unit-address warnings before
runner invocation, run directory, JUnit, flash or reset. Repair preserved memory
ranges and phandles; normal local outputs were restored. A plan is not execution.

### Diagnostic eligibility and interpretation

Public read-only ISO link-quality opcode `0x2075` supplies seven counters for a
live CIS. App LOST cannot distinguish missing HCI, CRC or unreceived causes.
Historical SW Split source lacked that public query route. Private ISO handles
and direct RADIO access were not alternatives. An RX-only `can_recv` filter
dependent on BT_AUDIO_TX wrongly excluded live sink CISes: local `-128` differed
from public handle `-ENOTCONN` and SDC status `0x02` mapped to `-EIO`. Removing
that filter retained strict live-sink and response validation. Historical
checkpoints were 45 native, 141 host and 96 build checks, not current counts.

Selected C-to-P fields came from cached establishment events. Host-v1 central
latency truncation and unrelated fallback fields were excluded. Max PDU is not
SDU length; PHY 1/2/4 means 1M/2M/Coded, and interval/flush values use 1.25 ms.
NSE 6, BN 1, FT 1 describes opportunities, not observed retransmissions. Equal
byte rates across 10/7.5 ms still change latency, frame size and ASRC path.

The header-only native seam needed buffer macros through `audio.h`/`buf.h`
assertions but no runtime driver, crypto or entropy. Child ISO options assigned
with disabled BT_CONN/ISO parents caused warnings. The fully BT-off proposal
failed compile; later type/header-only config corrected it. Enum references and
reordered suffix rejection were proved separately from ledger indentation.
Historical preflight cited 134 host, 47 native and 96 build checks, with DTS
warnings later repaired. Source images
`b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317` and
`4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`
were unchanged by that receiver-only build. Partition-manager, SW Split,
advanced-feature and watchdog notices belonged to old configs, not v3.4.1.

### Collection ordering and fault windows

A four-second usable tail, about 4.067 s in historical alignment, was too short
for synchronous ISO HCI work followed by a second active-offload query. Adopted
ordering collects active offload before scored completion, live ISO during tail
and STOPPED diagnostics after teardown. The old collector returned after first
Mode A summary even when both were in raw UART. Required slots, later faults
and duplicates must all be checked. A moving single pending transaction can
prove serialized progress; static submit=success+1 cannot. Historical 8 ms lock
and submit-before-commit ordering explain why adjacent samples need care.

Named recovery comparisons normalize an omitted Runtime line to zero only
there. Normal stop resets probation_success progress; cumulative probation_cleared
remains evidence. Submit non-regression and other faults stay checked. Zero-wait
cleanup drains queued records before sending STOP/IDLE; queued parse-error/unbound
after PASS remains a retained cleanup failure, never silently discarded.

Expected hang warnings are allowed only as complete lines wholly inside retained
raw-byte injection/recovery bounds. Boundary-crossing, identical pre/post-window,
unrelated or stall-row warnings fail. The fixed two-pass matrix has no arbitrary
skip, repetition or row-list escape hatch. CLI success/failure/cancellation/config
statuses are 0/1/130/2; runner-caught validation can instead produce row status 1.
Artifact plumbing and later v3.4.1 session helpers do not prove PB-007 acceptance.
