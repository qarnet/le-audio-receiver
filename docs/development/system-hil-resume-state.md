# System HIL resume state

> [!WARNING]
> Working restart handoff, not an acceptance record. Read this before resuming
> physical HIL work. Do not claim RH2, RH3, or hardware acceptance from this
> file.

## Current standing lab hardware authority (2026-08-23)

This current policy supersedes only forward-looking manual-hardware restrictions
elsewhere in this restart handoff. It does not rewrite recorded run facts,
immutable IDs, results, hashes, or historical execution wording.

- All attached Nordic nRF boards may be read, debugged, flashed, reset, erased,
  or recovered without fresh approval, subject to target and tooling support.
- Before each target-changing action, run `nrf-probes` or the appropriate
  project identity resolver, retain raw identity evidence, and never touch
  unknown or non-Nordic hardware.
- `scripts/hil-runner.py` remains useful for formal end-to-end and matrix
  evidence, but it is not the sole permitted owner for direct diagnostic work.
- Preserve immutable run evidence and all central-only pairing and streaming
  requirements.
- A simulator-reported behavior is evidence, not automatic proof of a
  production firmware defect. Where practical, test it on physical nRF hardware
  before accepting a behavior-changing source fix, then retain both simulator
  and board evidence.

## Repository state

- Branch at RH3-ModeB control execution:
  `feature/firmware-release-acceptance`.
- Normal build and runner HEAD:
  `f1c13f0273f653068efe4205a97a15f72d06915b`.
- At execution, `git status --porcelain` contained only the requested,
  initially untracked RH3-ModeB control handoff. `git diff --check` passed; no
  production or unrelated tracked change was present.
- CPUAPP image hashes are HEAD-dependent because `cmake/version.cmake` embeds
  `APP_COMMIT`. Future handoffs must derive CPUAPP identities at execution time;
  diagnostic images require a same-HEAD double-build byte-identity proof. Do not
  pin a CPUAPP hash from an earlier commit as a future expected value.
- `tests/hil/fixture.local.json` is gitignored. It is local fixture state and
  must not be committed.
- Retained top-level physical-run count is now twenty-nine: twenty-two failed,
  one cancelled, and six passed direct diagnostic/control executions. Matrix
  child rows remain counted in their matrix aggregate, not as additional
  top-level direct runs. H40 and H42 are passed bounded diagnostics, not
  acceptance evidence.
- Latest matrix attempt: `rh3-matrix-20260903-rh3a` ran once under frozen
  transport limits. The outer command returned `matrix_status=1`; aggregate
  result was `failed` with 14 scheduled children, 2 attempted and completed,
  1 passed, 1 failed, 0 cancelled, and 12 not attempted. Pass 1 fresh mono
  passed. Pass 1 fresh Mode A failed at `session end` with exact detail
  `missing receiver stream summary slot(s): [0, 1]`; retained raw summaries
  also record the transport-limit violations documented in
  `docs/development/system-hil-rh3-matrix-20260903-result.md`. This is not RH3
  acceptance and must not be retried in this phase.
- Latest direct RH3 Mode A diagnostic:
  `rh3-modea2-20260904-offload-disabled` ran once with offload compiled out and
  repaired runner support (`--allow-offload-disabled` plus full-segment raw
  summary scan). The outer command returned `status=1`; `result.json` records
  `outcome=failed` at `session end` with runner-validated frozen transport-limit
  failures: slot 0 `rx_valid=130` below `11379` and `plc=28594` above `1436`.
  Active FLPR correctly retained `submit=success=0`; post-stop FLPR was not
  collected because limits validation failed first. This selects the
  FAIL-on-limits isolation arm: transport/controller path implicated and FLPR
  cleared as primary suspect for this Mode A starvation. Preserve
  `/tmp/opencode/hil-runs/rh3-modea2-20260904-offload-disabled/`; do not rerun
  it. Canonical record:
  `docs/development/system-hil-rh3-modea2-result.md`.
- Latest direct RH3 Mode B control:
  `rh3-modeb-control-20260904` ran exactly once with the normal production
  image. The outer command returned `status=1`; `result.json` records
  `outcome=failed` at `session end` with runner-validated frozen transport-limit
  failures: slot 0 `rx_valid=113` below `11379` and `plc=27196` above `1371`.
  Active FLPR settled at `ACTIVE`, `submit=96 success=96`; post-stop FLPR was
  not collected because limits validation failed first. This selects the
  FAIL-on-limits control arm: collapse is broader than dual CIS, mono is the
  only healthy shape, and next work refocuses on the `240`-byte SDU versus
  `120`-byte mono transport difference. Preserve
  `/tmp/opencode/hil-runs/rh3-modeb-control-20260904/`; do not rerun it.
  Canonical record:
  `docs/development/system-hil-rh3-modeb-control-result.md`.
- Latest direct RH3 Mode B RX-timing diagnostic:
  `rh3-modeb-rxtiming-20260904` ran exactly once with the default-off
  `HIL_RX_TIMING_TRACE` instrument enabled. The outer command returned
  `status=1`; `result.json` records `outcome=failed` at `session end` with the
  same runner-validated frozen transport-limit failure, `rx_valid=113` below
  `11379` and `plc=27196` above `1371`. The 200 bounded callback records show
  113 valid callbacks in preamble sequences 0 through 143, last valid sequence
  140, then consecutive LOST callbacks from sequence 141; source sequence 144
  is scored onset. All 135 retained per-second summaries from `t=12s` through
  `t=146s` have zero valid and 99 to 101 LOST callbacks. This supports the
  scored-onset-correlation branch with a bounded 30 ms lead, not a root-cause
  claim. Preserve `/tmp/opencode/hil-runs/rh3-modeb-rxtiming-20260904/`; do not
  rerun it. Canonical record:
  `docs/development/system-hil-rh3-modea4-rxtiming-result.md`.
- The prior `rh3-modea1b-20260903-offload-disabled` direct diagnostic remains
  immutable fixture-defect baseline evidence. It stopped at `session end`
  before a runner-validated limits verdict and is documented in
  `docs/development/system-hil-rh3-modea1b-result.md`.
