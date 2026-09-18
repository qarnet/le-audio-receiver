# PB-031 P0e handoff: calibrate reconnect seven-PLC recipe

Status: Approved corrective calibration phase. P2 remains blocked until this
phase passes identified AMD, Intel, and ARM calibration.

## Goal

Add one exact stateful recipe for the measured second segment of
`reconnect_second_stream_10ms`:

```text
start7_10ms_l = PLC count 7; corpus first 0 count 100
```

The recipe has 107 decoder actions, 100 source-valid outputs, 7 PLC actions,
and uses portable reference `bsim_48k_10ms_120b_l.pcm` from frame 0. Calibrate
this ninth stateful-valid record on identified AMD, Intel, and ARM environments
before P2 maps reconnect segment 2 to it.

User-observable test behavior after P2 resumes: reconnect segment 2 accepts its
measured 7 startup PLC actions followed by exact corpus frames 0 through 99,
while transport, lifecycle, action counts, PCM limits, and all negative controls
remain strict.

## Grounding evidence

- Accepted P1 reconnect evidence:
  `/tmp/opencode/pb031-p1-exact-gap-fix-20260915/reconnect_second_stream_10ms-run1/receiver.log`.
- P2 full-run evidence:
  `/tmp/opencode/pb031-p2-bsim-stage1.PKts1b`.
- P2 reached 25 of 26 runs. Reconnect segment 2 reported `trans2=7`,
  `pushes2=100`, and `total2=107`, then failed because the configured
  `start8_10ms_l` recipe required PLC metadata at action 7.
- Existing recipe authority is `tests/support/lc3_stateful_recipes.c` plus
  `tests/fixtures/lc3/stateful-reference-manifest.json`.
- P0d acceptance is retained at
  `/tmp/opencode/pb031-p0d-acceptance-4fbe9bc/acceptance-report.md`.
- P0d established schema 3, 38 records, eight stateful-valid records, four
  stateful mutations, and frozen limits `2048 / 512 / 32750`.
- Exact pinned revisions remain:
  - nrf `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956`;
  - Zephyr `fd9204a02d52630660ce8d729945a4dd743feabf`;
  - liblc3 `48bbd3eacd36e99a57317a0a4867002e0b09e183`.

## Worktree safety

Main worktree intentionally contains uncommitted P2 work. Before editing,
capture:

```bash
git status --short
git diff --stat
git log --oneline -8
```

Hash every pre-existing modified or untracked P2 file listed by `git status`.
Verify those bytes remain unchanged throughout P0e. Do not reset, restore,
checkout, stash, clean, discard, or create another Git worktree. Temporary
clean clones under `/tmp/opencode` are allowed after the P0e implementation
commit. Never stage a pre-existing P2 file.

Known pre-existing P2 paths include:

- `scripts/bsim-stage1-run.sh`;
- `scripts/bsim_stage1_parse.py`;
- `src/audio_stream_session.c`;
- `tests/bsim/CMakeLists.txt`;
- `tests/bsim/src/audio_sink_stub.c`;
- `tests/bsim/src/bsim_observer.c`;
- `tests/bsim/src/bsim_observer.h`;
- `tests/bsim/src/bsim_sink_oracle.h`;
- `tests/bsim/src/bsim_test_main.c`;
- `tests/bsim/stage1-scenarios.json`;
- `tests/unit/audio_stream_session/src/fake_observer.c`;
- `tests/unit/audio_stream_session/src/fake_observer.h`;
- `tests/unit/audio_stream_session/src/test_audio_stream_session.c`;
- `tests/unit/bsim_runner/test_bsim_stage1_parse.py`;
- `docs/development/portable-lc3-pcm-oracle-p2-handoff.md`;
- `tests/bsim/src/bsim_pcm_limits.h.in`.

If status differs at start, preserve every pre-existing path even when not
listed above.

## In scope

Implementation commit scope:

