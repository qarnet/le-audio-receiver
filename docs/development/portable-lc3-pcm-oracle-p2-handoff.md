# PB-031 P2: payload-and-recipe-aware BSim PCM oracle handoff

Date: 2026-09-18

Product item: [PB-031](../product/backlog/tasks/pb-031%20-%20Make-LC3-PCM-test-oracle-platform-independent.md)

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`

Starting HEAD for final repair: `558fbe07b5d7a48c73d8b38cb4d03766cb7769f3`

## Goal

Replace BabbleSim receiver acceptance based on platform-specific decoded-PCM
FNV hashes with payload-and-recipe-aware numerical comparison against the
checked-in stateful references accepted by P0d and P0e. Exact accepted LC3 payload bytes
must identify source fixture and corpus frame. A per-channel recipe cursor must
advance for every PLC or valid decode action. Only source-valid outputs enter
numerical metrics.

Preserve exact transport, frame-count, PLC, lifecycle, routing, validity,
decoder-error, malformed-shape, guard, warning, and teardown contracts. Keep
production firmware signatures and behavior unchanged.

## Worktree warning

Main worktree contains the uncommitted, knowingly failing first P2 attempt.
Repair that work in place. Do not reset, stash, clean, restore, or discard it.
Do not use a second worktree. Edit only the P2 paths listed below, then stage
only the accepted P2 result.

The first attempt failed because it treated receiver-controller
`bt_iso_recv_info.seq_num` as sender fixture identity and compared post-PLC
decoder output with lossless per-frame PCM. P0c disproved both assumptions.
Do not preserve either design.

## Scope

### Required implementation files

- `src/audio_stream_session.c`
- `tests/bsim/CMakeLists.txt`
- `tests/bsim/src/audio_sink_stub.c`
- `tests/bsim/src/bsim_observer.c`
- `tests/bsim/src/bsim_observer.h`
- `tests/bsim/src/bsim_sink_oracle.h`
- `tests/bsim/src/bsim_test_main.c`
- `tests/bsim/stage1-scenarios.json`
- `scripts/bsim_stage1_parse.py`
- `scripts/bsim-stage1-run.sh`
- `tests/unit/audio_stream_session/src/fake_observer.c`
- `tests/unit/audio_stream_session/src/fake_observer.h`
- `tests/unit/audio_stream_session/src/test_audio_stream_session.c`
- `tests/unit/bsim_runner/test_bsim_stage1_parse.py`
- `tests/bsim/src/bsim_pcm_limits.h.in`
- this handoff, only for implementation facts and final evidence
- parent plan, only to mark P2 accepted after verification succeeds
- PB-031 `Implementation Notes`, only after verification succeeds

### Read-only P0/P0c inputs

- `tests/support/pcm_oracle.c`
- `tests/support/pcm_oracle.h`
- `tests/support/lc3_stateful_recipes.c`
- `tests/support/lc3_stateful_recipes.h`
- `tests/fixtures/lc3/portable-oracle-manifest.json`
- `tests/fixtures/lc3/stateful-reference-manifest.json`
- four checked-in `bsim_*.lc3` files
- four checked-in `bsim_*.pcm` portable references
- `tests/fixtures/lc3/stateful_48k_10ms_skip20_l.pcm`
- `tests/fixtures/lc3/stateful_48k_10ms_loss48x18_r.pcm`
- `tests/fixtures/lc3/stateful_48k_7p5ms_modea_start_r.pcm`

Do not edit these inputs. `pcm_oracle` API and arithmetic are already accepted.
`lc3_stateful_recipes` table and both manifests are already accepted. Escalate
before changing any of them.

## Out of scope

- P3 migration of `tests/unit/decode`.
- Fixture regeneration or any LC3/PCM binary change.
- Threshold recalibration or manifest-policy change.
- Production decoder, volume, BAP, sink, transport, Mode A assembler, ISO
  tracker, or LC3 compiler-flag changes.
- New scenario, send-count, PLC-count, frame-total, lifecycle, warning
  allowlist, or teardown baseline.
- Controller change or use of controller sequence as fixture identity.
- Hardware, HIL, flashing, release, workflow, and product-status changes.
- Full P4 canonical gate and receiver builds.
- Editing PB-031 title, status, priority, type, Description, acceptance
  criteria, or product-owned Implementation Plan.

## Grounding evidence

1. P0c accepted commit `262805eb51731ff7b2511e7e522762e4061bb270`
   and acceptance commit `88a073afa5d9ccbd56d28500830f3bc79572293c`.
   P0d corrected Mode A 7.5 ms history at
   `4fbe9bc135d9077aff90a56f0f6f70fe92637ebb`; documentation acceptance is
   recorded through current starting HEAD
   `3f6dee06fe357fb39b3b58d37ebd5539c2ebec0b`. P0e accepted
   `b38cfecebe6842172f2885e9439799a538946b8d`, appending the measured
   `start7_10ms_l` reconnect recipe. External evidence is
   `/tmp/opencode/pb031-p0e-acceptance-b38cfec/acceptance-report.md`.
2. Receiver controller sequence is controller/ISOAL timeline state, not sender
   fixture identity. Failed first-attempt logs prove values can reach 128 while
   accepted fixture payload remains inside the 128-frame corpus.
3. P0c proves every source stream has 128 unique exact payload byte strings and
   same-duration left/right streams have zero byte-identical overlap. Exact
   bytes, not hashes and not controller sequence, are authoritative identity.
4. LC3 output depends on decoder history after PLC. P0d and P0e freeze nine
   recipes, four portable PCM files, and three generated stateful PCM files that
   model Stage 1 histories exactly. P0e appends `start7_10ms_l`: PLC 7 then
   corpus 0 through 99.
5. P0e accepted six schema-3 calibration reports across identified AMD, Intel,
   and ARM environments. All nine stateful-valid records pass; all four
   stateful mutations return `max-error`. Corrected Mode A 7.5 ms and reconnect
   second-stream valid envelopes remain inside frozen policy.
6. Frozen limits remain solely in `portable-oracle-manifest.json`:

   ```text
   max_abs_error=2048
   max_rms_error=512
   min_correlation_q15=32750
   ```

7. `audio_sink_push()` observes PCM after `audio_volume_apply()`. BSim starts
   unmuted at volume 195. Scale reference samples with C integer expression
   `(int32_t(sample) * 195) / 255`, truncating toward zero, and fail if runtime
   BSim volume differs or is muted.
8. Malformed valid SDU length rejection occurs before decode, Mode A store,
   observer pre-push, volume, and sink push. It must create no recipe action.
9. `session_mode_a_store_and_process()` retains exact per-half compressed bytes
   in `s.modea_ev`. Mono and Mode B still have current callback payload bytes at
   the immediate test-only pre-push call.
10. RX callback decode, test-only pre-push observation, and sink push occur in
    one serialized call path. A bounded copied snapshot is sufficient; no
    allocation, queue, mutex, or production state is needed.

## Exact observer API and ownership

Replace sequence metadata from the first attempt. Use a bounded copied payload
snapshot with this public test-only shape in `bsim_observer.h`:

```c
#define BSIM_OBSERVER_MAX_PAYLOAD_BYTES 120U

