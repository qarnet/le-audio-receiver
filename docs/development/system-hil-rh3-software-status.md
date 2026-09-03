# RH3 matrix software status

Status: evidence-review and planning phase after direct Mode A and current-image
mono, Mode B, selected-layout Mode B, selected-layout mono, selected-layout
Mode A, and selected-layout Mode B 7.5 ms physical diagnostic completion, with
twenty live RH3 runs recorded: fifteen failed evidence, one cancelled evidence,
and four passed direct controls. No RH3, RH4,
release, or analog acceptance verdict occurred.

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
