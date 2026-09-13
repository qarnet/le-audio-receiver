# System HIL RH3 physical execution handoff

Status: internal execution handoff only. Eight RH3 physical runs are recorded:
runs `01` through `04`, `06`, `07`, and `08` failed in fresh rows, and `05` was cancelled by the host
executor timeout. Remaining replacement execution and named fault injection are
authorized, but this document is not an acceptance record and must not be used
to claim RH3 acceptance before evidence review.

## Gate and baseline

User explicitly authorized remaining USB/HIL work, including fresh replacement
execution and named fault injection. Matrix runner solely owns connected USB
hardware, flashing, reset, recovery, and FLPR actions.

RH2 formally passed once at
`/tmp/opencode/hil-runs/rh2-20260815-14/`:

- result: `passed`;
- source final: `sub=764/cb=764/sc=120/sf=0/out=0`;
- receiver: `764` SDUs;
- receiver decode, I2S, reset, and push faults: zero.

## Image identity and preflight

Recorded RH2 image SHA-256 values remain historical RH2 evidence. The RH2
source CPUAPP image is pre-fix and must not be used for a future physical RH3
candidate:

- historical RH2 source CPUAPP: `316781a6ee7fea24c843541e48a9176967fb8bce52edea8208b4569058e8e81f`;
- historical RH2 source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver cpuapp: `4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

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

RH3-07 exact diagnosis and correction: CIS 0 started, CIS 1 failed
establishment and emitted `stream_disconnected_cb()`, but the source ignored
that callback and waited for the full stream-connect timeout. The worker now
consumes the callback completion, waits exactly 10 ms without `app_mutex`, and
retries only the same stream once. A second failed-CIS outcome returns `-EIO`
and takes normal error teardown. This is based only on public NCS v3.3.0
BAP/ISO behavior; callbacks do not call BAP/ISO APIs and no HIL1 schema field
changed. The correction is software-only, not a physical RH3 run.

Native source app Twister passed `67/67`, including transient recovery and
persistent-failure coverage; `fw-build-hil-source` passed; and
`git diff --check` passed. The current source artifacts after the Mode A
busy-retry, CIS-connect serialization, and failed-CIS retry fixes are
software-verified only. They are not RH3 acceptance or hardware proof. Current
source artifact identities prepared for a future reviewed RH3 preflight are:

- CPUAPP (`build/hil-source/app/zephyr/zephyr.hex`): `b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317`;
- CPUNET (`build/hil-source/hci_ipc/zephyr/zephyr.hex`): `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- merged app (`build/hil-source/merged.hex`): `e45444024ccfba6ace276126568d3b89d19896dbe5440fc65a3baab992476004`;
- merged CPUNET (`build/hil-source/merged_CPUNET.hex`): `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0`.

No physical run occurred for this correction. RH3 evidence
`rh3-20260815-01` through `rh3-20260815-07` remains immutable evidence and is
not acceptance.

Receiver CPUAPP and FLPR stay at their separately recorded RH2 identities
above unless changed and reviewed. Do not rebuild during physical matrix
execution. Preflight must verify the current source artifact identity. Any
later source rebuild or configuration change needs review and updated identity
before execution.

## Physical RH3 run evidence

### `rh3-20260815-01`

Immutable failed evidence. Fresh mono passed. Fresh Mode A failed before
streaming after QoS with `first_errno=-16`, caused by the original bulk enable
submission diagnosis.

- Failed child:
  `/tmp/opencode/hil-runs/rh3-20260815-01.children.855cf5240f2e/rh3-20260815-01.p1.r2.rh3.fresh_mode_a_48_4_1.c8e167dc0e97/`
- Exact source evidence:
  `/tmp/opencode/hil-runs/rh3-20260815-01.children.855cf5240f2e/rh3-20260815-01.p1.r2.rh3.fresh_mode_a_48_4_1.c8e167dc0e97/source-records.jsonl`

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

This revealed state-callback versus local GATT busy-clear ordering. The new
source retry fix is software-verified only, not hardware proof.

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
two-CIS `bt_bap_stream_connect()` submission as root cause. IDs `01` through
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

Receiver evidence recorded one `i2s_nrfx: Next buffers not supplied on time`,
maximum RX callback gap `268003 us`, and `8884` PLC frames. FLPR stayed healthy
with `submit=success=12650`, zero fallback, zero busy, and zero faults. Source
terminal status was `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`, `out=0`.
RH3-06 used the same source and receiver image identities as the passing RH3-04
fresh mono child:

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