- The earlier `rh3-modea1-20260903-offload-disabled` run remains immutable
  fixture-defect baseline evidence. It stopped at `receiver active` before the
  repaired offload-disabled predicate and is documented in
  `docs/development/system-hil-rh3-modea1-result.md`.
- System HIL plan of record revised 2026-09-03
  (`docs/development/system-hil-milestones.md`): nRF54L15 is the only
  production receiver target (nRF5340 release track eliminated from the plan;
  the nRF5340DK keeps only the HIL source-fixture role); 7.5 ms `48_3_1` rows
  are removed from the mandatory matrix pending the named RH3-7p5 phase
  (fix-and-reinstate or remove 7.5 ms advertisement); receiver transport
  limits are frozen (`rx_valid` >= 90% of expected submitted, `plc` <= 5% of
  decoded, `rx_error`/`rx_unknown`/`empty_sdu` = 0) and enforced by the
  runner at session end (RH3a, host-verified: `tests/hil/` 290 passed).
  The mandatory matrix is 4 healthy 10 ms rows + reconnect/hang/stall,
  two passes (14 child runs). Reruns are the fix-validation mechanism;
  only blind unclassified retries are prohibited.
- CIS-layout telemetry was build-verified before physical diagnostics, then
  flashed in receiver CPUAPP image
  `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723`.
  `bt iso quality` appends strict selected C-to-P CIS fields:
  `iso_interval_1250us`, `nse`, `cig_sync_us`, `cis_sync_us`, `c_max_pdu`,
  `c_phy`, `c_bn`, `c_flush_1250us`.
- The latest runner-owned flash is `rh3-modeb-rxtiming-20260904`. Its
  authoritative `images.json` hashes are diagnostic receiver CPUAPP
  `8442e97190bc24a9d090270ba2375bf81f5326f69038c23f49b01f4d1b991153`, FLPR
  `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`, source
  CPUAPP `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`,
  and source CPUNET
  `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`.
  The diagnostic build proved `CONFIG_HIL_RX_TIMING_TRACE=y`,
  `CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_TRACING` and
  `CONFIG_HIL_BAP_ENABLE_TRACE` unset, and `CONFIG_BT_ISO_RX_BUF_COUNT=3`.
  A later local normal build proved `CONFIG_HIL_RX_TIMING_TRACE` unset and
  `CONFIG_AUDIO_OFFLOAD_ASRC=y`; it did not flash either target.
- Source image hashes remain as before: CPUAPP
  `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` and
  CPUNET
  `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`.
- Host verification remains: `audio_shell_nrf54` 45/45, RH2 154/154, capture
  19/19, both receiver target builds passed, and build contract 96/96. The
  selected-layout preflight also passed `py_compile`, fixture validation, all
  four image hashes, and `git diff --check`; four selected-layout hardware/HIL
  direct diagnostics used this receiver image. No HIL-source build was run.
- Host J-Link fingerprints use current OpenOCD port-command syntax: `gdb port
  disabled`, `tcl port disabled`, and `telnet port disabled`. This run's source
  J-Link evidence contains no legacy `gdb_port`, `tcl_port`, or `telnet_port`
  deprecation line. Old immutable RH3 evidence still retains its recorded
  deprecation messages.
- Selected-layout telemetry is layout evidence only; no QoS, controller, RF,
  threshold, causal, or acceptance claim changed.
- The authorized direct runner diagnostic
  `rh3-20260822-01-modea-tail-order-cleanup` was run once and failed at the
  receiver tail. Its retained `result.json` and JUnit outcome are `failed`; no
  runner process exit code is retained. It produced no acceptance result and
  must not be retried. Matrix runner remains sole owner of hardware actions.
- The authorized direct runner diagnostic
  `rh3-20260822-02-modea-iso-parser-fix` was run once. The direct runner command
  has no retained process exit code in its evidence root. Its retained result,
  JUnit, and manifest outcome was `failed` at the receiver tail because
  `invalid receiver status: offload state='STOPPED'`. Cleanup failures were
  empty and evidence integrity passed `23/23` SHA-256 entries. The repaired ISO
  parser accepted the real transcript with an exact header and two stream
  records; later receiver-status validation failed. It produced no acceptance
  result and must not be retried. Matrix runner remains sole owner of hardware
  actions.
- The authorized direct runner diagnostic
  `rh3-20260822-03-modea-critical-tail-snapshot` was run once. Retained
  `result.json`, JUnit, and manifest outcome was `failed` at the receiver tail
  because `invalid receiver status: offload state='STOPPED'`; retained
  `environment.json` records runner command status `0`. Cleanup failures were
  empty and evidence integrity passed `23/23` SHA-256 entries. It retained the
  repaired live ISO transcript and a post-teardown `STOPPED` FLPR snapshot with
  equal submit/success counters, but no acceptance result was produced. This
  diagnostic must not be retried. Matrix runner remains sole owner of hardware
  actions.
- The authorized direct runner diagnostic
  `rh3-20260822-04-modea-lifecycle-split` was run once. Retained
  `environment.json` records direct runner command status `0`; retained
  `result.json`, JUnit, and manifest outcome was `failed` at first boundary
  `log scan`. The retained receiver warning was
  `[00:42:49.737,888] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?`.
  Cleanup failures were empty and evidence integrity passed `26/26` SHA-256
  entries. Active-to-stopped lifecycle records, live ISO quality, source
  terminal PASS, and final receiver diagnostics were retained. This diagnostic
  is not acceptance evidence and must not be retried. Matrix runner remains
  sole owner of hardware actions.
- The authorized direct runner diagnostic
  `rh3-20260822-05-current-image-mono-control` was run once with direct runner
  command status `0`. Its retained `result.json`, JUnit, and `MANIFEST.md`
  outcome was `passed` for `rh3.fresh_mono_48_4_1`, with no failed boundary,
  no failure detail, and `cleanup_failures=[]`. Evidence integrity passed
  `26/26` SHA-256 entries. This is a control result, not acceptance evidence,
  and must not be retried. Matrix runner remains sole owner of hardware
  actions.