- `docs/development/portable-lc3-pcm-oracle-p0e-handoff.md`;
- `tests/support/lc3_stateful_recipes.c`;
- `tests/fixtures/lc3/stateful-reference-manifest.json`;
- `tests/fixtures/lc3/generate_stateful_references.sh`;
- `tests/fixtures/lc3/README.md`;
- `scripts/lc3_pcm_calibrate.py`;
- `tests/calibration/lc3_pcm_oracle/CMakeLists.txt`;
- `tests/calibration/lc3_pcm_oracle/src/main.c`;
- `tests/unit/lc3_pcm_calibrate/test_lc3_pcm_calibrate.py`.

Post-calibration evidence commit scope:

- this handoff document;
- `docs/development/portable-lc3-pcm-oracle-plan.md`;
- PB-031 Implementation Notes only in
  `docs/product/backlog/tasks/pb-031 - Make-LC3-PCM-test-oracle-platform-independent.md`.

Update untracked P2 handoff active truth from 38/eight to 39/nine after P0e
acceptance, but leave that file unstaged for the later P2 commit.

## Out of scope

- Production source, decoder behavior, BAP behavior, lifecycle, transport,
  PLC generation, scenario timing, thresholds, corpus bytes, and generated PCM
  bytes.
- Any P2 source or test change, including reconnect scenario mapping. P2 makes
  that mapping only after P0e acceptance.
- Manifest schema bumps, evidence schema bumps, new mutation records, source
  corpus generation, tolerance widening, exclusions, or old-record deletion.
- `flake.nix`, SDK revisions, release state, tags, pushes, PRs, or merges.
- Product-owned PB-031 title, priority, status, type, Description, or acceptance
  criteria.

## Exact implementation

### Recipe authority

Append `start7_10ms_l` as recipe position 9, zero-based index 8. Do not insert it
before existing recipes. Existing order and ARM mutation reference indices must
remain stable.

Add a shared two-step table:

```c
{LC3_STATEFUL_ACTION_PLC, 0U, 7U},
{LC3_STATEFUL_ACTION_CORPUS, 0U, 100U},
```

Recipe fields:

| Field | Exact value |
| --- | --- |
| `id` | `start7_10ms_l` |
| `source_stem` | `bsim_48k_10ms_120b_l` |
| `reference_path` | `bsim_48k_10ms_120b_l.pcm` |
| `reference_kind` | `LC3_STATEFUL_REFERENCE_PORTABLE_PCM` |
| `reference_first_frame` | `0U` |
| `duration_us` | `10000U` |
| `frame_bytes` | `120U` |
| `samples_per_frame` | `480U` |
| `output_action_count` | `107U` |
| `valid_frame_count` | `100U` |

Update both runtime recipe table and independent expected table in
`lc3_stateful_recipes.c`. Keep validator semantics and all existing recipes
unchanged.

### Manifest and strict generator

Append matching ninth entry to stateful manifest. Portable reference fields
must repeat existing authoritative left PCM metadata:

```json
{
  "kind": "portable-pcm",
  "path": "bsim_48k_10ms_120b_l.pcm",
  "first_frame": 0,
  "frame_count": 100,
  "size": 122880,
  "sha256": "b42fd31158d24d255214580a63261087d57322266563cec778abc1d9f7f830a7"
}
```

Append matching generator authority. Change exact diagnostic text from eight
recipes to nine recipes. No generated file is added. Default strict generation
must leave all LC3, portable PCM, and generated stateful PCM files unchanged.

### Host protocol

Append recipe to `EXPECTED_STATEFUL_RECIPES`. Keep original 26-record prefix,
existing eight stateful-valid records, and existing four mutations unchanged.
New exact order becomes:

- records 0 through 25: unchanged original prefix;
- records 26 through 33: unchanged existing stateful-valid records;
- record 34: `stateful-valid`, `start7_10ms_l`, reference
  `bsim_48k_10ms_120b_l.pcm`, 100 frames, 48000 samples, `pass`;
