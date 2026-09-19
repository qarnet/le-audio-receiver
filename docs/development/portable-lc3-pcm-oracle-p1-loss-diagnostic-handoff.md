# PB-031 P1 one-CIS-loss diagnostic handoff

Status: Approved diagnostic-only handoff. It follows checkpoint commit
`15282df` and local-only tag `pb031-p1-diagnostic-baseline-20260915`.

Parent handoff: `docs/development/portable-lc3-pcm-oracle-p1-handoff.md`.

## Goal

Capture exact client submission/completion and receiver ISO callback/Mode A
emit boundaries around `modea_one_cis_loss_10ms`. Explain why a right-stream
pause synchronized to 18 left controller completions yields 19 receiver
concealments. Produce raw evidence for Delegator; do not choose or implement
production repair.

## Scope

Modify only:

- `tests/bsim/client/src/bsim_client_main.c`
- `tests/bsim/client/src/bsim_tx.c`
- `tests/bsim/client/src/bsim_tx.h`
- `tests/bsim/src/bsim_observer.c`
- `tests/bsim/src/bsim_observer.h`
- `tests/bsim/src/bsim_test_main.c`, only the scenario-select enable call
- `src/bt_bap.c`, only code inside `CONFIG_BSIM_OBSERVER`
- `src/audio_stream_session.c`, only code inside `CONFIG_BSIM_OBSERVER`
- `tests/unit/audio_stream_session/src/fake_observer.c`, only no-op
  `bsim_observer_modea_emit()` implementation needed to preserve observer seam
- this handoff, only for raw result appendix after evidence capture

All diagnostics are test-only and temporary. Production builds with
`CONFIG_BSIM_OBSERVER=n` must contain no new calls or behavior.

## Non-scope

- No receiver oracle, expected count, hash, scenario schema, parser, fixture,
  buffer count, timing constant, production behavior, or warning-policy change.
- Do not defer or relax fail-fast. The 19th concealment should still fail.
- No threshold, P2/P3, hardware, HIL, flash, release, workflow, backlog status,
  commit, push, PR, reset, stash, or clean action.
- Do not compare candidate repairs in this execution. Capture current tagged
  behavior only.

## Exact diagnostic shape

### Client trace gate and TX submissions

In `bsim_tx.h/.c`, add `void bsim_tx_trace_enable(bool enabled)`. Back it with
one `atomic_bool`, initially false. Do not change TX scheduling.

When enabled, after a successful send has committed count/hash/sequence under
`tx_lock`, emit one INFO/printk record after releasing the lock:

```text
LOSS_TX_SUB slot=<slot> seq=<submitted-seq> count=<committed-count>
```

Never log failed candidates as submissions. Do not move existing commit point,
hold `tx_lock` while printing, or alter retained audit.

In one-CIS-loss scenario, enable trace immediately after both 50-send waits and
before pausing right. Leave enabled through expected receiver failure.

### Client controller completions

Keep current per-stream atomic completion counters and `.sent` callback. While
TX trace is enabled, callback emits:

```text
LOSS_TX_DONE stream=<stream-index> completion=<new-count>
```

Unknown stream pointers still do nothing. Increment before printing. Do not
query TX state or take `tx_lock` from callback.

Retain current diagnostic boundary records, but make values complete:

```text
LOSS_BOUNDARY pause right_sends=<n> right_done=<n> left_sends=<n> left_done=<n>
LOSS_BOUNDARY right_drained=<n> left_done_start=<n>
LOSS_BOUNDARY resume left_done=<n> target=<n> right_done=<n>
```

Current behavior remains: pause right, drain right completions to right send
snapshot, snapshot left completion, wait 18 further left completions, resume.

### Receiver raw ISO callbacks

Add BSim observer enable API selected once from receiver `scenario_main`:

```c
void bsim_observer_loss_trace_enable(bool enabled);
```

Enable only for `BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS`, before Bluetooth setup.
Observer owns atomic enable flag and one per-slot callback ordinal.

Add observer API called from `bt_bap.c::stream_recv()` after `idx`, `valid`, and
`has_ts` are derived, before gate or receive-session handling:

```c
void bsim_observer_iso_recv(size_t slot, uint8_t flags, bool valid,
                            bool has_ts, uint32_t ts, uint16_t seq,
                            size_t len);
```

When enabled, increment slot ordinal and print every callback:

```text
LOSS_RX slot=<slot> ordinal=<n> flags=0x<hex> valid=<0|1> has_ts=<0|1> ts=<n> seq=<n> len=<n>
```