struct bsim_observer_push_half {
	bool source_valid;
	uint16_t payload_len;
	uint8_t payload[BSIM_OBSERVER_MAX_PAYLOAD_BYTES];
};

struct bsim_observer_push {
	struct bsim_observer_push_half left;
	struct bsim_observer_push_half right;
};

void bsim_observer_pre_push(bool l_valid, const uint8_t *l_payload,
			    size_t l_payload_len, bool r_valid,
			    const uint8_t *r_payload, size_t r_payload_len);

bool bsim_observer_take_push(struct bsim_observer_push *out);
```

Include `<stddef.h>` for `size_t`. Keep this API behind existing
`CONFIG_BSIM_OBSERVER` build use. No observer symbol enters production images.

`bsim_observer_pre_push()` rules:

- valid half requires non-null payload and length `1..120`;
- invalid half requires null payload and zero length;
- copy exact bytes into one static test-only snapshot;
- mark snapshot ready only when both halves are well formed;
- never retain caller pointers;
- do not allocate;
- do not use controller sequence;
- `bsim_observer_take_push()` copies snapshot to caller and consumes readiness;
- missing, malformed, or stale snapshot returns false so sink fails closed.

Existing lifecycle counters remain atomic. Snapshot needs no lock because
pre-push and immediate sink take occur on same serialized RX call stack. Fake
observer may retain a bounded history of copied snapshots for unit assertions.

Remove first-attempt sequence getters and all tests that treat controller
sequence as fixture identity. Keep controller sequence in production tracker,
Mode A structures, and diagnostics unchanged.

### `audio_stream_session.c` call mapping

- Mono valid packet: copy same exact packet payload to left and right halves.
- Mode B valid packet: split exact packet bytes into left and right channel
  payloads using configured per-channel byte count. Current Stage 1 geometry is
  one frame block, but derive length from configured shape rather than a magic
  constant.
- Mode A resolved event: copy each `s.modea_ev.data[channel]` only when that
  half is source-valid; concealed half is invalid with null/zero payload.
- Current LOST/empty mono or Mode B packet: both halves invalid, null, zero.
- Synthetic omitted mono or Mode B PLC: both halves invalid, null, zero. Restore
  `session_mode_plc_push()` production signature; do not pass synthetic
  controller sequence into observer metadata.
- Synthetic or one-sided Mode A loss: missing half invalid/null/zero; carried
  valid half uses its exact assembler payload.
- Malformed valid packet and hard decode failure: no pre-push snapshot because
  no sink push occurs.

All production behavior outside `#if defined(CONFIG_BSIM_OBSERVER)` remains
byte-for-byte equivalent in control flow and data flow.

