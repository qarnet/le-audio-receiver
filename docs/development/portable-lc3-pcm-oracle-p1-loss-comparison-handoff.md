# PB-031 P1 one-CIS-loss placement comparison handoff

Status: Approved diagnostic-only handoff after the refill-lead repair failed
with the correct concealment count but changed right/full PCM hashes.

Parent handoff: `docs/development/portable-lc3-pcm-oracle-p1-handoff.md`.

Checkpoint: commit `15282df443e7`, local-only tag
`pb031-p1-diagnostic-baseline-20260915`.

## Goal

Compare event placement between:

1. the previously accepted fixed-200-ms/old-Start reference behavior; and
2. the warning-clean 17-completion candidate currently in the worktree.

Capture one compact client boundary record and one compact receiver Mode A
record per run. Identify the exact right-CIS loss event range and the right LC3
fixture frame before and after that range. Return raw evidence for Delegator.
Do not choose or implement another repair.

## Starting state

Expected starting diff:

- `tests/bsim/client/src/bsim_client_main.c` contains only the failed
  17-completion refill-lead candidate from
  `docs/development/portable-lc3-pcm-oracle-p1-loss-repair-handoff.md`.
- Both earlier loss handoff documents are untracked.
- No `LOSS_*` instrumentation remains in source.

Before editing, record `git status --short` and the exact client diff in the
external evidence root. The diagnostic must finish with this same source diff,
plus this comparison handoff and its factual result appendix. No diagnostic
code may remain.

## Scope

Temporary diagnostic edits only:

- `tests/bsim/client/src/bsim_client_main.c`
- `tests/bsim/src/bsim_observer.c`
- `tests/bsim/src/bsim_observer.h`
- `tests/bsim/src/bsim_test_main.c`
- `src/audio_stream_session.c`, only inside `CONFIG_BSIM_OBSERVER`
- `scripts/bsim-stage1-run.sh`, only a temporary exact-scenario filter
- this handoff, only a factual raw-result appendix after both runs

Final worktree must remove every temporary edit and restore the failed
17-completion client candidate byte-for-byte.

## Non-scope

- No receiver oracle, expected count, PCM hash, transport pin, fixture,
  scenario JSON, parser, send limit, TX scheduler, shared pool, controller
  buffer count, warning policy, or production behavior change.
- No per-callback, per-completion, per-submission, or per-emit printing.
- No sleep tuning or candidate repair experiment.
- No P2/P3, hardware, HIL, flash, backlog lifecycle, workflow, release, commit,
  push, PR, tag, reset, stash, clean, destructive, or installer action.
- Do not edit product-owned PB-031 fields or claim P1 acceptance.

## Compact receiver observer

The existing `bsim_observer_pre_push()` call proves source validity but lacks
Mode A sequence and payload identity. Add a temporary test-only observer API:

```c
void bsim_observer_loss_summary_enable(bool enabled);
void bsim_observer_modea_loss_event(uint32_t ts, uint16_t left_seq,
                                    uint16_t right_seq,
                                    bool left_source_valid,
                                    bool right_source_valid,
                                    const uint8_t *right_data,
                                    size_t right_len);
void bsim_observer_loss_summary_dump(void);
```

Add `<stddef.h>` to the observer header for `size_t`.

In `scenario_main()`, call `bsim_observer_loss_summary_enable()` once before
Bluetooth setup. Enable only when
`scn == BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS`.

In `session_mode_a_store_and_process()`, inside the existing
`CONFIG_BSIM_OBSERVER` block and immediately before
`bsim_observer_pre_push()`, pass unchanged values from `s.modea_ev`:

- `ts`
- left and right `seq[]`
- left and right `half_src[]`
- right `data[]`
- right `len[]`

Observer behavior when enabled:

1. Increment one Mode A emit ordinal for every call.
2. Ignore startup one-half events until one event has both source halves valid.
3. While both source halves are valid and no loss is active, retain this event
   as the last full event: ordinal, timestamp, both sequences, and first eight
   bytes of right payload.