Failed child result records first boundary `receiver tail` and exact detail
`invalid receiver status: I2S underruns=1; Stream resets=1; Push failures=1;
offload submit/success mismatch`. Failed-child `images.json` records source
CPUAPP `b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317`,
source CPUNET
`4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`, receiver
CPUAPP `4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`,
and receiver FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Relevant raw receiver evidence is retained at
`/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/receiver-status.txt`:
`i2s_nrfx: Next buffers not supplied on time`,
`i2s_nrfx: Cannot write in state: 4`,
`audio_i2s: I2S underrun, restarting DMA`, `I2S underruns : 1`,
`Stream resets : 1`, `Push failures : 1`, and offload snapshots
`submit=14223 success=14222`, `submit=14253 success=14252`, and
`submit=14278 success=14277`. Source records are retained at
`/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/source-records.jsonl`;
they record `streaming`, `scored_complete`, counters `sub=12320`, `sc=12000`,
`sf=0`, `cb=12320`, `out=0`, teardown cause `stop`, and terminal verdict
`fail`.

Aggregate and all three attempted child directories retain `result.json`,
JUnit, `MANIFEST.md`, and `SHA256SUMS`. External JUnit records `tests=20`,
`failures=1`, `skipped=17`, and `errors=0`. Review criteria are not met.
Physical RH3 acceptance is absent. Preserve evidence exactly; do not retry a
child or launch another matrix from this phase.

## Existing implementation

Matrix implementation already exists. It is not new work:

- `scripts/hil/rows.py` owns frozen rows and `RH3_PASS_ROWS`;
- `scripts/hil/matrix.py` owns fixed schedule, fail-fast behavior, and
  aggregate evidence;
- `scripts/hil/cli.py` owns `run-rh3-matrix`.

The direct wrapper `scripts/hil-runner.py` has mode `755`. Direct no-hardware
`validate` invocation already verified this path. Use the direct wrapper, not a
replacement Python invocation.

## Physical fixture

Runner resolves current identities. Do not add static probe mapping to code.

- Receiver: XIAO nRF54L15, probe `8EE9B3FF`, console `/dev/ttyACM2`, DTR true,
  RTS false.
- Source: nRF5340DK, J-Link `001050023938`, console `/dev/ttyACM1`, DTR true,
  RTS false.

Recorded receiver fingerprint is DPIDR `0x6ba02477`, PART `0x00054b15`,
VARIANT `AAC0`. The local binding records exact source J-Link
`ID_SERIAL_SHORT=001050023938` and family `nrf53`; execution evidence must
retain runner-resolved source target fingerprint data.

## Radio and configuration invariants

- Receiver RF power stays `0 dBm`.
- Source RF power stays `+3 dBm`.
- Source outstanding target stays `2`.
- Source host TX buffers and controller TX buffers stay `6`.

During physical matrix execution, do not rebuild, change configuration, or
adjust RF power manually. Complete current-source hash preflight before
starting the matrix.

## Frozen RH3 schedule

`RH3_PASS_ROWS` contains seven healthy rows, then preserved-bond reconnect,
FLPR hang, and FLPR stall. `RH3_PASS_COUNT=2`. Schedule has exactly `20`
child rows. Each pass starts with its fresh-pair row. Matrix must not be
manually rerun.

| Row | Frozen name | State | Mode | Profile | Scored SDUs | Special behavior |
| --- | --- | --- | --- | --- | ---: | --- |
| 1 | `rh3.fresh_mono_48_4_1` | fresh | mono | `48_4_1` | 12000 | fresh pair |
| 2 | `rh3.fresh_mode_a_48_4_1` | fresh | Mode A | `48_4_1` | 12000 | none |
| 3 | `rh3.fresh_mode_b_48_4_1` | fresh | Mode B | `48_4_1` | 12000 | none |
| 4 | `rh3.fresh_mono_48_3_1` | fresh | mono | `48_3_1` | 16000 | none |
| 5 | `rh3.fresh_mode_a_48_3_1` | fresh | Mode A | `48_3_1` | 16000 | none |
| 6 | `rh3.fresh_mode_b_48_3_1` | fresh | Mode B | `48_3_1` | 16000 | none |
| 7 | `rh3.preserved_mode_b_48_4_1` | preserved | Mode B | `48_4_1` | 12000 | healthy preserved bond |
| 8 | `rh3.reconnect_mode_b_48_4_1` | preserved | Mode B | `48_4_1` | 12000 | reconnect once |
| 9 | `rh3.flpr_hang_mode_b_48_4_1` | preserved | Mode B | `48_4_1` | 12000 | FLPR hang |
| 10 | `rh3.flpr_stall_mode_b_48_4_1` | preserved | Mode B | `48_4_1` | 12000 | FLPR stall |

Streaming alone exceeds 46 minutes: 22 segments across two passes, each about
126 seconds. Per-child flash, boot, pairing, fault recovery, and evidence add
unknown time. Do not promise total wall-clock time.

## Matrix ownership and prohibitions