- records 35 through 38: unchanged four mutations.

Schema remains 3. Total becomes exactly 39 records. Atomic output, no overwrite,
bounded output, provenance, payload identity, source/hash checks, and mutation
evaluation remain unchanged.

### ARM protocol

Append matching CMake recipe authority. Set `METRIC_RECORD_COUNT` to `39U` and
PASS marker to `PB031_ARM_PASS metrics=39`. Do not add another embedded byte
array because left portable PCM is already embedded. Keep mutation reference
indices `{0U, 6U, 7U, 0U}` unchanged because new recipe is appended.

Keep static bounded memory, 8192-byte main stack, thread analyzer, warning
policy, source provenance, full-RRAM calibration layout, and production
partitions unchanged.

### Tests and fixture documentation

Update exact recipe identity/count assertions from 8 to 9 and protocol count
from 38 to 39. Preserve Mode A index assumptions at recipe positions 5 and 6.
Add explicit test proof that recipe position 9 expands to 7 PLC actions followed
by corpus sequence 0 through 99, has 107 total actions, 100 valid frames, and
uses portable 10 ms left reference.

Update fixture README active truth:

- table has nine recipes, with `start7_10ms_l` appended;
- six normal-start recipes use portable PCM directly;
- ARM image validates nine recipes and emits 39 metrics;
- thread analyzer follows 39 metric lines;
- PASS marker is `PB031_ARM_PASS metrics=39`.

Historical accepted evidence sections and old P0/P0c/P0d handoffs retain their
original record counts. Do not rewrite history.

## Focused verification before implementation commit

```bash
bash tests/fixtures/lc3/generate_stateful_references.sh
git diff --exit-code -- tests/fixtures/lc3/bsim_*.lc3 tests/fixtures/lc3/bsim_*.pcm tests/fixtures/lc3/stateful_*.pcm
python3 -m unittest discover -s tests/unit/lc3_pcm_calibrate -p 'test_*.py' -v
nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d /tmp/opencode/pb031-p0e-pcm-oracle \
  tests/unit/pcm_oracle -p -t run
nix develop -c west build --no-sysbuild \
  -b xiao_nrf54l15/nrf54l15/cpuapp \
  -d /tmp/opencode/pb031-p0e-arm-calibration \
  tests/calibration/lc3_pcm_oracle -p
nix develop -c backlog doctor
git diff --check
```

Known Nix failure is `error: attribute 'x86_64-darwin' missing` at
`flake.nix:29:29`. Record it if encountered, use only already-established pinned
NCS toolchain equivalents, and do not edit `flake.nix`.

Run local AMD calibration twice from the dirty main tree only as a protocol
smoke. Each run must have 39 records, nine passing stateful-valid records, four
`max-error` mutations, and identical repeated metrics. Dirty provenance is not
formal acceptance.

## Implementation commit

After focused checks pass:

1. Inspect full status/diff/log.
2. Stage only implementation-scope files above.
3. Confirm no pre-existing P2 file is staged.
4. Commit exactly:

```text
test: add reconnect PCM oracle recipe
```

Do not amend.

## Cross-platform acceptance

Create a clean temporary clone under `/tmp/opencode` from committed P0e HEAD.
Do not use `git worktree`. Create a fresh evidence root named with P0e and short
commit. Preserve raw commands, stdout/stderr, identities, reports, validation,
resource data, worktree-preservation proof, and evidence SHA-256 manifest.

Adapt copies of prior external validators from
`/tmp/opencode/pb031-p0d-acceptance-4fbe9bc/` for exactly 39 records and nine
stateful-valid records. Validators remain external evidence, not repository
source. Require:

- exact 39-record identity/order equality across AMD, Intel, and ARM;
- two full repeats per environment with identical metric JSON;
- nine stateful-valid records all `pass`;
- four mutations all `max-error`;
- exact record 34 identity/dimensions for `start7_10ms_l`;
- frozen limits unchanged;
- cross-platform stateful population exactly 9 recipes by 3 environments;
- overall envelope still inside `2048 / 512 / 32750`.

### AMD

Use clean committed clone, GCC 14.3.0, and clean minimal pinned NCS layout as in
P0d. Capture raw `lscpu`, `uname`, compiler version, revisions, clean status,
two schema-3 reports, repeat validator output, and report hashes.

### Intel

SSH access is available as `thomas-nuc`. Use batch SSH. Build a fresh remote
root under `/tmp/opencode`; do not reuse P0d remote source. Transfer a Git bundle
for exact P0e commit plus the pinned minimal nrf, Zephyr, and liblc3 sources.
Require raw `GenuineIntel`, Core i3-6100U identity, Clang 21.1.8, clean exact
commit, pinned clean SDK revisions, two reports, repeat equality, and copied
local evidence. Do not alter remote persistent configuration.

### ARM

Use attached identified nRF54L15. Before every target-changing action, run
`nrf-probes` and retain raw DPIDR, AP map, PART, and VARIANT evidence. Start UART
capture before flash/reset. Flash clean committed calibration image using
project OpenOCD path only. Never use probe-rs. Capture two complete UART runs,
each with one BEGIN, one SOURCE, 39 metrics, main stack analyzer line, and
`PB031_ARM_PASS metrics=39`; no PB031 failure, fault, error, or warning.

Record full RRAM and RAM use, image size, stack use/headroom, compiler version,
flash bytes, and source/image hashes. If image does not fit full RRAM or bounded
memory is unclear, stop.

After ARM acceptance, rebuild and restore production firmware from same clean
committed source, with fresh identity evidence. Verify cpuapp and FLPR hashes,
flash bytes, exact P0e commit boot banner, required BLE/settings/timing/I2S/FLPR/
advertising markers, and zero UART warning/error lines. Existing documented
build diagnostics remain explainable; no new warning is accepted.

### Main worktree preservation

After all acceptance work, verify every pre-existing P2 path has same SHA-256 as
before P0e and remains unstaged. P0e-owned files may differ only through commits.

## Acceptance documentation and commit

Only after all three environments pass:

- append completed P0e evidence to this handoff;
- add P0e measured correction and acceptance to parent plan, changing active
  P2 status from blocked to resumed;
- append concise P0d/P0e chronology and external evidence path to PB-031
  Implementation Notes without changing product-owned fields;
- update untracked P2 handoff to require 39 records and nine stateful-valid
  records, but keep it unstaged.

Stage only tracked evidence-document scope and commit exactly:

```text
docs: record P0e oracle acceptance
```

Do not amend, push, merge, open a PR, or add attribution.

## Stop conditions

Stop without weakening checks if:

- `start7_10ms_l` exceeds frozen limits on any environment;
- any mutation passes or requires threshold widening;
- reports differ in identity/order or repeats differ;
- original corpus or generated stateful bytes change;
- ARM image does not fit or memory is unbounded;
- unexplained build, flash, UART, or test warning appears;
- P0e requires production behavior, P2 scenario timing, or corpus changes;
- two materially different implementation/debug attempts fail.

Preserve partial state and report exact blocker, attempts, logs, diff, status,
one precise question, and smallest suspected next step. Do not guess, normalize
noise, repin output, add exclusions, widen thresholds, or modify P2 files.

## Executor return

Return:

- implementation and evidence commit hashes/messages;
- exact files changed per commit;
- focused test/build results;
- AMD, Intel, and ARM report paths, identities, compilers, repeat proof, new
  recipe metrics, and full nine-recipe envelope;
- ARM image/RRAM/RAM/stack data and production restoration proof;
- accepted warnings/diagnostics with reasons;
- P2 worktree byte-preservation proof and final status;
- deviations and blockers.