Flags remain raw `info->flags`; do not classify or mutate callback data.

### Receiver Mode A emits

Add observer API called only from existing `CONFIG_BSIM_OBSERVER` block in
`session_mode_a_store_and_process()`, immediately before
`bsim_observer_pre_push()`:

```c
void bsim_observer_modea_emit(uint32_t ts, uint16_t left_seq,
                              uint16_t right_seq, bool left_present,
                              bool right_present, bool left_source_valid,
                              bool right_source_valid);
```

When trace enabled, increment one emit ordinal and print:

```text
LOSS_EMIT ordinal=<n> ts=<n> lseq=<n> rseq=<n> lpresent=<0|1> rpresent=<0|1> lsrc=<0|1> rsrc=<0|1>
```

Use `modea_event.ts`, `seq[]`, `half_valid[]`, and `half_src[]` unchanged.
Do not add fields to production structs or sink PASS records.

## Verification and evidence

1. Run parser tests to prove no accepted contract changed:

```bash
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
```

2. Build and execute through normal matrix runner into a new external root:

```bash
nix develop -c env \
  BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-loss-diagnostic-20260915 \
  bash scripts/bsim-stage1-run.sh
```

Expected result: first 24 runs pass; one-CIS-loss run 1 fails on current 19th
concealment. Preserve logs.

3. Check diagnostic records exist and contain no malformed lines. Retain raw
receiver and client logs. Do not use their values to edit acceptance.

4. Run:

```bash
git diff --check
git status --short
git diff pb031-p1-diagnostic-baseline-20260915 -- \
  tests/bsim/client/src/bsim_client_main.c \
  tests/bsim/client/src/bsim_tx.c \
  tests/bsim/client/src/bsim_tx.h \
  tests/bsim/src/bsim_observer.c \
  tests/bsim/src/bsim_observer.h \
  src/bt_bap.c src/audio_stream_session.c
```

Return raw evidence paths; exact pause, drain, and resume boundary values;
ordered client SUB/DONE rows spanning five records before pause through five
after resume; ordered receiver RX and EMIT rows spanning first invalid right
half through failure; counts by slot/validity; command results; diff/status;
and blockers. Do not infer repair or commit.

## Escalation

Stop if instrumentation itself changes non-loss pins, produces build warnings,
cannot identify stream/slot unambiguously, or requires receiver fail relaxation.
Preserve worktree and evidence. Report exact conflict rather than inventing a
new trace or acceptance rule.

## Raw result appendix (2026-09-15)

Evidence root:

```text
/tmp/opencode/pb031-p1-loss-diagnostic-20260915
/tmp/opencode/pb031-p1-loss-diagnostic-20260915/modea_one_cis_loss_10ms-run1/client.log
/tmp/opencode/pb031-p1-loss-diagnostic-20260915/modea_one_cis_loss_10ms-run1/receiver.log
```

`nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py`
completed `94 PASS / 0 FAIL`.

`nix develop -c env BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-loss-diagnostic-20260915 bash scripts/bsim-stage1-run.sh`
compiled receiver and client without compiler warnings. It produced 24 passing
runs and one expected `modea_one_cis_loss_10ms` run-1 failure. The current
runner reached that loss run after 16 passes, continued its eight later
one-run scenarios, and reported no non-loss failure.

### Client boundaries

```text
LOSS_BOUNDARY pause right_sends=51 right_done=48 left_sends=51 left_done=48
LOSS_BOUNDARY right_drained=51 left_done_start=49
LOSS_BOUNDARY resume left_done=67 target=67 right_done=51
```

Only two TX records precede the pause record because the trace gate is enabled
immediately before that pause. Ordered TX records through the first five after
resume:

```text
LOSS_TX_DONE stream=0 completion=48
LOSS_TX_SUB slot=1 seq=50 count=51
LOSS_BOUNDARY pause right_sends=51 right_done=48 left_sends=51 left_done=48
LOSS_TX_DONE stream=0 completion=49
LOSS_TX_SUB slot=0 seq=51 count=52
LOSS_TX_DONE stream=1 completion=49
LOSS_TX_SUB slot=0 seq=52 count=53
LOSS_TX_DONE stream=1 completion=50
LOSS_TX_SUB slot=0 seq=53 count=54
LOSS_TX_DONE stream=1 completion=51
LOSS_TX_SUB slot=0 seq=54 count=55
LOSS_BOUNDARY right_drained=51 left_done_start=49
LOSS_TX_DONE stream=0 completion=50
LOSS_TX_SUB slot=0 seq=55 count=56
LOSS_TX_DONE stream=0 completion=51
LOSS_TX_SUB slot=0 seq=56 count=57
LOSS_TX_DONE stream=0 completion=52
LOSS_TX_SUB slot=0 seq=57 count=58
LOSS_TX_DONE stream=0 completion=53
LOSS_TX_SUB slot=0 seq=58 count=59
LOSS_TX_DONE stream=0 completion=54
LOSS_TX_SUB slot=0 seq=59 count=60
LOSS_TX_DONE stream=0 completion=55
LOSS_TX_SUB slot=0 seq=60 count=61
LOSS_TX_DONE stream=0 completion=56
LOSS_TX_SUB slot=0 seq=61 count=62
LOSS_TX_DONE stream=0 completion=57
LOSS_TX_SUB slot=0 seq=62 count=63
LOSS_TX_DONE stream=0 completion=58
LOSS_TX_SUB slot=0 seq=63 count=64
LOSS_TX_DONE stream=0 completion=59
LOSS_TX_SUB slot=0 seq=64 count=65
LOSS_TX_DONE stream=0 completion=60
LOSS_TX_SUB slot=0 seq=65 count=66
LOSS_TX_DONE stream=0 completion=61
LOSS_TX_SUB slot=0 seq=66 count=67
LOSS_TX_DONE stream=0 completion=62
LOSS_TX_SUB slot=0 seq=67 count=68
LOSS_TX_DONE stream=0 completion=63
LOSS_TX_SUB slot=0 seq=68 count=69
LOSS_TX_DONE stream=0 completion=64
LOSS_TX_SUB slot=0 seq=69 count=70
LOSS_TX_DONE stream=0 completion=65
LOSS_TX_SUB slot=0 seq=70 count=71
LOSS_TX_DONE stream=0 completion=66
LOSS_TX_SUB slot=0 seq=71 count=72
LOSS_TX_DONE stream=0 completion=67
LOSS_TX_SUB slot=0 seq=72 count=73
LOSS_BOUNDARY resume left_done=67 target=67 right_done=51
LOSS_TX_DONE stream=0 completion=68
LOSS_TX_SUB slot=0 seq=73 count=74
LOSS_TX_DONE stream=0 completion=69
LOSS_TX_SUB slot=1 seq=51 count=52
LOSS_TX_DONE stream=0 completion=70
```

### Receiver callbacks and Mode A emits

Ordered rows from first invalid right callback through receiver failure:

```text
LOSS_RX slot=1 ordinal=59 flags=0x0c valid=0 has_ts=1 ts=7707282 seq=58 len=0
LOSS_EMIT ordinal=60 ts=7707290 lseq=93 rseq=58 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=95 flags=0x09 valid=1 has_ts=1 ts=7717290 seq=94 len=120
LOSS_RX slot=1 ordinal=60 flags=0x0c valid=0 has_ts=1 ts=7717282 seq=59 len=0
LOSS_EMIT ordinal=61 ts=7717290 lseq=94 rseq=59 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=96 flags=0x09 valid=1 has_ts=1 ts=7727290 seq=95 len=120
LOSS_RX slot=1 ordinal=61 flags=0x0c valid=0 has_ts=1 ts=7727282 seq=60 len=0
LOSS_EMIT ordinal=62 ts=7727290 lseq=95 rseq=60 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=97 flags=0x09 valid=1 has_ts=1 ts=7737290 seq=96 len=120
LOSS_RX slot=1 ordinal=62 flags=0x0c valid=0 has_ts=1 ts=7737282 seq=61 len=0
LOSS_EMIT ordinal=63 ts=7737290 lseq=96 rseq=61 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=98 flags=0x09 valid=1 has_ts=1 ts=7747288 seq=97 len=120
LOSS_RX slot=1 ordinal=63 flags=0x0c valid=0 has_ts=1 ts=7747282 seq=62 len=0
LOSS_EMIT ordinal=64 ts=7747288 lseq=97 rseq=62 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=99 flags=0x09 valid=1 has_ts=1 ts=7757290 seq=98 len=120
LOSS_RX slot=1 ordinal=64 flags=0x0c valid=0 has_ts=1 ts=7757282 seq=63 len=0
LOSS_EMIT ordinal=65 ts=7757290 lseq=98 rseq=63 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=100 flags=0x09 valid=1 has_ts=1 ts=7767288 seq=99 len=120
LOSS_RX slot=1 ordinal=65 flags=0x0c valid=0 has_ts=1 ts=7767282 seq=64 len=0
LOSS_EMIT ordinal=66 ts=7767288 lseq=99 rseq=64 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=101 flags=0x09 valid=1 has_ts=1 ts=7777288 seq=100 len=120
LOSS_RX slot=1 ordinal=66 flags=0x0c valid=0 has_ts=1 ts=7777282 seq=65 len=0
LOSS_EMIT ordinal=67 ts=7777288 lseq=100 rseq=65 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=102 flags=0x09 valid=1 has_ts=1 ts=7787289 seq=101 len=120
LOSS_RX slot=1 ordinal=67 flags=0x0c valid=0 has_ts=1 ts=7787282 seq=66 len=0
LOSS_EMIT ordinal=68 ts=7787289 lseq=101 rseq=66 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=103 flags=0x09 valid=1 has_ts=1 ts=7797288 seq=102 len=120
LOSS_RX slot=1 ordinal=68 flags=0x0c valid=0 has_ts=1 ts=7797282 seq=67 len=0
LOSS_EMIT ordinal=69 ts=7797288 lseq=102 rseq=67 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=104 flags=0x09 valid=1 has_ts=1 ts=7807290 seq=103 len=120
LOSS_RX slot=1 ordinal=69 flags=0x0c valid=0 has_ts=1 ts=7807282 seq=68 len=0
LOSS_EMIT ordinal=70 ts=7807290 lseq=103 rseq=68 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=105 flags=0x09 valid=1 has_ts=1 ts=7817290 seq=104 len=120
LOSS_RX slot=1 ordinal=70 flags=0x0c valid=0 has_ts=1 ts=7817282 seq=69 len=0
LOSS_EMIT ordinal=71 ts=7817290 lseq=104 rseq=69 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=106 flags=0x09 valid=1 has_ts=1 ts=7827289 seq=105 len=120
LOSS_RX slot=1 ordinal=71 flags=0x0c valid=0 has_ts=1 ts=7827282 seq=70 len=0
LOSS_EMIT ordinal=72 ts=7827289 lseq=105 rseq=70 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=107 flags=0x09 valid=1 has_ts=1 ts=7837290 seq=106 len=120
LOSS_RX slot=1 ordinal=72 flags=0x0c valid=0 has_ts=1 ts=7837282 seq=71 len=0
LOSS_EMIT ordinal=73 ts=7837290 lseq=106 rseq=71 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=108 flags=0x09 valid=1 has_ts=1 ts=7847290 seq=107 len=120
LOSS_RX slot=1 ordinal=73 flags=0x0c valid=0 has_ts=1 ts=7847282 seq=72 len=0
LOSS_EMIT ordinal=74 ts=7847290 lseq=107 rseq=72 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=109 flags=0x09 valid=1 has_ts=1 ts=7857290 seq=108 len=120
LOSS_RX slot=1 ordinal=74 flags=0x0c valid=0 has_ts=1 ts=7857282 seq=73 len=0
LOSS_EMIT ordinal=75 ts=7857290 lseq=108 rseq=73 lpresent=1 rpresent=0 lsrc=1 rsrc=0
LOSS_RX slot=0 ordinal=110 flags=0x09 valid=1 has_ts=1 ts=7867290 seq=109 len=120
ERROR: (CMAKE_SOURCE_DIR/src/audio_sink_stub.c:338): le_audio_receiver: concealed pushes 19 > 18 — unexpected losses
The TESTCASE FAILED (test return code 2)
```

### Trace validation and callback counts

All diagnostic records matched their required format: 35 `LOSS_TX_SUB`, 35
`LOSS_TX_DONE`, one each of the three boundary records, 184 `LOSS_RX`, and 75
`LOSS_EMIT`; malformed diagnostic records: 0.

All `LOSS_RX` records, grouped by slot and `valid` value:

```text
slot=0 valid=0 count=42
slot=0 valid=1 count=68
slot=1 valid=0 count=23
slot=1 valid=1 count=51
```

## Resolution result (2026-09-15)

The diagnostic boundary evidence and subsequent placement model were resolved by
an exact right-stream send cap of 48, rather than polling placement. Final
source contains no `LOSS_*` diagnostics. The warning-clean strict matrix at
`/tmp/opencode/pb031-p1-exact-gap-fix-20260915` passed all 17 scenarios and 26
runs; both loss runs retained count 18 and receiver hashes
`0x30D6BAF0`/`0x32777D65`/`0x9859F1D8` with both TX hashes unchanged.