- The authorized direct runner diagnostic
  `rh3-20260822-06-current-image-modeb-control` was run once with direct runner
  command status `0`. Its retained `result.json`, JUnit, and `MANIFEST.md`
  outcome was `passed` for `rh3.fresh_mode_b_48_4_1`, with no failed boundary,
  no failure detail, and `cleanup_failures=[]`. Evidence integrity passed
  `26/26` SHA-256 entries. This is a Mode B control result, not acceptance
  evidence, and must not be retried. Matrix runner remains sole owner of
  hardware actions.
- The authorized direct runner diagnostic
  `rh3-20260822-07-modeb-selected-layout` was run once with direct runner
  command status `0`. Its retained `result.json`, JUnit, and `MANIFEST.md`
  outcome was `passed` for `rh3.fresh_mode_b_48_4_1`, with no failed boundary,
  no failure detail, and `cleanup_failures=[]`. It used receiver telemetry image
  `d8e57082564cca70ae00d4d0a4653a00b34743c08075685e2ffea7721ce8a723` and
  captured one complete selected Mode B layout record. This is not a retry of
  RH3-06 or acceptance evidence. Evidence integrity passed `26/26` SHA-256
  entries. Matrix runner remains sole owner of hardware actions.
- The authorized direct runner diagnostic
  `rh3-20260822-10-modeb-7p5-selected-layout` was run once with direct runner
  command status `0`. Its retained `result.json`, JUnit, and `MANIFEST.md`
  outcome was `failed` at `session end` because receiver stream summary slot `0`
  was missing; `cleanup_failures=[]`. Evidence integrity passed `26/26` SHA-256
  entries. It retained one selected 7.5 ms Mode B layout record and active
  CPUAPP-ASRC zero-plane evidence, but no post-stop FLPR snapshot or receiver
  stream summary. This is not acceptance evidence and must not be retried.
  Matrix runner remains sole owner of hardware actions.
- No serial-mcp connection remains open.

## Physical fixture

| Role | Hardware | Probe identity | Console binding |
| --- | --- | --- | --- |
| Receiver | XIAO nRF54L15 | `8EE9B3FF`, nRF54L15, DPIDR `0x6ba02477`, PART `0x00054b15` | `/dev/ttyACM2`, USB interface `02`, 115200, DTR true, RTS false |
| Source | nRF5340DK | J-Link `001050023938` | `/dev/ttyACM1`, USB interface `02`, 115200, DTR true, RTS false |

`/dev/ttyACM0` on the nRF5340DK is the network-core VCOM. It is not the HIL
source application console.

HIL evidence root: `/tmp/opencode/hil-runs`.

## Current hardware state

- Receiver remains at `0 dBm`.
- Source remains at `+3 dBm`.
- Source software outstanding target is now `3` per active Mode A stream,
  changed from `2`; the target-three source image has received ten direct
  physical diagnostics, recorded below, with no acceptance execution.
- Source host and controller ISO TX buffers remain `6`.

## Fixes after `rh2-20260815-12`

1. `hil/source/app/src/hil_source_app.c` now uses completion-driven
   `sem_tx_wake` pacing instead of fixed 10 ms waits. Lifecycle stop, idle, and
   error wake behavior is included.
   - Focused Twister: `62/62` passed four times.
   - Deliberately restored polling mutation: `61/62` failed.
   - HIL source build: passed.

2. `scripts/hil/serial_io.py` synchronizes serial descriptor close with the
   reader pump gate. Expected post-close read failure is shutdown, not a row
   failure. Genuine active-console errors remain failures.

3. Host receiver-tail validation now re-polls a healthy 10 ms snapshot with
   exactly one pending FLPR submit for up to `0.5 s`; the final validator still
   requires `submit==success`.

4. Existing source Mode A enable remains strictly serial. After stream 0
   reaches the remote enabled state, stream 1 retries only synchronous `-EBUSY`
   at a `10 ms` interval for at most `1000 ms`, checks stop/fatal state before
   and after waits, then waits for real enabled completion. Remote
   `stream_ops.enabled` does not prove that the NCS local long-GATT write busy
   flag cleared.

5. `kick_stream_connect` now accepts one stream index. The coordinator
   serializes Mode A separate-CIS connects: kick stream 0, wait for real public
   `.connected` completion or error, then kick stream 1. NCS
   `bt_bap_stream_connect()` submits one CIS and rejects another pending CIS
   with `-EBUSY`; no public BAP batch connect exists. No private API is used.

6. RH3-07 diagnosis and source-fixture correction: CIS 0 started, CIS 1 failed
   establishment and emitted `stream_disconnected_cb()`, but the source ignored
   that callback and waited for the full stream-connect timeout. The callback
   now publishes a retryable completion for the matching stream. The
   coordinator worker checks stop/fatal state, sleeps 10 ms without
   `app_mutex`, and retries only that same stream once. A second failed-CIS
   outcome returns `-EIO` and takes normal error teardown. This uses only public
   NCS BAP/ISO behavior and keeps all BAP lifecycle calls in the worker.

 7. RH3 Mode A source-depth diagnostic correction: the fixed outstanding target
   changed from two to three per active stream. The existing six host ISO TX
   buffers and six controller ISO TX buffers remain unchanged, so two active
   Mode A streams can use the full six-buffer source pool while preserving
   lockstep pair submission and completion backpressure. NCS v3.3.0 HCI IPC
   central ISO configuration and BAP unicast client pool-fill behavior motivated
   this unproven source-fixture change. Native regression and source build
   verification passed; no artifact was flashed during this software phase.
    The target-three source image later received eleven direct physical diagnostics,
   recorded below, with no acceptance execution.

8. Host receiver evidence now follows stream lifecycle boundaries: active FLPR
   offload is captured after source `streaming`, CIS-dependent ISO quality is
   captured at `scored_complete`, and audio/performance, stopped FLPR counters,
   and handshake diagnostics are captured after receiver stream summaries.
    Strict validation proves active-to-stopped progress without treating terminal
    `STOPPED` as active evidence. This split-repair has one direct diagnostic
    below, retained active-to-stopped lifecycle boundaries, and failed at
    `log scan` on one receiver warning. It is not acceptance evidence.

The current source artifacts after the failed-CIS retry and Mode A source-depth
corrections are
software-verified only. They are not RH3 acceptance or hardware proof. Current
source artifact identities prepared for a future reviewed RH3 preflight are:

- CPUAPP (`build/hil-source/app/zephyr/zephyr.hex`): `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- CPUNET (`build/hil-source/hci_ipc/zephyr/zephyr.hex`): `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- merged app (`build/hil-source/merged.hex`): `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472`;
- merged CPUNET (`build/hil-source/merged_CPUNET.hex`): `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0`.

Receiver CPUAPP and FLPR stay at their separately recorded RH2 identities
below unless changed and reviewed. Do not rebuild during physical matrix
 execution. Preflight must verify the current source artifact identity. Any
later source rebuild or configuration change needs review and updated identity
before execution.

## Verification completed

- `tests/hil/rh2_test.py`: `143/143` passed.
- `tests/hil/rh3_matrix_test.py`: `14/14` passed.
- Native source app Twister: `68/68` passed, including transient and persistent failed-CIS retry coverage and the exact three-per-stream Mode A lockstep/backpressure regression.
- `fw-build-hil-source`: passed.
- Compileall: passed.
- `git diff --check`: passed.

The failed-CIS correction was software-only. The target-three source image
later received eleven direct physical diagnostics, recorded below. Existing RH3
evidence remains immutable and is not acceptance evidence; no acceptance claim
follows from this verification.

Source build and host regression ran sequentially, not concurrently, because
the source build and `tests/hil` both touch `build/hil-source`. Never run them
concurrently.

## Physical RH2 evidence

### `rh2-20260815-12`

Source-to-receiver transport was healthy. Runner failed only from a serial
close/read race:

```text
receiver console read failed: 'NoneType' object cannot be interpreted as an integer
```

Source final: verdict `pass`, `sub=764`, `cb=764`, `sc=120`, `sf=0`, `out=0`.
Receiver: `SDUs=764`, `decoded=777`, `PLC=13`; decode errors, I2S underruns,
stream resets, and push failures were all `0`.

### `rh2-20260815-13`

Failed before stream due real RF connection-establishment loss. Receiver log:

```text
bt_conn ... failed to establish. RF noise?
Security changed: level 1 err 9 bonded 0
Disconnected ... reason 0x3e
```

Source timed out while connecting after roughly 20 s, with `first_errno=-116`.
Source later asserted while HCI Disconnect `0x0406` response timed out. Retain
this run as evidence of connection-establishment loss, not transport-pacing
regression.

### `rh2-20260815-14`

Formally passed one RH2 `rh2.short_mono_48_4_1` witness. CLI exit was `0`, the
result outcome was passed, and there was no failed boundary or cleanup failure.

Source lifecycle reached:

```text
secured -> discovered -> qos -> streaming -> scored_complete -> teardown -> idle
```

Source final verdict: `pass`, `sub=764`, `cb=764`, `sc=120`, `sf=0`, `out=0`.
Receiver summary: `SDUs=764`, `decoded=777`, `PLC=13`; all decode, I2S
underrun, stream reset, and push faults were `0`. Handshake was healthy. FLPR
offload was `ACTIVE` with `submit=success=309`.

Historical RH2 image SHA256 values, same as run 13. The source app value is
pre-fix Mode A evidence and is not the future RH3 source identity:

- Source app: `316781a6ee7fea24c843541e48a9176967fb8bce52edea8208b4569058e8e81f`
- Source cpunet: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`
- Receiver cpuapp: `4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`

## Physical RH3 evidence

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
`submit=12191 success=12190 fallback=0 busy=0`. This drove the host tail-settle
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

Immutable failed evidence, not acceptance. Aggregate: scheduled `20`, attempted
`2`, passed `1`, failed `1`, cancelled `0`; cleanup failures empty. Fresh mono
passed. Fresh Mode A failed before streaming at boundary `run row`, with
`state='teardown'; aborted=True; first_errno=-16`.

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

- Exact aggregate result: `/tmp/opencode/hil-runs/rh3-20260815-06/result.json`
- Exact child:
  `/tmp/opencode/hil-runs/rh3-20260815-06.children.54d1839d896c/rh3-20260815-06.p1.r1.rh3.fresh_mono_48_4_1.c90ec37a4bfc/`
- Exact child result:
  `/tmp/opencode/hil-runs/rh3-20260815-06.children.54d1839d896c/rh3-20260815-06.p1.r1.rh3.fresh_mono_48_4_1.c90ec37a4bfc/result.json`
- Failure detail: `invalid receiver status: I2S underruns=1; Stream resets=1; Push failures=1`

Retained receiver status recorded one `i2s_nrfx: Next buffers not supplied on
time`, maximum RX callback gap `268003 us`, and `8884` PLC frames. FLPR stayed
healthy with `submit=success=12650`, zero fallback, zero busy, and zero faults.
Source terminal status reached `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`,
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
failures empty. The first failed boundary was exactly
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

## Narrow conclusion

RH2 short-mono runner witness passed. RH3 runs `01` through `09` are immutable
evidence: `01` through `04`, `06`, `07`, `08`, and `09` failed, while `05` was
cancelled by the host timeout. Direct diagnostics
`rh3-20260821-03-modea-depth-fix`,
`rh3-20260822-01-modea-tail-order-cleanup`,
`rh3-20260822-02-modea-iso-parser-fix`,
`rh3-20260822-03-modea-critical-tail-snapshot`, and
`rh3-20260822-04-modea-lifecycle-split`, and
`rh3-20260822-09-modea-selected-layout`, and
`rh3-20260822-10-modeb-7p5-selected-layout` are immutable failed evidence. Direct
diagnostics `rh3-20260822-05-current-image-mono-control`,
`rh3-20260822-06-current-image-modeb-control`,
`rh3-20260822-07-modeb-selected-layout`, and
`rh3-20260822-08-mono-selected-layout` are immutable passed control evidence,
not acceptance. The selected-layout diagnostics captured the eight requested
fields. The new Mode A diagnostic retained two selected CIS records and failed
log scan on one receiver warning. The Mode B diagnostic retained materially
severe loss counters; the mono diagnostic retained low loss counters. Neither
infers cause.
Current source fixes are software-verified only. This does not claim RH3, RH4,
release, analog, full hardware acceptance, audibility, or repeatability.

## Next resume steps

1. RH2 witness is no longer blocked.
2. Direct diagnostics `rh3-20260822-03-modea-critical-tail-snapshot` and
   `rh3-20260822-04-modea-lifecycle-split` are complete. The former failed at
   receiver-tail status validation. The latter failed at log scan. Both retain
   ISO and FLPR snapshots but produce no acceptance result.
3. Direct diagnostics `rh3-20260822-05-current-image-mono-control`,
   `rh3-20260822-06-current-image-modeb-control`,
   `rh3-20260822-07-modeb-selected-layout`, and
   `rh3-20260822-08-mono-selected-layout` completed once with outcome
   `passed`. They are mono and Mode B control results, with selected-layout
   telemetry in `07` and `08`, not acceptance. Do not retry any of these
   diagnostics.
4. RH3-07, RH3-08, RH3-09, and direct diagnostics
   `rh3-20260821-03-modea-depth-fix`,
   `rh3-20260822-01-modea-tail-order-cleanup`, and
    `rh3-20260822-02-modea-iso-parser-fix`, and
    `rh3-20260822-03-modea-critical-tail-snapshot`, and
    `rh3-20260822-04-modea-lifecycle-split`, and
    `rh3-20260822-09-modea-selected-layout`, and
    `rh3-20260822-10-modeb-7p5-selected-layout` are immutable failed
    evidence, not acceptance. Do not retry any child or launch another matrix
    from this phase.
   Bullet 5 is historical execution control for the recorded diagnostics;
   the current standing policy supersedes it for future work.
 5. Matrix runner solely owns hardware. Do not use manual serial, flashing,
    reset, or FLPR work, and do not rebuild source during execution.
6. Preserve these evidence roots:
   - `/tmp/opencode/hil-runs/rh2-20260815-12/`
   - `/tmp/opencode/hil-runs/rh2-20260815-13/`
   - `/tmp/opencode/hil-runs/rh2-20260815-14/`
   - `/tmp/opencode/hil-runs/rh3-20260815-01.children.855cf5240f2e/`
   - `/tmp/opencode/hil-runs/rh3-20260815-02.children.f5b5588ed7f5/`
   - `/tmp/opencode/hil-runs/rh3-20260815-03.children.898a3f645e81/`
   - `/tmp/opencode/hil-runs/rh3-20260815-04/`
   - `/tmp/opencode/hil-runs/rh3-20260815-04.children.9f096d25eb41/`
    - `/tmp/opencode/hil-runs/rh3-20260815-05/`
    - `/tmp/opencode/hil-runs/rh3-20260815-05.children.cb825a82f91c/`
    - `/tmp/opencode/hil-runs/rh3-20260815-06/`
    - `/tmp/opencode/hil-runs/rh3-20260815-06.children.54d1839d896c/`
    - `/tmp/opencode/hil-runs/rh3-20260815-07/`
     - `/tmp/opencode/hil-runs/rh3-20260815-07.children.0e29ddf55210/`
      - `/tmp/opencode/hil-runs/rh3-20260815-08/`
      - `/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/`
        - `/tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup/`
        - `/tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix/`
        - `/tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot/`
        - `/tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split/`
         - `/tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control/`
         - `/tmp/opencode/hil-runs/rh3-20260822-05-current-image-mono-control.junit.xml`
         - `/tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control/`
         - `/tmp/opencode/hil-runs/rh3-20260822-06-current-image-modeb-control.junit.xml`
          - `/tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout/`
          - `/tmp/opencode/hil-runs/rh3-20260822-07-modeb-selected-layout.junit.xml`
           - `/tmp/opencode/hil-runs/rh3-20260822-08-mono-selected-layout/`
           - `/tmp/opencode/hil-runs/rh3-20260822-08-mono-selected-layout.junit.xml`
            - `/tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout/`
            - `/tmp/opencode/hil-runs/rh3-20260822-09-modea-selected-layout.junit.xml`
            - `/tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout/`
            - `/tmp/opencode/hil-runs/rh3-20260822-10-modeb-7p5-selected-layout.junit.xml`
7. Do not run the source build and `tests/hil` concurrently. Both touch
   `build/hil-source`; run them sequentially.
8. Never retry, delete, overwrite, or reuse IDs `01` through `09`, either
   current-image control, or any selected-layout diagnostic.
9. Do not edit `STATUS.md` or claim RH3, RH4, release, or analog acceptance.

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

## RH3 direct runner diagnostic `rh3-20260822-01-modea-tail-order-cleanup`

Immutable direct runner diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-01-modea-tail-order-cleanup.junit.xml`.
The retained `result.json` and JUnit outcome are `failed`; cleanup failures
were empty. No runner process exit code is retained. Evidence integrity passed
`23/23` SHA-256 entries.

The four reviewed image identities matched exactly:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Source reached `streaming`, `scored_complete`, `teardown`, terminal PASS, and
final `idle` in order. At the active streaming status, stream 0 was
`sub=24 sc=0 sf=0 cb=22 out=2`, and stream 1 was
`sub=23 sc=0 sf=0 cb=22 out=1`. Stop status reached both streams at
`sub=12644 sc=12000 sf=0 cb=12644 out=0`. The final idle status retained
`seq=12644` and reset the single idle stream counters to zero. No source
parse-error/unbound cleanup record occurred.

Receiver `bt iso quality` ran before the later disable/teardown logs. Raw
output contained the expected header and two active records, each with all
seven counters:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14323 retransmitted=0 crc_error=0 rx_unreceived=14317 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14250 retransmitted=0 crc_error=0 rx_unreceived=14241 duplicate=0
```

The runner nevertheless failed at boundary `receiver tail` with exact detail
`invalid ISO link quality: ISO link quality grammar malformed`. This records
the raw command order and values only; it does not claim the receiver-tail
criterion passed. Strict loss remained high in the retained counters.

Receiver logs showed `Stream[0] started: CIG 0 CIS 0` and
`Stream[1] started: CIG 0 CIS 1`. No slot-specific callback summary was
emitted. Aggregate `audio perf` recorded `iso_recv=28754`, `lc3_decode=28740`,
`sink_push=14369`, `Push failures=0`, `I2S write fail total=0`, RX callback gap
`10112 us` maximum, and I2S write gap `11769 us` maximum. `flpr offload`
recorded `submit=14370 success=14370 fallback=0` with zero faults and zero
recovery attempts. `flpr status` recorded Ready/ACKed/Healthy all `yes`, with
zero handshake errors, RX loss, duplicates, out-of-order, or missed packets.

Timestamped runtime warning was:

```text
[00:31:31.290,950] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
```

Flash logs also retained OpenOCD extra-erase-range warnings. No assertion,
source protocol error, or runner cleanup failure was recorded. Physical RH3
acceptance remains absent. Parser repair is host-only, and no physical retry
occurs in this phase. Do not retry this diagnostic.

## Physical RH3 run `rh3-20260815-08`

Observed execution on 2026-08-20 used the exact fixed matrix command from the
approved execution handoff. Preflight confirmed both output paths were absent,
fixture validation returned:

```text
warning: Git tree '/home/thomas-workstation/repos/le-audio-receiver' is dirty
le-audio-receiver shell (NCS v3.3.0, toolchain env scoped to west)
ZEPHYR_BASE: /home/thomas-workstation/ncs/v3.3.0/zephyr
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