## BSim build inputs

In `tests/bsim/CMakeLists.txt`:

- compile `tests/support/pcm_oracle.c`;
- compile `tests/support/lc3_stateful_recipes.c`;
- include `tests/support/` and generated-header directory;
- embed all four source LC3 corpus files;
- embed all four portable PCM references;
- embed all three generated stateful PCM references;
- read `portable-oracle-manifest.json` at configure time;
- fail configure unless schema is 2 and all three `pcm_limits` members exist as
  integers;
- generate test-only limit constants through `bsim_pcm_limits.h.in`;
- add configure dependencies for both manifests, recipe source/header,
  comparator source/header, generated-header template, and embedded binaries.

Do not copy threshold values into C source or scenario JSON. Keep C build
assertions for exact fixture/reference byte sizes and geometry. Call
`lc3_stateful_recipes_validate()` before scenario execution; failure is fatal.

## Recipe binding and cursor

Keep public `audio_sink.h` API exact. Add private BSim-only bindings from each
accepted recipe to its source LC3 array and reference PCM array. Bind by exact
recipe ID/source/reference strings from `lc3_stateful_recipes[]`; missing,
duplicate, or mismatched binding fails before audio acceptance.

Each segment owns independent left and right recipe cursors:

```text
recipe pointer
step index
offset inside current step
total action count
valid action/reference-frame count
PLC action count
```

Cursor state starts fresh for every segment, including reconnect segment 2.
Partial lifecycle segments may end on a valid recipe prefix. Full scenarios
must consume the complete recipe.

### Scenario recipe mapping

| Scenario | Segment | Left recipe | Right recipe | Completion |
| --- | ---: | --- | --- | --- |
| `mono_10ms` | 1 | `start8_10ms_l` | `start8_10ms_l` | full |
| `mono_7p5ms` | 1 | `start11_7p5ms_l` | `start11_7p5ms_l` | full |
| `modea_10ms` | 1 | `start8_10ms_l` | `start8_10ms_r` | full |
| `modea_7p5ms` | 1 | `modea_start_7p5ms_l` | `modea_start_7p5ms_r` | full |
| `modea_reverse_start_10ms` | 1 | `start8_10ms_l` | `start8_10ms_r` | full |
| `modeb_10ms` | 1 | `start8_10ms_l` | `start8_10ms_r` | full |
| `modeb_7p5ms` | 1 | `start11_7p5ms_l` | `start11_7p5ms_r` | full |
| `invalid_sdu_resume_10ms` | 1 | `skip20_10ms_l` | `skip20_10ms_l` | full |
| `modea_one_cis_loss_10ms` | 1 | `start8_10ms_l` | `loss48x18_10ms_r` | full |
| `modea_first_stop_10ms` | 1 | `start8_10ms_l` | `start8_10ms_r` | prefix |
| `release_without_disable_10ms` | 1 | `start8_10ms_l` | `start8_10ms_l` | prefix |
| `disconnect_streaming_10ms` | 1 | `start8_10ms_l` | `start8_10ms_l` | prefix |
| `reconnect_second_stream_10ms` | 1 | `start8_10ms_l` | `start8_10ms_l` | prefix |
| `reconnect_second_stream_10ms` | 2 | `start7_10ms_l` | `start7_10ms_l` | full |
| `duplicate_release_10ms` | 1 | `start8_10ms_l` | `start8_10ms_l` | prefix |
| three no-audio scenarios | none | none | none | none |

