# FR4 mono cadence review fix handoff

Date: 2026-08-10

## Goal

Close review findings after commit `60e2ac1` added timestamp-cadence omission
concealment. Canonical gate reached 64 PASS / 1 FAIL / 65 TOTAL; only test
matrix validation failed because four new public cadence APIs lack outcome
ledger entries.

Add exact matrix evidence, correct two stale source comments that still claim
controller-side omission advances HCI packet sequence, keep sequence diagnostic
counts distinct from merged cadence counts, and add direct session proof that a
delivered LOST callback carrying a timestamp consumes its own cadence position.

No architecture, production behavior, acceptance criterion, or release state
change beyond truthful diagnostics/comments and one missing direct test.

## Grounding

- `tests/test-matrix.json` classifies `src/audio_iso_seq.c` as `direct` and
  records outcomes for existing `audio_iso_seq_*` APIs. Rule 12 of
  `scripts/check-test-matrix.py` requires every public API in that source to
  have a `public_outcomes` record.
- Gate failure names exactly:
  - `audio_iso_cadence_reset`
  - `audio_iso_cadence_update`
  - `audio_iso_cadence_get_concealed`
  - `audio_iso_cadence_get_resyncs`
- Installed NCS v3.3.0 SW Split source proves controller-side no-PDU events
  emit no HCI SDU and consume no HCI packet sequence number. Timestamp cadence,
  not sequence jump, detects those omissions.
- `src/audio_stream_session.c` currently merges cadence into `omitted`, then an
  INFO diagnostic for a simultaneous sequence gap prints merged `omitted`
  rather than sequence-derived count. If cadence count is larger, log falsely
  labels timestamp-only positions as sequence omissions.
- NCS SW Split emits a timestamp on START/SINGLE HCI ISO packets, including
  emitted LOST SDUs. Public API still allows missing TS, already covered by
  tests.

## Exact changes

### Test matrix

In existing `src/audio_iso_seq.c` entry of `tests/test-matrix.json`, add nine
`public_outcomes` records:

1. `audio_iso_cadence_reset` -> `void`, witness
   `test_cadence_null_reset_first`;
2. `audio_iso_cadence_update` -> each of
   `AUDIO_ISO_CADENCE_RES_FIRST`, `AUDIO_ISO_CADENCE_RES_NO_TS`,
   `AUDIO_ISO_CADENCE_RES_CONTIG`, `AUDIO_ISO_CADENCE_RES_GAP`,
   `AUDIO_ISO_CADENCE_RES_WRAP`, `AUDIO_ISO_CADENCE_RES_RESYNC`, using narrow
   existing cadence witnesses;
3. `audio_iso_cadence_get_concealed` -> `UINT32`, witness
   `test_cadence_counters_and_reset`;
4. `audio_iso_cadence_get_resyncs` -> `UINT32`, same witness.

Use these update witnesses unless matrix schema/test naming requires a more
specific existing test:

- FIRST: `test_cadence_null_reset_first`
- NO_TS: `test_cadence_missing_ts_no_false_omission`
- CONTIG: `test_cadence_contiguous_10ms_and_75ms`
- GAP: `test_cadence_timestamp_only_omissions`
- WRAP: `test_cadence_backward_wrap_rebase`
- RESYNC: `test_cadence_resync_classes`

Also add truthful cadence `state_transitions` to same stateful source entry:

- `cadence-uninitialized->first-timestamp`
- `cadence-timestamped->no-ts-position-counted`
- `cadence-timestamped->contiguous`
- `cadence-timestamped->gap-concealed`
- `cadence-timestamped->wrap-rebase`
- `cadence-timestamped->resync`
- `cadence-any->reset`

Use corresponding witnesses above; reset uses
`test_cadence_counters_and_reset`. Do not alter existing sequence outcomes or
transitions.

### Correct stale comments

Update current comments in:

- `src/audio_iso_seq.h` opening module description;
- `src/audio_iso_seq.c` opening module description;
- `src/audio_stream_session.c` slot tracker comment and receive-path sequence
  block;
- `src/audio_stream_session.h` related current contract text if it still
  implies every controller omission appears as a sequence jump.

Truth to state:

- module contains independent HCI packet-sequence and timestamp-cadence
  trackers;
- sequence gaps detect emitted HCI SDUs omitted after controller sequencing,
  such as controller-to-host/host-side loss;
- controller-side radio events with no emitted HCI SDU keep sequence numbers
  contiguous and require timestamp cadence;
- Mode A retains sequence-only synthetic sentinels; mono/Mode B merge evidence
  with MAX.

Remove contradictory wording like "controller omits callback, next
Packet_Sequence_Number jumps". Preserve exact hardware evidence and bounded
semantics.

### Keep diagnostic evidence separate

In `session_recv_path()` retain separate variables:

```c
uint32_t seq_omitted = 0U;
uint32_t omitted;
```

Feed `seq_omitted` to `audio_iso_seq_update()`, initialize merged `omitted` from
it, then merge cadence with MAX as now. Sequence-gap INFO must print
`seq_omitted`, never merged `omitted`. Mode A sentinel loop remains based on
sequence-only count and `first_seq`; one-CIS PLC loop uses merged count.

No new INFO-per-cadence-gap logging. Preserve cadence RESYNC warning and WRAP
silence.

### LOST callback test

In `tests/unit/audio_stream_session/src/test_audio_stream_session.c`, add one
public-boundary test:

1. configure/open mono;
2. deliver valid SDU at timestamp 10000, sequence 1;
3. deliver LOST SDU (`valid=false`) with `has_ts=true`, timestamp 20000,
   sequence 2, NULL/zero payload;
4. deliver valid SDU at timestamp 30000, sequence 3;
5. prove exactly three sink pushes total, exactly one PLC frame from delivered
   LOST SDU, two good decoded frames, zero decode errors, and no extra cadence
   PLC before third callback.

Update current test count in `docs/testing/coverage-matrix.md` from 45 to 46.
Do not alter golden PCM, existing tests, stats API, or observer contract.

## Scope

Touch exactly:

- `docs/development/firmware-release-fr4-mono-cadence-review-fix-handoff.md`
- `tests/test-matrix.json`
- `src/audio_iso_seq.h`
- `src/audio_iso_seq.c`
- `src/audio_stream_session.c`
- `src/audio_stream_session.h` only if stale wording exists
- `tests/unit/audio_stream_session/src/test_audio_stream_session.c`
- `docs/testing/coverage-matrix.md`

Do not touch Kconfig, central scripts, QoS, I2S, timing/drift, Mode A behavior,
coverage baseline, VERSION, workflows, release/tag state, hardware, FR4
procedure/results, or retained `/tmp` evidence.

## Verification and commit

Run:

```bash
NIX_HARDENING_ENABLE="" west twister -T tests/unit/iso_seq \
  -p native_sim/native/64 --inline-logs
NIX_HARDENING_ENABLE="" west twister -T tests/unit/audio_stream_session \
  -p native_sim/native/64 --inline-logs
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

`check-test-matrix.py` without coverage JSON must still validate static matrix
shape and public outcomes. Inspect status, full diff, and recent log. Stage only
scoped files and commit, without amend:

```text
fix: complete ISO cadence verification
```

Require clean worktree, then rerun:

```bash
./scripts/test-all.sh
```

Expected: 65 PASS / 0 FAIL / 65 TOTAL; coverage population 36; BSim pins
unchanged; build contract 95/95. Production builds need not repeat because no
compiled production behavior changes beyond local variable naming/log argument
and comments, and both targets already built successfully at `60e2ac1`.

Do not push, merge, open PR, amend, run hardware, flash, or touch GitHub state.
Return files, focused checks, full gate/coverage/contract/BSim results, commit,
status, warnings/deviations, and remaining hardware blocker.