The four required preflight image hashes matched exactly:

- source CPUAPP: `b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `4838edf32ef2754f02757756d70fabfe009ad4ae6a079a69081b8293657fb6a4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The runner command exited `1`. Aggregate result is `failed` with
`scheduled=20`, `attempted=3`, `passed=2`, `failed=1`, `cancelled=0`, and
empty cleanup failures. The exact aggregate first failed boundary is
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

Failed child result records first boundary `receiver tail` and exact failure
detail `invalid receiver status: I2S underruns=1; Stream resets=1; Push
failures=1; offload submit/success mismatch`. Its `images.json` records the
four required hashes listed above. Aggregate and all three attempted child
directories retain `result.json`, JUnit, `MANIFEST.md`, and `SHA256SUMS`.

Relevant retained receiver evidence is in
`/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/receiver-status.txt`:

```text
i2s_nrfx: Next buffers not supplied on time
i2s_nrfx: Cannot write in state: 4
audio_i2s: I2S underrun, restarting DMA
Frames decoded : 28354
PLC frames     : 28290 (99%)
I2S underruns  : 1
Stream resets  : 1
Push failures  : 1
Counters       : submit=14223 success=14222 fallback=0 busy=0
```

The same file records settle snapshots with `submit=14253 success=14252` and
`submit=14278 success=14277`. Source record evidence is retained at
`/tmp/opencode/hil-runs/rh3-20260815-08.children.131fa0f7335a/rh3-20260815-08.p1.r3.rh3.fresh_mode_b_48_4_1.d330fbe1d3e9/source-records.jsonl`;
it records `streaming`, `scored_complete`, terminal counters `sub=12320`,
`sc=12000`, `sf=0`, `cb=12320`, `out=0`, then teardown with cause `stop` and
terminal verdict `fail`.

External JUnit records `tests=20`, `failures=1`, `skipped=17`, and `errors=0`.
Review criteria are not met. Physical RH3 acceptance is absent. RH3-01 through
RH3-07 remain immutable historical evidence. Preserve this run and do not
retry a child or launch another matrix from this phase.

## Physical RH3 run `rh3-20260820-01`

Immutable failed evidence, not acceptance. The exact fixed RH3 schedule stopped
after the fresh mono and fresh Mode A rows. The aggregate result is:

```text
aggregate: /tmp/opencode/hil-runs/rh3-20260820-01/
children:  /tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/

outcome=failed
scheduled=20 attempted=2 passed=1 failed=1 cancelled=0
cleanup_failures=[]
first_failed_boundary=pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: receiver tail
```

The passed fresh-mono child was:

```text
/tmp/opencode/hil-runs/rh3-20260820-01.children.f271c7e01015/
  rh3-20260820-01.p1.r1.rh3.fresh_mono_48_4_1.82541559ac27/
```

The failed fresh Mode A child was:

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
[00:26:51.063,809] <wrn> audio_i2s: I2S slab full - dropping frame

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
  execution, but no acceptance result. A later lifecycle-split direct
  diagnostic is recorded below; it does not change this earlier run's
  non-acceptance status.

## Physical RH3 direct runner diagnostic `rh3-20260822-02-modea-iso-parser-fix`

Immutable direct Mode A diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-02-modea-iso-parser-fix.junit.xml`.
`result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first boundary
`receiver tail`, and exact detail `invalid receiver status: offload
state='STOPPED'`. No runner process exit code is retained in this evidence root;
do not infer one from later read-only commands. Cleanup failures were empty.
Evidence integrity passed `23/23` SHA-256 entries.

The sequential preflight passed: the new run paths were absent, fixture
validation returned `{"capture_capability": "none", "fixture_id":
"local-nrf54l15-receiver"}`, all four reviewed image hashes matched, and
`git diff --check` passed. The reviewed image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The receiver `bt iso quality` query appeared at `receiver-status.txt` lines
75-78, before stream disable. The repaired parser accepted the physical
transcript:
`header_seen=true`, `malformed=false`, exactly two records, and zero validation
errors. The exact parsed records were:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14302 retransmitted=0 crc_error=0 rx_unreceived=14293 duplicate=1
  Stream[1] handle=0x0002 tx_unacked=0 tx_flushed=0 tx_last_subevent=14381 retransmitted=0 crc_error=89 rx_unreceived=14387 duplicate=12