## Payload identity and action consumption

For every accepted sink push, take exactly one fresh observer snapshot before
changing oracle counters.

For each channel independently:

1. Read next expected expanded recipe action.
2. If action is PLC, require source-invalid metadata with zero payload. Advance
   total and PLC cursors. Do not enter samples into `pcm_oracle`.
3. If action is CORPUS, require source-valid metadata and exact recipe frame
   length.
4. Identify payload by exact byte comparison against every frame of both
   same-geometry source streams. Require exactly one match. Do not use payload
   hash as identity.
5. Require identified source stem equals recipe `source_stem` and identified
   frame index equals current recipe corpus sequence.
6. Select next reference frame by valid-action cursor, not source sequence:
   - portable reference: `reference_first_frame + valid_cursor`;
   - generated reference: `valid_cursor` in generated valid-only trace.
7. Scale reference samples by volume 195 and accumulate actual strided channel
   samples with `pcm_oracle_accumulate()`.
8. Only after every check and accumulation succeeds, advance action and valid
   cursors.

Any missing snapshot, unexpected PLC, missing PLC, wrong channel, off-by-one
payload, unknown payload, duplicate payload identity, geometry mismatch, recipe
overrun, or reference overrun fails immediately with segment, channel, recipe,
action index, and identified source/frame where available.

This yields exact required histories:

- normal 10 ms: PLC 8, then corpus 0 through 99;
- mono/Mode B 7.5 ms: PLC 11, then corpus 0 through 99;
- Mode A 7.5 ms left: PLC 12, then corpus 0 through 100;
- Mode A 7.5 ms right: PLC 10, corpus 0, PLC 2, then corpus 1 through 100;
- malformed resume: PLC 8, corpus 0 through 19, no action for malformed frame
  20, then corpus 21 through 100;
- one-CIS loss right: PLC 8, corpus 0 through 47, PLC 18, then corpus 48
  through 81;
- one-CIS loss left: normal `start8_10ms_l` through corpus 99.
- reconnect second stream: PLC 7, then corpus 0 through 99.

## Sink metrics and retained contracts

Keep existing startup-boundary, `pushes`, `transients`, `startup_zero`,
`startup_plc`, energy, PACS, malformed-sample, push-after-stop, lifecycle, and
audio-stats behavior. Recipe consumption occurs for startup transients before
the existing boundary return. Numerical comparison occurs only for valid
actions.

For every segment and channel record:

- recipe ID;
- total recipe actions consumed;
- valid recipe actions consumed;
- PLC recipe actions consumed;
- compared frames and samples;
- post-boundary excluded frames;
- maximum absolute error;
- squared error;
- RMS error;
- correlation Q15;
- evaluation result.

Use these exact accounting invariants. First independently expand each
channel's recipe prefix of length `transients` into `pre_valid` and `pre_plc`.
This is required because Mode A 7.5 ms has one source-valid output per channel
before both halves become source-valid together. No new PASS fields are needed:
the parser and C sink derive both prefix counts from immutable recipe data.

```text
recipe_actions == transients + pushes
recipe_valid == compared_frames
recipe_plc == recipe_actions - recipe_valid
pre_valid + pre_plc == transients
post_boundary_excluded == recipe_plc - pre_plc
recipe_valid == pre_valid + pushes - post_boundary_excluded
recipe_plc == pre_plc + post_boundary_excluded
compared_samples == compared_frames * samples_per_channel
```