4. On each subsequent event with left source valid and right source invalid:
   record first event on transition, update last event, and increment loss
   count. Preserve the retained pre-loss full event.
5. On first later event with both source halves valid, record recovery event
   once: ordinal, timestamp, both sequences, and first eight bytes of right
   payload. Close the active loss range.
6. Count any other post-boundary source-validity shape separately as
   `other_invalid`; do not reinterpret it as expected loss.
7. Keep state private to `bsim_observer.c`. Plain private fields are acceptable
   because Mode A emits are serialized by the receive path and one receiver
   process runs one scenario. Do not add production/session fields.
8. Copy payload bytes only when right source is valid and `right_len >= 8`.
   Otherwise retain an explicit `prefix_valid=0` marker.

Call `bsim_observer_loss_summary_dump()` at the start of `receiver_pass()`,
before the existing PASS record. It emits exactly one line:

```text
LOSS_SUMMARY count=<n> other=<n> pre_ord=<n> pre_ts=<n> pre_lseq=<n> pre_rseq=<n> pre_r8=<16-uppercase-hex|NONE> first_ord=<n> first_ts=<n> first_lseq=<n> first_rseq=<n> last_ord=<n> last_ts=<n> last_lseq=<n> last_rseq=<n> recovery_ord=<n> recovery_ts=<n> recovery_lseq=<n> recovery_rseq=<n> recovery_r8=<16-uppercase-hex|NONE>
```

No observer output occurs when disabled. Do not append these fields to the
strict PASS record.

The first eight bytes uniquely identify all 128 frames in each current LC3
fixture. This was verified before handoff for both
`bsim_48k_10ms_120b_l.lc3` and `bsim_48k_10ms_120b_r.lc3`. After each run, map
`pre_r8` and `recovery_r8` to zero-based frame indexes with a read-only Python
script over `tests/fixtures/lc3/bsim_48k_10ms_120b_r.lc3`, using 120-byte frame
stride. Require exactly one match for each valid prefix.

## Compact client boundary record

In `test_main_normal_modea_one_cis_loss()`, add function-local fields for:

- left/right accepted-send counts at pause;
- left/right controller-completion counts at pause;
- left/right accepted-send counts immediately before resume;
- left/right controller-completion counts immediately before resume.

Take pause snapshots immediately after `bsim_tx_pause(&streams[1])`. Take
resume snapshots immediately before resuming right, but call
`bsim_tx_resume(&streams[1])` before printing anything. Print no boundary
record in the live window. After both streams reach their final 110-send limits
and the existing teardown margin expires, print exactly one line before client
PASS:

```text
LOSS_CLIENT pause_ls=<n> pause_ld=<n> pause_rs=<n> pause_rd=<n> resume_ls=<n> resume_ld=<n> resume_rs=<n> resume_rd=<n>
```

Retain existing ordinary pause/resume messages in each variant. Do not add TX
or completion callback tracing.

## Temporary exact-scenario runner filter

In `scripts/bsim-stage1-run.sh`, after `MATRIX` loads from the versioned JSON,
accept temporary environment variable `BSIM_ONLY_SCENARIO`. When nonempty:

1. require exactly one matrix entry whose name equals the value;
2. fail clearly otherwise; and
3. replace `MATRIX` with that one scenario and exactly one run.

Do not change JSON, parser, normal no-filter behavior, known pins, or warning
scan. Remove this filter before finishing.

## Variant A: accepted historical behavior

Temporarily replace only these two client behaviors:

1. Restore `start_streams()` from commit `012b197` exactly: call
   `bt_bap_stream_start()` in requested order; accept `-EALREADY`, `-EINVAL`,
   and `-EBADMSG` as already streaming; otherwise wait for
   `sem_stream_started` after success.