```

The parser failure did not recur. Later strict receiver-tail validation failed
on the offload state, not ISO link-quality grammar.

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal PASS, and final
`idle`. Final idle status retained `seq=12644` and reset the single idle stream
counters to `sub=0 sc=0 sf=0 cb=0 out=0`. The active status snapshot had stream
0 `seq=17 sub=17 sc=0 sf=0 cb=17 out=0` and stream 1
`seq=17 sub=17 sc=0 sf=0 cb=15 out=2`.

Receiver logs recorded `Stream[0] started: CIG 0 CIS 0` and
`Stream[1] started: CIG 0 CIS 1`. Final stream summaries were:

```text
Stream[0]: SDUs=130 decoded=28750 plc=28620 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=130 rx_error=0 rx_lost=14289 rx_unknown=0 rx_no_ts=50
Stream[1]: SDUs=0 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=0 rx_error=0 rx_lost=14389 rx_unknown=0 rx_no_ts=14389
```

The post-stop `audio status` snapshot reported zero decoded/PLC/error,
underrun, reset, and empty-SDU counters, with drift `INIT`, ppm `0`, and
resampler `ASRC linear`. `audio perf` retained `iso_recv=28764`,
`lc3_decode=28750`, `sink_push=14374`, zero push/I2S write failures, and
maximum RX callback and I2S write gaps of `10000 us` and `10635 us`.
`flpr offload` retained `STOPPED / epoch=0 gen=3`, submit/success
`14375/14375`, fallback/busy `0/0`, all listed faults and recovery counters
zero, and RTT average `1885 us`. `flpr status` reported Ready/ACKed/Healthy
all `yes`, handshake error counters zero, and RX lost/duplicate/out-of-order/
missed all zero.

Read-only strict scan facts: source warning hits `0`, source HIL1 protocol
errors `0`, receiver raw warning hits `0`, and receiver shell-error hits `0`.
No assertion line occurred in runtime console evidence. Flash evidence retained
the existing source OpenOCD extra-erase-range warnings and source probe-scan
deprecation messages; these are tool-log facts, not runtime receiver warnings.
High ISO loss remains unqualified evidence: `rx_unreceived=14293` and `14387`,
with `crc_error=89` on slot 1. This diagnostic is not RH3 acceptance, an audio
health claim, or a root-cause conclusion. Do not retry it.

## Physical RH3 direct runner diagnostic `rh3-20260822-03-modea-critical-tail-snapshot`

Immutable direct Mode A diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-03-modea-critical-tail-snapshot.junit.xml`.
The retained `environment.json` records runner command status `0`. Retained
`result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first boundary
`receiver tail`, exact detail `invalid receiver status: offload
state='STOPPED'`, and `cleanup_failures=[]`. Evidence integrity passed all
`23/23` SHA-256 entries.

The sequential preflight passed: both output paths were absent, fixture
validation returned `{"capture_capability": "none", "fixture_id":
"local-nrf54l15-receiver"}`, all four reviewed image hashes matched, and
`git diff --check` passed. The exact image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

The retained receiver command sequence began with live `bt iso quality`; the
later `flpr offload`, `audio status`, `audio perf`, and `flpr status` commands
were post-teardown diagnostics. The post-stop offload counters were already
equal, so the old bounded settle outcome was `equal` with `0` retries. The ISO
transcript was accepted with its exact header and two records:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14328 retransmitted=0 crc_error=2 rx_unreceived=14322 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14257 retransmitted=0 crc_error=0 rx_unreceived=14248 duplicate=1
```

The post-teardown offload snapshot retained `STOPPED / epoch=0 gen=3`,
`submit=14377 success=14377 fallback=0 busy=0`. Timeout, full, stale, seq,
frame, CRC, and payload faults were all `0`. Recovery was
`attempts=0 fail=0 relapses=0 exhaustion=0`; probation was
`active=0 success=0 cleared=0`. RTT was min/max/avg `1827/2072/1863 us`,
`n=14377`; FLPR cycles were min/max/avg `977/1019/986`, `n=14377`.
The ASRC offload fault counters, including state and verify, were all `0`.
The separate FLPR handshake snapshot was Ready/ACKed/Healthy `yes`, with
handshake errors and RX lost, duplicate, out-of-order, and missed counters all
`0`. Startup logs had recorded offload prep `epoch=1626819632 gen=2 state=ACTIVE`
before the post-stop snapshot reported `STOPPED / epoch=0 gen=3`.

Source records reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active status recorded both streams at `seq=25 sub=25 sc=0 sf=0 cb=24
out=1`, connected with security level `2`, two sink ASEs, and group `true`.
Stop status recorded both streams at `seq=12644 sub=12644 sc=12000 sf=0
cb=12644 out=0`. Final idle retained `seq=12644` and reset the single idle
stream counters to zero. No active audio capture ran because
`capture_capability=none`; no `summary.json` or receiver stream-summary
records were retained because strict receiver-tail validation stopped first.

The post-stop audio status snapshot reported frames decoded `0`, PLC `0`,
decode errors `0`, I2S underruns `0`, stream resets `0`, empty SDUs `0`, drift
`INIT`, ppm `0`, resampler `ASRC linear`, and volume `195/255`. Aggregate
performance recorded `iso_recv=28768`, `lc3_decode=28754`, `volume=14377`,
`sink_push=14376`, ASRC `0`; push failures, ASRC capacity failures, I2S write
failures, and DMA restarts were all `0`. Slab free was `0/3`, output frames
`476/477`, output blocks `14376`, maximum RX callback gap `10131 us`, maximum
I2S write gap `11143 us`, and maximum I2S write time `22 us`.

Runtime receiver warning evidence retained exactly:

```text
[00:27:06.510,949] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
```