For full `modea_7p5ms`, both channels consume 113 actions and compare 101 valid
frames. Left derives `pre_valid=1`, `pre_plc=12`; right derives
`pre_valid=1`, `pre_plc=12` despite right's valid action occurring before its
last two startup PLC actions. Both report zero post-boundary exclusions. Do not
drop either source-valid startup output from numerical comparison.

Full recipes also require exact recipe `output_action_count` and
`valid_frame_count`. Prefix recipes require counts equal the exact expanded
prefix at observed action count and must not overrun the recipe.

Finalize and evaluate each active channel with manifest-generated limits and
`min_samples` equal to one configured channel frame. Every pushed segment must
evaluate `pass` on both channels. No-audio scenarios retain zero metrics and
`insufficient-samples` while proving zero actions and pushes.

Retain direct routing evidence:

- mono requires exact `L == R` for every post-boundary sample pair;
- stereo records differing sample positions and requires a nonzero count.

Remove decoded-PCM FNV accumulation, fields, validation, output, runner arrays,
CLI options, and baseline mode. Never retain old decoded hashes as fallback.
Client TX FNV remains untouched and exact.

`SINK_SEG` human diagnostics use `pcm_oracle_result_name()`. Receiver `PASS`
may use numeric result values. Both records expose compiled limits and stable,
short tokens for all recipe and metric fields. Suggested per-segment tokens:

```text
lrid1 lact1 lval1 lplc1 rrid1 ract1 rval1 rplc1
lfr1 lsm1 lex1 lmax1 lsse1 lrms1 lcorr1 lres1
rfr1 rsm1 rex1 rmax1 rsse1 rrms1 rcorr1 rres1
```

Use `none` plus zero counts for absent/no-audio segment recipe fields.

## Scenario schema and parser

Keep the intended P2 scenario schema at version 3. Add required
`receiver_oracle` list to every scenario. Each entry has exactly:

```json
{
  "left_recipe": "start8_10ms_l",
  "right_recipe": "start8_10ms_l",
  "completion": "full"
}
```

List order is segment order. Use the mapping table above. No-audio scenarios
use an empty list. Allowed completion values are only `full` and `prefix`.

Parser must load and strictly validate `stateful-reference-manifest.json` using
the accepted calibration validator, then reject unknown recipe IDs, mixed
geometry, wrong mapping, wrong completion, missing/unknown fields, wrong
segment count, or any mutation of the fixed 17-scenario/26-run matrix.

Schema 3 must also:

- remove every `known.full`, `known.l`, and `known.r` value;
- permit only `known.total`;
- preserve every existing `known.total`, run count, decoder-call count, channel
  mode, transport entry, fixture choice, malformed index, and send contract;
- state that exact client TX FNV remains parser-owned;
- state that receiver acceptance uses exact payload identity and stateful
  recipes, never controller sequence.

Parser must independently enforce:

- receiver-emitted limits equal portable manifest limits;
- reported recipe IDs equal scenario `receiver_oracle`;
- recipe progress equals exact full recipe or exact observed prefix;
- all recipe/metric accounting equations above;
- result is pass and numerical values satisfy limits for pushed segments;
- configured geometry matches recipe and transport manifest;
- exact mono equality or stereo distinction;
- exact one-CIS-loss right recipe with 18 post-boundary exclusions;
- malformed frame 20 creates no action through exact `skip20_10ms_l` progress;
- reconnect gets fresh segment-2 recipe state;
- all existing exact totals, sends, transport hashes, lifecycle counts, PLC
  placement, decode errors, malformed observer, teardown ordering, guards,
  and fault scans;
- scenario 17 remains sole exact warning allowlist.

Apply `known.total` to segment 1 for every declaring scenario, including
partial lifecycle and reconnect. Retain exact derived segment-2 total for
reconnect. `--known-total` and `--no-known` may remain. Decoded hash CLI options
must be absent.

## Runner

Remove decoded-PCM hash arrays, arguments, pairwise checks, summary, and
baseline-repin output from `scripts/bsim-stage1-run.sh`. Reject
`BSIM_BASELINE=1` before toolchain setup with a clear message.

Keep unchanged:

- exact 17 scenarios and 26 runs;
- shared-tree lock;
- caller-owned `BSIM_LOG_ROOT` validation and preservation semantics;
- private log cleanup semantics;
- toolchain and compile flags;
- process launch, timeout, and exit checks;
- strict parser invocation;
- final PASS/FAIL behavior.

