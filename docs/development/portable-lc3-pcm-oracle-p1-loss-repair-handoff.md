# PB-031 P1 one-CIS-loss refill-lead repair handoff

Status: Approved focused repair handoff after diagnostic run
`/tmp/opencode/pb031-p1-loss-diagnostic-20260915`.

Parent handoff: `docs/development/portable-lc3-pcm-oracle-p1-handoff.md`.

Checkpoint: commit `15282df443e7`, local-only tag
`pb031-p1-diagnostic-baseline-20260915`.

## Goal

Make warning-clean `modea_one_cis_loss_10ms` produce exactly 18 post-start
right-channel concealments and retain its accepted receiver and transport pins.
Remove all temporary loss tracing after applying the measured one-completion
sender-refill compensation.

## Scope

Modify only:

- `tests/bsim/client/src/bsim_client_main.c`
- the temporary diagnostic hunks in:
  - `tests/bsim/client/src/bsim_tx.c`
  - `tests/bsim/client/src/bsim_tx.h`
  - `tests/bsim/src/bsim_observer.c`
  - `tests/bsim/src/bsim_observer.h`
  - `tests/bsim/src/bsim_test_main.c`
  - `src/bt_bap.c`
  - `src/audio_stream_session.c`
  - `tests/unit/audio_stream_session/src/fake_observer.c`
- `docs/development/portable-lc3-pcm-oracle-p1-handoff.md`, append factual
  repair result only after verification
- `docs/development/portable-lc3-pcm-oracle-p1-loss-diagnostic-handoff.md`,
  append diagnostic conclusion only
- PB-031 `Implementation Notes` only, after verification
- this handoff, append execution result only

## Non-scope

- No production behavior change relative to checkpoint `15282df443e7`.
- No receiver assembler, decoder, sink oracle, scenario schema, expected count,
  hash, transport, fixture, buffer-count, warning-policy, lifecycle, or send-limit
  change.
- No sleep-duration tuning, new synchronization API, TX scheduling rewrite,
  accepted-warning expansion, PCM threshold selection, P2/P3 work, hardware,
  HIL, workflow, release, push, PR, or tag action.
- Do not edit product-owned PB-031 title, status, priority, type, Description,
  acceptance criteria, or implementation-plan text.

## Grounding and decided repair

### Accepted public behavior

`tests/bsim/src/bsim_sink_oracle.h` requires exactly 18 post-boundary
single-channel losses. `tests/bsim/stage1-scenarios.json` pins:

```text
pushes1=100 trans1=8 splc1=16 plc1=34 total1=216
h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
```

An earlier fixture-driven run at
`/tmp/opencode/pb031-p1-bsim-executor-20260915` produced those exact values.
It still contained warning-producing sink Start behavior, so its values remain
behavioral reference, not warning-clean acceptance.

### Current warning-clean synchronization

Current client behavior after checkpoint:

1. wait for 50 successful submissions on both streams;
2. pause right and drain its completion count to its accepted-send snapshot;
3. snapshot left completion count;
4. wait 18 further left completions;
5. resume right.

This warning-clean model produces 19 peer losses.

### Measured refill lead

Diagnostic boundaries were:

```text
LOSS_BOUNDARY pause right_sends=51 right_done=48 left_sends=51 left_done=48
LOSS_BOUNDARY right_drained=51 left_done_start=49
LOSS_BOUNDARY resume left_done=67 target=67 right_done=51
```

After resume, no right submission occurred on the next free completion. Fixed
slot traversal in `bsim_tx.c` visits slot 0 before slot 1, and both streams share
the three-buffer `tx_pool`. Trace order was:

```text
LOSS_BOUNDARY resume left_done=67 target=67 right_done=51
LOSS_TX_DONE stream=0 completion=68
LOSS_TX_SUB slot=0 seq=73 count=74
LOSS_TX_DONE stream=0 completion=69
LOSS_TX_SUB slot=1 seq=51 count=52
```

Therefore one absent-right event occurs during sender refill after scenario
thread resumes right. Peer-loss target remains 18. Completion-only pause window
must be 17, followed by measured one-completion refill lead. This is event-count
accounting from captured controller/TX order, not wall-clock tuning.

### Exact code shape

In `tests/bsim/client/src/bsim_client_main.c`:

1. Rename misleading `MODEA_ONE_CIS_LOSS_SEND_COUNT` to
   `MODEA_ONE_CIS_LOSS_COUNT`, value `18U`.
2. Add client-local `MODEA_ONE_CIS_REFILL_LEAD_COMPLETIONS`, value `1U`.
3. Add a build assertion that loss count is greater than refill lead.
4. Keep right pause, right accepted-send snapshot, right completion drain, left
   completion snapshot, bounded completion wait, immediate right resume, and
   both final send limits of 110.
5. Set left completion target to:

```c
left_completion_count +
	(MODEA_ONE_CIS_LOSS_COUNT - MODEA_ONE_CIS_REFILL_LEAD_COMPLETIONS)
```

6. Update nearby scenario comment to state 17 pause-window completions plus one
   measured sender-refill lead produce 18 absent right-CIS peer events. Do not
   claim `.sent` proves on-air delivery; receiver count remains peer truth.
7. Retain concise non-diagnostic pause/resume information already present at
   checkpoint. Do not retain any `LOSS_*` records.

Remove every temporary diagnostic addition shown by current diff against
`15282df443e7` except client scenario repair above. Specifically:

- restore `.sent` callback to atomic increment only;
- remove `bsim_tx_trace_enabled`, `bsim_tx_trace_enable()`, trace-only include,
  trace fields, and `LOSS_TX_SUB` output;
- remove receiver loss-trace APIs, state, calls, scenario enablement, and fake
  observer seam;
- retain all non-diagnostic P1 and warning-clean checkpoint behavior.

Do not use whole-file restore on `bsim_client_main.c`, because it also owns this
repair. Whole-file restore to checkpoint is acceptable only for files whose
post-checkpoint diff is purely diagnostic, after confirming that fact.

## Validation

Run in order from repository root:

```bash
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
nix develop -c env \
  BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-refill-lead-fix-20260915 \
  bash scripts/bsim-stage1-run.sh
nix develop -c ./scripts/test-all.sh --phase unit
nix develop -c backlog doctor
git diff --check
```

Before matrix command, verify output root does not exist or is empty. Never
delete or overwrite prior evidence roots.

Matrix acceptance:

- 17 scenarios, 26 runs, all pass;
- both one-CIS-loss runs produce exactly accepted values listed above;
- both runs have identical receiver and TX hashes;
- every other receiver pin, lifecycle total, transport count/hash, and parser
  contract remains unchanged;
- no compiler warning;
- no client/receiver runtime warning or error except scenario 17 receiver exact
  `Invalid operation in state: releasing` allowlist;
- no `LOSS_TX_*`, `LOSS_BOUNDARY`, `LOSS_RX`, or `LOSS_EMIT` output remains.

## Documentation and commit

Only after all validation passes:

1. Append concise factual result to parent handoff, diagnostic handoff, this
   handoff, and PB-031 Implementation Notes. Preserve raw evidence path.
2. Inspect `git status`, `git diff`, and `git log --oneline -10`.
3. Stage only scoped files. Commit with message:

```text
test: stabilize one-CIS loss oracle
```

Do not push, create PR, create/push tag, amend, merge, or add attribution.

Return files changed, exact matrix summary, one-CIS values from both runs, unit
and parser totals, warning scan result, evidence path, commit hash/message,
deviations, and blockers.

## Escalation

If 17-completion pause does not produce exact accepted count and hashes in both
runs, or any other pin/warning changes, stop without commit. Preserve worktree
and logs. Do not try another count, sleep, repin, oracle relaxation, trace, or
scheduler change. Report exact results and one focused blocker to Delegator.

## Superseded result (2026-09-15)

Status: Failed and superseded by
`portable-lc3-pcm-oracle-p1-loss-placement-repair-handoff.md`.

The 17-completion refill-lead candidate reached the required concealment count
but placed the gap after 51 valid right frames, producing
`h1=0x134C23B0` and `rh1=0xAE966FB0` instead of accepted hashes. Its existing
diagnostic and comparison evidence remains retained. The exact-cap repair
validated cap 48 at `/tmp/opencode/pb031-p1-exact-gap-fix-20260915`, preserving
the accepted count, receiver hashes, and TX hashes in both strict loss runs.