No runtime assertion, shell error, source warning, or source HIL1 protocol error
was recorded. Source flash logs retained extra erase-range warnings at
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`; source probe scan
retained the existing `gdb_port`, `tcl_port`, and `telnet_port` deprecation
messages. These are tool-log facts, not causal findings. High ISO loss remains
unqualified evidence: `rx_unreceived=14322` and `14248`, with stream 0
`crc_error=2` and stream 1 `duplicate=1`. This diagnostic is not RH3
acceptance, an audio-health claim, or a root-cause conclusion. Do not retry it.

## Physical RH3 direct runner diagnostic `rh3-20260822-04-modea-lifecycle-split`

Immutable direct Mode A diagnostic, not a matrix run and not acceptance. Exact
evidence root:
`/tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split/`. JUnit:
`/tmp/opencode/hil-runs/rh3-20260822-04-modea-lifecycle-split.junit.xml`.
The retained `environment.json` records direct runner command status `0`.
Retained `result.json`, JUnit, and `MANIFEST.md` record outcome `failed`, first
boundary `log scan`, exact warning detail from the receiver, and
`cleanup_failures=[]`. `SHA256SUMS` verification passed all `26/26` entries.

The sequential preflight passed: both output paths were absent, `rh2_test.py`
reported `153` tests OK, `capture_runner_test.py` reported `19` tests OK,
`py_compile` passed, fixture validation returned
`{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`,
all four reviewed image hashes matched, and `git diff --check` passed. The
reviewed image identities were:

- source CPUAPP: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`;
- source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`;
- receiver CPUAPP: `6607e71c06c5b73db8b3eeaf2162e1088858c62acbccdb3cd0213badc62750d4`;
- receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.

Active FLPR evidence was `ACTIVE / epoch=422562923 gen=2`. The first snapshot
was `submit=111 success=110 fallback=0 busy=0`; the settle retry ended at
`submit=131 success=131`, with settle outcome `equal` and `retries=1`.
Faults, recovery, exhaustion, and probation counters were `0`. Structured
evidence retained `hb_dedup=-1`, `remote_epoch=-1`, and runtime fields
`runtime_fails=-1`, `runtime_last_ms=-1`, `runtime_restarts=-1`.

The receiver tail command order was active `flpr offload`, settle retry
`flpr offload`, live `bt iso quality`, then post-stop `audio status`,
`audio perf`, `flpr offload`, and `flpr status`. The live ISO header and exact
two records were:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14314 retransmitted=0 crc_error=0 rx_unreceived=14308 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14242 retransmitted=0 crc_error=0 rx_unreceived=14233 duplicate=0
```

Post-stop FLPR was `STOPPED / epoch=0 gen=3`, with `submit=14370
success=14370 fallback=0 busy=0`. Listed faults, recovery, exhaustion, and
probation counters remained `0`; structured runtime and heartbeat fields
remained `-1`. Audio status reported decoded, PLC, decode-error, I2S-underrun,
stream-reset, and empty-SDU counters all `0`, drift `INIT`, ppm `0`, and
resampler `ASRC linear`. Performance reported `iso_recv=28754`,
`lc3_decode=28740`, `volume=14370`, `sink_push=14369`, ASRC `0`, push/I2S/DMA
failures `0`, slab free `0/3`, output frames `476/477`, output blocks `14369`,
RX callback gap `10127 us` maximum, and I2S write gap `11769 us` maximum.
FLPR handshake was Ready/ACKed/Healthy `yes`, handshake errors and RX
lost/duplicate/out-of-order/missed counters `0`, with handshake epoch
`2560148019` (`ready=1 reboot=1`).

Source lifecycle reached `configured`, `connecting`, `secured`, `discovered`,
`qos`, `streaming`, `scored_complete`, `teardown`, terminal `PASS`, and final
`idle`. Active status had two connected Mode A streams at `seq=25 sub=25
sc=0 sf=0 cb=24 out=1`; terminal status had both at `seq=12644 sub=12644
sc=12000 sf=0 cb=12644 out=0`. Receiver stream summaries retained persistent
loss evidence:

```text
Stream[0]: SDUs=137 decoded=28740 plc=28468 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=137 rx_error=0 rx_lost=14310 rx_unknown=0 rx_no_ts=85
Stream[1]: SDUs=135 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=135 rx_error=0 rx_lost=14249 rx_unknown=0 rx_no_ts=8
```

The retained receiver warning was:

```text
[00:42:49.737,888] <wrn> bt_conn: conn 0x20005640 failed to establish. RF noise?
```

Source warning and protocol lists and shell-error list were empty. Source flash
logs retained extra erase-range warnings at `0x01023afc .. 0x01023fff` and
`0x0005741c .. 0x00057fff`; source probe evidence retained existing OpenOCD
port-name deprecation messages. These are tool-log facts only. Lifecycle
boundary records were retained through stopped diagnostics, but the overall
row did not validate as passing because first failed boundary was `log scan`.
No RH3, hardware, audio, release, product, or root-cause conclusion follows.
Do not retry this diagnostic.

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
follows.

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

## RH3-40 current stop point

H40 is documented in the [canonical result](system-hil-rh3-40-tx-notify-workqueue-result.md).
Preserve its immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/
```

H40 must not be retried. Do not adopt its trace configuration in production.
Any further hardware work requires a new reviewed plan.

## RH3-41 current stop point

H41 is documented in the [canonical result](system-hil-rh3-41-tx-notify-workqueue-untraced-result.md).
Preserve its immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-20260830-41-tx-notify-wq-untraced/
```

H41 must not be retried or adopted in production. Any further hardware work
requires a new reviewed plan that distinguishes receiver enable callback and
start behavior from source enabled-completion observation.

## RH3-42 current stop point

H42 is documented in the [canonical result](system-hil-rh3-42-bap-enable-trace-result.md).
Preserve its immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/
```

H42 must not be retried, and its untraced workqueue configuration or callback
marker must not be adopted in production. The single passed row proves only that
the receiver enabled callback returned from `bt_bap_stream_start()` with a zero
result in this execution; it does not prove workqueue causation, an H41 root
cause, audio health, or a repair. Any further hardware or production work
requires a new reviewed plan.

## RH3-ModeA1b offload-disabled rerun stop point

`rh3-modea1b-20260903-offload-disabled` is immutable runner-owned evidence,
not an acceptance run. It used the derived diagnostic CPUAPP hash
`4f2c5e5a37f59ed1faa562ec89e0a77bef6ced10ad6d661f2b93160e45900204` under
HEAD `eac880d516d4828ac2169f9589c3808dc1810d4c`, with FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Double pristine diagnostic builds were byte-identical. The runner accepted the
intended active offload-disabled zero plane, `ACTIVE` with
`submit=0 success=0`, then the source completed its terminal PASS. The runner
failed at `session end` because its summary scan retained neither required slot
after its cursor, despite raw console evidence containing both summary lines.

`result.json` has `summary={}` and no frozen transport-limits verdict. Raw
telemetry includes severe loss but is not a substitute for runner-accepted
summaries. Post-stop FLPR and handshake commands were not reached. The binary
FLPR-offload versus transport/controller classification is therefore
inconclusive; triage class is fixture/runner evidence-order failure. Integrity
passed `24/24` SHA-256 entries. Preserve the evidence root and external JUnit.
Do not rerun this ID, query the board manually, or claim that local normal-image
restoration changed flashed hardware.

The original `rh3-modea1-20260903-offload-disabled` remains immutable
fixture-defect baseline evidence. Any future physical row requires reviewed
repair and host proof for the receiver-summary collection ordering, then a new
run ID and execution-time CPUAPP identity derivation.