Summary may print pushes, recipe progress, and concise left/right numerical
metrics. Missing parser summary fields fail closed.

## Focused tests

### Audio-stream-session observer boundary

Update fake observer and tests to prove:

- mono copies exact same payload to both valid halves;
- Mode B splits exact left and right compressed payload bytes;
- Mode A preserves each exact carried half payload even when controller
  sequence values differ;
- one-sided and synthetic Mode A loss uses invalid/null/zero for concealed half
  and exact bytes for carried half;
- LOST, empty-SDU, and synthetic mono/Mode B PLC use invalid/null/zero halves;
- malformed rejection and hard decode failure create no pre-push snapshot;
- malformed resume carries exact next valid payload without relying on sequence.

Remove assertions that synthetic or carried controller sequences identify
fixture frames. Retain every existing decode, stats, lifecycle, admission,
cadence, and sink-boundary assertion.

### Parser and scenario boundary

Adapt rather than delete pre-P2 test coverage. P1 had 94 parser assertions; do
not reduce breadth to make P2 pass. Preserve schema error cases, transport byte
mutation cases, malformed fixture hashing, no-transport behavior, known-value
precedence, fault allowlist, and all `BSIM_LOG_ROOT` ownership/preflight tests.

Add fail-closed tests for:

- schema 3 rejects decoded hash keys;
- missing, unknown, or changed `receiver_oracle` mapping;
- unknown recipe ID and wrong completion mode;
- missing/non-integer recipe fields in receiver PASS;
- recipe ID mismatch;
- action, valid, PLC, compared, excluded, and sample accounting mismatch;
- asymmetric Mode A 7.5 ms startup derives one pre-boundary valid frame and 12
  pre-boundary PLC frames per channel, compares 101 frames per channel, and
  reports zero post-boundary exclusions;
- full recipe truncated or prefix overrun;
- manifest limit mismatch;
- max, RMS, correlation, and non-pass result violations;
- mono routing mismatch and stereo no-distinction;
- exact one-CIS-loss right progress and 18 exclusions;
- malformed-resume `skip20` progress;
- reconnect fresh second recipe;
- exact TX byte/sequence hashes and lifecycle checks remain enforced;
- `BSIM_BASELINE=1` fails before toolchain setup.

Retain existing `pcm_oracle` negative controls and P0c stateful mutation tests;
do not duplicate their arithmetic implementation inside parser tests.

## User-observable proof

Smallest public boundary is full Stage 1 execution:

- all 17 scenarios and 26 runs pass;
- receiver logs show expected recipe IDs/progress and portable numerical
  metrics inside frozen limits;
- client logs retain exact TX hashes;
- loss, malformed, reconnect, and partial lifecycle histories match exact
  recipes;
- no decoded-PCM hash pin contributes to acceptance;
- no warning/error appears except existing exact scenario-17 warning.

## Verification

Run from repository root. Use fresh `/tmp/opencode` build/output directories.

```bash
bash tests/fixtures/lc3/generate_stateful_references.sh
python3 tests/unit/lc3_pcm_calibrate/test_lc3_pcm_calibrate.py
python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py

nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/opencode/pb031-p2-pcm-oracle \
  tests/unit/pcm_oracle -p -t run

nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/opencode/pb031-p2-audio-stream-session \
  tests/unit/audio_stream_session -p -t run

nix develop -c env \
  BSIM_LOG_ROOT="$(mktemp -d /tmp/opencode/pb031-p2-bsim-stage1.XXXXXX)" \
  bash scripts/bsim-stage1-run.sh

backlog doctor
git diff --check
```

Inspect every preserved receiver/client log for warnings and errors. Only
scenario 17 may contain exact existing warning
`Invalid operation in state: releasing`.

If any valid BSim channel exceeds frozen limits, stop. Do not widen thresholds,
change corpus, repin output, exclude a source-valid action, substitute controller
sequence, or weaken routing, count, PLC, lifecycle, warning, recipe, or
transport checks. Return measured metrics, recipe/action evidence, logs, and
current diff for Delegator analysis.

## Commit and return

After all verification passes:

1. Append factual P2 implementation and evidence to PB-031 Implementation
   Notes. Do not edit product-owned fields or acceptance checkboxes.
2. Update this handoff only with concise implementation/result facts.
3. Update parent plan status and P2 section to record accepted result, exact
   evidence root, and next phase as P3. Do not rewrite prior phase history.
4. Inspect `git status`, `git diff`, and `git log --oneline -10`.
5. Stage only P2 paths listed in this handoff.
6. Commit once with message `test: use payload-aware portable PCM oracle`.
7. Do not push, merge, open a PR, amend, force-push, add attribution, reset,
   stash, or clean.

Return changed files, behavior, exact commands and results, full BSim metric
envelope, preserved log root, commit hash/message, deviations, and blockers.

Stop without committing and escalate if two materially different repair
attempts fail, runtime evidence contradicts an accepted recipe, valid output
exceeds frozen limits, exact payload identity is ambiguous, parser/test breadth
would need reduction, or repair needs threshold widening, fixture change,
production behavior change, architecture invention, destructive action, or
scope expansion.

## Repair facts

Current dirty P2 work already implements bounded payload snapshots and processes
recipe actions before the shared fully-valid boundary. Preserve that work.

Repairs already completed and verified before P0e:

1. Removed stale `start13_7p5ms_l`/`start13_7p5ms_r` IDs in favor of accepted
   `modea_start_7p5ms_l`/`modea_start_7p5ms_r`.
2. Embedded and bound `stateful_48k_7p5ms_modea_start_r.pcm` with exact 72720
   byte size.
3. Replaced transient-is-PLC assumptions with exact recipe-prefix accounting in
   C and Python.
4. Added asymmetric Mode A 7.5 ms fixtures and fail-closed mutations. Parser
   suite now passes 134 assertions, above P1's 94.
5. Strict generator passed with unchanged hashes; calibration suite passed 37
   tests before P0e and now passes 38; native `pcm_oracle` passed 12; native
   `audio_stream_session` passed 48.
6. Full Stage 1 reached 25 of 26 runs. Successful numerical envelope was maximum
   error 257, RMS 182, and correlation Q15 32767. All 26 receiver/client logs
   contained no unexpected warning or error; only scenario 17 had accepted
   `Invalid operation in state: releasing`.

Final repair:

7. Reconnect segment 2 must map both channels to accepted `start7_10ms_l`, not
    `start8_10ms_l`. Require seven startup PLC actions, 100 valid frames, and
    107 total actions. Updated both authoritative mappings:
    `tests/bsim/src/audio_sink_stub.c` and
    `tests/bsim/stage1-scenarios.json`, plus parser-owned fixed mapping and
    reconnect parser tests that pin this exact segment-2 mapping and reject
    `start8_10ms_l` there.
8. P0e adds a ninth accepted stateful recipe. Increased
   `BSIM_RECIPE_BINDING_MAX` from 8 to 9 so BSim binds the complete accepted
   recipe table before scenario execution.

Do not add new observer fields, PASS tokens, recipe IDs, thresholds, fixtures,
or production behavior to solve these defects.

## Accepted result

P2 verification passed on 2026-09-18. The complete payload-aware oracle keeps
exact transport and lifecycle checks while comparing only source-valid PCM
against immutable stateful recipes and portable numerical limits. Full Stage 1
passed all 17 scenarios and 26 runs with maximum error 257, maximum RMS 182,
and minimum correlation Q15 32767. Reconnect segment 2 reported
`start7_10ms_l` on both channels with 107 actions, 100 valid frames, seven PLC
actions, and 48000 compared samples per channel.

Focused results: strict stateful generator hashes unchanged; calibration tests
38/38; parser tests 140 PASS / 0 FAIL; native `pcm_oracle` 12/12; native
`audio_stream_session` 48/48; `backlog doctor`; and `git diff --check` passed.
All 52 preserved receiver/client logs were inspected under
`/tmp/opencode/pb031-p2-bsim-stage1.C6FBBY`. Only scenario 17 contained the
exact allowlisted warning `Invalid operation in state: releasing`. Read-only
P0/P0c recipe, manifest, corpus, reference, and comparator inputs retained
their pre-verification SHA-256 values. P3 is next.