Matrix runner solely owns all flash, reset, console, pairing, cleanup, and
evidence actions. Do not use `serial-mcp`, `btattach`,
`scripts/bap_central.py`, manual serial, flashing, reset, another serial reader,
blanket erase/recovery, or manual FLPR shell commands. Do not rebuild source
during execution or run a source build and `tests/hil` concurrently.

## Previous execution record and current prohibition

The consumed attempts used these destinations:

- `/tmp/opencode/hil-runs/rh3-20260815-01/`;
- `/tmp/opencode/hil-runs/rh3-20260815-01.children.855cf5240f2e/`;
- `/tmp/opencode/hil-runs/rh3-20260815-01.junit.xml`.
- `/tmp/opencode/hil-runs/rh3-20260815-02.children.f5b5588ed7f5/`;
    - `/tmp/opencode/hil-runs/rh3-20260815-03.children.898a3f645e81/`;
    - `/tmp/opencode/hil-runs/rh3-20260815-04/`;
    - `/tmp/opencode/hil-runs/rh3-20260815-04.children.9f096d25eb41/`.
- `/tmp/opencode/hil-runs/rh3-20260815-05/`;
- `/tmp/opencode/hil-runs/rh3-20260815-05.children.cb825a82f91c/`.
- `/tmp/opencode/hil-runs/rh3-20260815-06/`;
- `/tmp/opencode/hil-runs/rh3-20260815-06.children.54d1839d896c/`.
- `/tmp/opencode/hil-runs/rh3-20260815-07/`;
- `/tmp/opencode/hil-runs/rh3-20260815-07.children.0e29ddf55210/`.

Runs `01` through `04`, `06`, `07`, and `08` are immutable failed-run evidence;
`05` is immutable cancelled evidence. Do not delete or overwrite them. Do not
retry a child, reuse an old ID, or claim any of them as acceptance.

The commands below are retained as historical invocation shape only. Do not
execute them as a retry:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Historical matrix invocation:

```bash
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260815-01 \
  --junit /tmp/opencode/hil-runs/rh3-20260815-01.junit.xml
```

RH3-07 and RH3-08 are immutable failed evidence, not acceptance. Do not retry
any child or launch another matrix from this phase. Do not add row, repeat, skip, no-flash,
recovery, erase, image, or command options, and do not manually restart a
stopped matrix. Never retry, delete, overwrite, or reuse IDs `01` through `08`.

## Success review

Review evidence before any claim. A software matrix result qualifies for review
only when all of these hold:

- CLI exit is `0`.
- Aggregate `result.json` has outcome `passed`.
- Aggregate counts are `scheduled=20`, `attempted=20`, `passed=20`,
  `failed=0`, `cancelled=0`.
- Aggregate cleanup failures are empty.
- Aggregate and every child retain `result.json`, `junit.xml`, `MANIFEST.md`,
  and `SHA256SUMS`.
- Aggregate external JUnit has `20` tests and zero failure, error, or skipped
  cases.
- Every source final status passes the frozen per-row parser. For normal
  one-segment `48_4_1` rows, expected terminal counters are
  `sub=12644/sc=12000/cb=12644/out=0`. For normal one-segment `48_3_1` rows,
  they are `sub=16859/sc=16000/cb=16859/out=0`. Reconnect has two segments and
  doubled exact terminal counters: `sub=25288/sc=24000/cb=25288/out=0`.
  Let runner own exact parser validation.
- Receiver faults are zero. Healthy 10 ms paths show FLPR `ACTIVE`. A snapshot
  with exactly one pending FLPR submit is re-polled for up to `0.5 s`, and final
  validation still requires `submit==success`. The 7.5 ms paths may use CPUAPP
  fallback and must remain fault-free. Hang and stall rows must satisfy
  runner-defined recovery evidence, not the normal-row zero-recovery
  interpretation.
- Raw logs contain no unexpected warning, error, assert, or fault. Only the
  narrowly named fault-window behavior accepted by the runner is exempt.

This result does not prove analog output, DAC wiring, audibility, RH4 artifact
acceptance, release, or system audio.

## Failure or cancellation procedure

Matrix stops on first failure or cancellation. After that:

- do not run another matrix or retry a child;
- do not change power, rebuild, reset manually, recover, or delete evidence;
- preserve aggregate and child evidence exactly as written;
- report aggregate `result.json`, first child `result.json`, raw console lines,
  image hashes, child records, command ledger, and the exact failure
  classification question to the orchestrator.

Do not call a partial or failed run acceptance evidence before review.

## Follow-up

After either passed, failed, or cancelled result, update
`docs/development/system-hil-resume-state.md` only with verified evidence. Do
not change `STATUS.md` or claim RH3, RH4, release, or analog acceptance without
evidence review.