2. Restore one-CIS-loss timing from commit `012b197` exactly: after both
   50-send waits, pause right, sleep `K_MSEC(200)`, then resume right. Do not
   drain completions or wait on a completion target in this variant.

Keep current deterministic identity, controller-buffer count, fixed fixture,
send limits, transport audit, compact summary instrumentation, and all other P1
changes.

Run:

```bash
nix develop -c env \
  BSIM_BASELINE=1 \
  BSIM_ONLY_SCENARIO=modea_one_cis_loss_10ms \
  BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-loss-placement-reference-20260915 \
  bash scripts/bsim-stage1-run.sh
```

Expected receiver/client processes: PASS with accepted count and hashes. Strict
runner may return nonzero only because old Start calls emit known
warning-producing client messages. Preserve raw result and warning text. Stop
if process fails, count/hash differs from accepted historical values, compact
summary is absent/malformed, or any new compiler/runtime fault appears.

## Variant B: warning-clean 17-completion candidate

Restore the exact client worktree state from handoff start, retaining only
temporary compact summary instrumentation and runner filter. This means:

- warning-clean endpoint-state observation in `start_streams()`;
- right completion drain;
- left completion target of
  `MODEA_ONE_CIS_LOSS_COUNT - MODEA_ONE_CIS_REFILL_LEAD_COMPLETIONS`;
- immediate right resume;
- failed candidate constants 18 and 1.

Run:

```bash
nix develop -c env \
  BSIM_BASELINE=1 \
  BSIM_ONLY_SCENARIO=modea_one_cis_loss_10ms \
  BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-loss-placement-candidate-20260915 \
  bash scripts/bsim-stage1-run.sh
```

Expected process and baseline parser result: PASS, exactly 18 concealments,
warning-clean runtime, retained left/transport values, and currently observed
candidate hashes `h1=0x134C23B0`, `rh1=0xAE966FB0`. Stop on any different
behavior; do not tune or rerun with another count.

## Result extraction

For both variants, return:

- exact receiver PASS and client PASS records;
- exact `LOSS_SUMMARY` and `LOSS_CLIENT` records;
- mapped zero-based right fixture frame indexes for `pre_r8` and
  `recovery_r8`;
- loss count, first/last emit ordinal, sequence range, timestamp range, and
  recovery event;
- process exits, strict parser result, compiler warnings, and runtime
  warnings/errors;
- raw evidence paths.

Then provide a field-by-field A/B table. State only observed differences. Do
not infer or implement repair.

## Cleanup and verification

After evidence extraction:

1. Remove all compact observer/client diagnostic code.
2. Remove `BSIM_ONLY_SCENARIO` support.
3. Restore `tests/bsim/client/src/bsim_client_main.c` to exact starting failed
   17-completion candidate diff.
4. Append factual results to this handoff.
5. Run:

```bash
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
git diff --check
git status --short
git diff -- tests/bsim/client/src/bsim_client_main.c
git diff -- scripts/bsim-stage1-run.sh \
  tests/bsim/src/bsim_observer.c \
  tests/bsim/src/bsim_observer.h \
  tests/bsim/src/bsim_test_main.c \
  src/audio_stream_session.c
```

Acceptance for diagnostic completion:

- parser tests remain `94 PASS / 0 FAIL`;
- no temporary diagnostic diff remains in code or runner;
- client diff matches handoff starting state exactly;
- only this handoff gains raw result text;
- no commit exists.

Return commands, results, final status/diff, deviations, and blockers.

## Escalation

Stop and preserve evidence if compact instrumentation changes accepted
reference hashes/count, either prefix has zero or multiple fixture matches,
one variant does not reach PASS, or temporary code cannot be removed without
touching non-diagnostic work. Report exact conflict. Do not invent another
trace, acceptance relaxation, or repair.

## Attempt 1 result and approved refinement (2026-09-15)

Attempt 1 ran Variant A exactly as specified at:

```text
/tmp/opencode/pb031-p1-loss-placement-reference-20260915
```

Build completed without compiler warnings. Processes exited receiver `2`,
client `1`, PHY `0`; strict parsing was not reached. Receiver failed at the
19th concealment:

```text
ERROR: (CMAKE_SOURCE_DIR/src/audio_sink_stub.c:338): le_audio_receiver: concealed pushes 19 > 18 — unexpected losses
```

Client retained the historical Start errors and old handling:

```text
<err> bt_bap_stream: Invalid state: streaming
CLI stream 0 already streaming (start -74)
CLI stream 1 already streaming (start -22)
```

Receiver/client PASS, `LOSS_SUMMARY`, and `LOSS_CLIENT` were absent because the
mandatory fail-fast fired before end-of-run dumping. Variant B was not run.
Executor removed every temporary diagnostic and runner edit and restored the
starting failed 17-completion candidate byte-for-byte. Parser tests remained
`94 PASS / 0 FAIL`; `git diff --check` passed; no commit was created.

Comparison conclusion so far: fixed 200 ms is phase-sensitive and no longer a
reproducible 18-event reference. It cannot be used as a repair.

### Refined execution

Resume the same diagnostic with these changes only:

1. Run original Variant B first, unchanged, into new root:

   ```bash
   nix develop -c env \
     BSIM_BASELINE=1 \
     BSIM_ONLY_SCENARIO=modea_one_cis_loss_10ms \
     BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-loss-placement-candidate-summary-20260915 \
     bash scripts/bsim-stage1-run.sh
   ```

2. If Variant B matches its expected count/hashes and emits both compact
   records, run one diagnostic-only reference-placement probe. Use exact
   historical Start behavior, but use `K_MSEC(190)` instead of 200. This is one
   CIS interval shorter because Attempt 1 produced 19 rather than 18 losses.
   It is a measurement probe only, never an allowed repair.
3. Run the 190 ms probe once into new root:

   ```bash
   nix develop -c env \
     BSIM_BASELINE=1 \
     BSIM_ONLY_SCENARIO=modea_one_cis_loss_10ms \
     BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-loss-placement-reference-190ms-20260915 \
     bash scripts/bsim-stage1-run.sh
   ```

4. Accept that probe for comparison only if it produces exactly 18
   concealments and exact historical receiver values:

   ```text
   pushes1=100 trans1=8 splc1=16 plc1=34 total1=216
   h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
   ```

5. Do not try 180 ms, another duration, another completion count, or a rerun if
   190 ms misses. Stop and return evidence.
6. If both records succeed, perform the original prefix mapping and A/B table,
   then original cleanup and verification. Append factual results below this
   refinement. Do not infer or implement repair.

## Attempt 2 result and approved record split (2026-09-15)

Variant B ran at:

```text
/tmp/opencode/pb031-p1-loss-placement-candidate-summary-20260915
```

All three processes and baseline strict parsing passed. Exact compact client
record:

```text
LOSS_CLIENT pause_ls=51 pause_ld=48 pause_rs=51 pause_rd=48 resume_ls=72 resume_ld=66 resume_rs=51 resume_rd=51
```

The receiver record contained every required field, but BSim console transport
split the long physical line between `re` and `covery_lseq`. Concatenating only
that transport split gives:

```text
LOSS_SUMMARY count=18 other=0 pre_ord=59 pre_ts=7697282 pre_lseq=92 pre_rseq=57 pre_r8=B1924E697DF350CF first_ord=60 first_ts=7707282 first_lseq=93 first_rseq=58 last_ord=77 last_ts=7877284 last_lseq=110 last_rseq=75 recovery_ord=78 recovery_ts=7887284 recovery_lseq=111 recovery_rseq=76 recovery_r8=7538E43181B32E80
```

Right fixture prefix mapping was unique: pre-loss frame index 50, recovery
frame index 51. Receiver values were the expected candidate values:

```text
pushes1=100 trans1=8 splc1=16 plc1=34 total1=216
h1=0x134C23B0 lh1=0x32777D65 rh1=0xAE966FB0
txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
```

This is complete usable Variant B evidence. Do not rerun it.

For the single remaining 190 ms reference-placement probe, replace the one long
receiver dump with exactly three short lines carrying the same state:

```text
LOSS_PRE count=<n> other=<n> ord=<n> ts=<n> lseq=<n> rseq=<n> r8=<16-uppercase-hex|NONE>
LOSS_GAP first_ord=<n> first_ts=<n> first_lseq=<n> first_rseq=<n> last_ord=<n> last_ts=<n> last_lseq=<n> last_rseq=<n>
LOSS_RECOVERY ord=<n> ts=<n> lseq=<n> rseq=<n> r8=<16-uppercase-hex|NONE>
```

Each line must remain below the observed console split limit. This output-shape
change is approved; all observer state and semantics remain unchanged.

Resume with only the already-approved 190 ms old-Start probe. Do not rerun
Variant B. If the probe reaches process PASS and emits all three short records,
map prefixes and complete the A/B table even if strict runner exit remains
nonzero solely because historical Start errors violate warning policy. Then
perform original cleanup and verification. If count/hash differs, a record is
missing, or another failure appears, stop without another probe.

## Attempt 3 result and placement model (2026-09-15)

The single 190 ms probe ran at:

```text
/tmp/opencode/pb031-p1-loss-placement-reference-190ms-20260915
```

It also failed at 19 concealments before compact records or PASS. Processes
exited receiver `2`, client `1`, PHY `0`; strict parsing was not reached. No
compiler warning appeared. Historical Start errors remained. Executor removed
all temporary code and restored the starting candidate diff byte-for-byte.
Parser tests remained `94 PASS / 0 FAIL`; `git diff --check` passed; no commit
was created.

Because both wall-clock probes were phase-sensitive, no further duration probe
is approved. Exact accepted placement was instead resolved with a read-only
host model under `/tmp/opencode/pb031-hash-model/`. The model used:

- installed liblc3 v1.1.2 source;
- `-m32 -O3 -ffast-math -fshort-enums -DLC3_PLUS=0 -DLC3_PLUS_HR=0`, matching
  the i386 BSim target and LC3 optimization/configuration;
- eight startup PLC events, matching `trans1=8` and `splc1=16`;
- volume 195 scaling from `audio_volume.c`;
- exact sink FNV framing: four-byte little-endian push index followed by
  little-endian signed 16-bit PCM samples;
- 100 post-boundary pushes and an 18-push right-channel PLC gap.

The model reproduced both observed full/right hash pairs exactly:

```text
valid right pushes before gap=48 full=0x30D6BAF0 left=0x32777D65 right=0x9859F1D8
valid right pushes before gap=51 full=0x134C23B0 left=0x32777D65 right=0xAE966FB0
```

Therefore accepted oracle requires exactly 48 valid right pushes before the
18-event gap. Variant B produced 51: its compact trace had pre-loss fixture
frame 50 and first loss at emit ordinal 60. Accepted placement is three right
frames earlier: pre-loss fixture frame 47, first loss at post-boundary push
index 48. This resolves the hash difference without changing fixture, decoder,
oracle, or expected pins.

## Resolution result (2026-09-15)

The modeled 48-valid-right-frame placement was implemented with an exact
right-stream send cap and process-lifetime per-slot condition-variable
notification. Strict evidence at
`/tmp/opencode/pb031-p1-exact-gap-fix-20260915` passed all 17 scenarios and 26
runs. Both one-CIS-loss runs retained count 18, full/left/right hashes
`0x30D6BAF0`/`0x32777D65`/`0x9859F1D8`, and TX hashes
`0x8980C79D`/`0xDD25CC21`; no `LOSS_*` output remained.
