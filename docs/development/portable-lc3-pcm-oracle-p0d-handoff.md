# PB-031 P0d handoff: correct Mode A 7.5 ms stateful recipes

Status: Accepted, 2026-09-17.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

Starting repository HEAD:
`88a073afa5d9ccbd56d28500830f3bc79572293c`.

## Why P0d exists

P2's exact payload observer disproved P0c's symmetric Mode A 7.5 ms recipe.
P0c modeled both channels as 13 PLC actions followed by corpus frames 0 through
99. Two focused BSim diagnostic runs instead produced the same 113-action trace:

| Channel | Exact expanded action history |
| --- | --- |
| Left | PLC actions 0 through 11; corpus frame 0 at action 12; corpus frames 1 through 100 at actions 13 through 112 |
| Right | PLC actions 0 through 9; corpus frame 0 at action 10; PLC actions 11 and 12; corpus frames 1 through 100 at actions 13 through 112 |

The first fully source-valid sink push is action 13. The 13 earlier actions are
startup transients, but each channel has 12 PLC actions and one source-valid
action. Totals are 226 decoder actions, 202 source-valid actions, and 24 PLC
actions. This matches the accepted pre-P2 Stage 1 totals exactly.

The right decoder receives corpus frame 0 before two PLC actions, so its later
valid output needs a generated stateful reference. Portable lossless PCM is not
a valid reference for that decoder history. P2 stays blocked until corrected
recipes and the new right trace pass the same AMD, Intel, and ARM calibration
gate used for P0c.

Diagnostic evidence:

- root: `/tmp/opencode/pb031-p2-modea7p5-diag-t5H2hQ`;
- normalized trace SHA-256:
  `eb0cf50a49d11bd694ffd5c860c1e8b227cb48dc16083d427b8ae5119b506f6f`;
- `run1/p2diag.trace` and `run2/p2diag.trace` are byte-identical;
- `trace-analysis.txt`, `trace-compare.txt`, and
  `restoration-post-rebuild-checks.txt` retain analysis and restoration proof.

## Worktree warning

Main worktree contains valuable uncommitted P2 implementation. Preserve it
exactly. Never reset, restore, checkout, stash, clean, or move to another
worktree for implementation. Stage only P0d files named below.

Existing P2-dirty or untracked paths that P0d must not edit or stage:

- `scripts/bsim-stage1-run.sh`
- `scripts/bsim_stage1_parse.py`
- `src/audio_stream_session.c`
- `tests/bsim/CMakeLists.txt`
- `tests/bsim/src/audio_sink_stub.c`
- `tests/bsim/src/bsim_observer.c`
- `tests/bsim/src/bsim_observer.h`
- `tests/bsim/src/bsim_sink_oracle.h`
- `tests/bsim/src/bsim_test_main.c`
- `tests/bsim/stage1-scenarios.json`
- `tests/unit/audio_stream_session/src/fake_observer.c`
- `tests/unit/audio_stream_session/src/fake_observer.h`
- `tests/unit/audio_stream_session/src/test_audio_stream_session.c`
- `tests/unit/bsim_runner/test_bsim_stage1_parse.py`
- `docs/development/portable-lc3-pcm-oracle-p2-handoff.md`
- `tests/bsim/src/bsim_pcm_limits.h.in`

## Goal

Replace the two inaccurate `start13_7p5ms_*` recipes with exact asymmetric
Mode A 7.5 ms recipes, add one checked-in right-channel stateful PCM reference,
and keep the schema-3 calibration protocol at exactly 38 records:

- 26 original portable records, unchanged and in the same order;
- eight stateful-valid records, with recipe positions five and six corrected;
- four existing mutation records, unchanged and in the same order.

Use these replacement recipe IDs and exact order positions:

| ID | Source | Steps | Actions | Valid | Reference |
| --- | --- | --- | ---: | ---: | --- |
| `modea_start_7p5ms_l` | `bsim_48k_7p5ms_90b_l` | PLC 12; corpus 0 count 101 | 113 | 101 | portable `bsim_48k_7p5ms_90b_l.pcm`, first frame 0 |
| `modea_start_7p5ms_r` | `bsim_48k_7p5ms_90b_r` | PLC 10; corpus 0 count 1; PLC 2; corpus 1 count 100 | 113 | 101 | generated `stateful_48k_7p5ms_modea_start_r.pcm`, first frame 0 |

The generated right trace contains only the 101 source-valid outputs in recipe
order. Exact size: `101 * 360 * 2 = 72720` bytes. PLC outputs advance decoder
state but are not stored or compared numerically.

## User-observable behavior proved

The public calibration command remains atomic and fail-closed:

```bash
python3 scripts/lc3_pcm_calibrate.py --output ABSOLUTE_NEW_FILE.json
```

Successful schema-3 evidence still has 38 records. Corrected Mode A records
must report 101 frames and 36360 samples each. All eight stateful-valid records
must pass frozen policy. Existing four mutations must still return `max-error`.
No original LC3 or portable PCM corpus byte may change.

## Scope

Modify only:

- this handoff, for implementation facts before commit;
- `docs/development/portable-lc3-pcm-oracle-plan.md`;
- `tests/support/lc3_stateful_recipes.c`;
- `tests/fixtures/lc3/stateful-reference-manifest.json`;
- `tests/fixtures/lc3/generate_stateful_references.sh`;
- `tests/fixtures/lc3/README.md`;
- `scripts/lc3_pcm_calibrate.py`;
- `tests/unit/lc3_pcm_calibrate/test_lc3_pcm_calibrate.py`;
- `tests/calibration/lc3_pcm_oracle/CMakeLists.txt`;
- `tests/calibration/lc3_pcm_oracle/src/main.c`.

Add only:

- `tests/fixtures/lc3/stateful_48k_7p5ms_modea_start_r.pcm`.

`tests/support/lc3_stateful_recipes.h`,
`tests/fixtures/lc3/gen_stateful_references.c`, and ARM build-info template
should not need changes. Touch them only if a verified compile or test failure
proves the generic API insufficient, then stop and escalate before expanding
scope.

## Non-scope

- No P2 receiver, observer, sink, parser, scenario, runner, or BSim CMake edits.
- No production code or production decoder behavior change.
- No fixture source LC3 or portable PCM changes.
- No portable manifest or frozen-limit changes.
- No threshold widening, record exclusion, decoded hash fallback, or controller
  sequence identity.
- No change to Stage 1 send counts, transport pins, totals, scenario count,
  lifecycle behavior, or accepted one-CIS-loss placement.
- No P3 decoder-test migration.
- No workflow, release, HIL, or product-status change.
- No push, PR, tag, amend, force operation, stash, reset, or cleanup.

## Exact implementation decisions

### Recipe table

In `tests/support/lc3_stateful_recipes.c`:

1. Remove both `start13_7p5ms_*` definitions and expected-validator entries.
2. Add `modea_start_7p5ms_l` and `modea_start_7p5ms_r` in the same positions,
   preserving eight total recipes and all other order.
3. Give left and right separate step arrays. Do not share a synthetic common
   prefix.
4. Pin the exact action, valid, geometry, reference kind, and reference path
   values from the table above in both live and expected tables.
5. Keep every existing generic validation rule.

### Stateful manifest and generator

Keep `stateful-reference-manifest.json` schema 1 and exactly eight recipes.
Replace entries five and six with the exact corrected recipes. Left remains a
portable reference with authoritative full-file size/hash and `frame_count=101`.
Right uses generated file `stateful_48k_7p5ms_modea_start_r.pcm`,
`frame_count=101`, and size 72720. Record its final lowercase SHA-256.

Update `generate_stateful_references.sh` exact expected recipe table and
generated-file list. Its default mode must verify all three generated files.
Its explicit `--rebase-stateful` transaction must support adding this new file
without weakening safety:

- existing destinations retain mode and rollback backup behavior;
- a missing destination is allowed only as a new manifest-declared generated
  file during explicit rebase;
- new file uses normal non-executable file permissions;
- failure after committing a new file removes that file during rollback;
- no partial old/new set survives failure;
- default strict mode never creates or rewrites checked-in files.

Extend focused generator tests for successful new-file rebase and rollback of a
newly created destination. Do not reduce existing source-integrity, hash,
unknown-mode, mismatch, or no-partial-copy coverage.

Bootstrap new trace through explicit rebase with a temporary valid 64-character
placeholder hash in the edited manifest if needed. After generation, replace
placeholder with actual hash, then run default strict verification. No
placeholder may remain in the committed diff.

### Host calibration

Update `scripts/lc3_pcm_calibrate.py` exact recipe authority only. Keep:

- schema version 3;
- 38 records;
- original 26-record prefix;
- eight stateful-valid positions;
- four mutation identities/order/evaluations;
- atomic output, no overwrite, bounded process output, provenance, and strict
  manifest/hash validation.

Corrected left/right records each use 101 frames and 36360 samples. Generated
right trace must be covered by stateful reference hashes and calibration-input
provenance through existing generic paths.

### ARM calibration

Update ARM CMake exact manifest authority, generated reference list,
configure dependencies, and generated include list. Embed the new 72720-byte
trace. Update `main.c` with:

- one generated byte array and include;
- exact 72720-byte static assertion;
- exact reference-path binding in `reference_for_recipe()`.

Keep `METRIC_RECORD_COUNT=38` and `PB031_ARM_PASS metrics=38`. Keep static
bounded memory, 8192-byte main stack, thread analyzer, warning policy, and all
existing provenance fields. Do not alter production partitions.

### Documentation

Update fixture README active truth:

- replace old symmetric recipes with exact asymmetric histories;
- explain source-valid startup transients and right post-valid PLC history;
- list three generated traces, individual sizes/hashes, and new total size;
- update transaction wording from two traces to three traces;
- retain 38-record ARM protocol.

Update parent plan status to mark P0d corrective calibration in progress and P2
blocked on cross-platform P0d acceptance. Add concise measured trace correction
near P0c evidence. Preserve P0c handoff as immutable historical evidence at its
accepted commit.

## Focused tests

Tests must prove:

1. Exact eight-recipe order and replacement IDs.
2. Left recipe expands to PLC 0..11 then corpus 0..100.
3. Right recipe expands to PLC 0..9, corpus 0, PLC twice, corpus 1..100.
4. Both have 113 actions, 101 valid, 12 PLC.
5. Right generated file has exactly 72720 bytes and manifest-owned hash.
6. Default generator verifies all three checked-in generated traces without
   writing them.
7. Explicit rebase can add a missing manifest-declared generated file
   transactionally and rolls it back on later failure.
8. Host protocol remains exactly 38 records; corrected records have 101 frames
   and 36360 samples.
9. All stateful-valid records pass; all existing mutations remain `max-error`.
10. ARM configure rejects any recipe/reference/hash mutation and embeds the new
    trace through exact size assertion.
11. Original LC3 and portable PCM corpus files remain byte-identical.

## Executor verification

Run before commit:

```bash
bash tests/fixtures/lc3/generate_stateful_references.sh
git diff --exit-code -- tests/fixtures/lc3/bsim_*.lc3 tests/fixtures/lc3/bsim_*.pcm
python3 -m unittest discover -s tests/unit/lc3_pcm_calibrate -p 'test_*.py' -v
nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d /tmp/opencode/pb031-p0d-pcm-oracle \
  tests/unit/pcm_oracle -p -t run
nix develop -c west build --no-sysbuild \
  -b xiao_nrf54l15/nrf54l15/cpuapp \
  -d /tmp/opencode/pb031-p0d-arm-calibration \
  tests/calibration/lc3_pcm_oracle -p
nix develop -c backlog doctor
```

Also run host calibration twice to two new external absolute paths and compare
record identity/order plus repeat metrics. Dirty repository provenance is
expected because preserved P2 work remains, so these local runs prove protocol
behavior only. Reviewed acceptance uses a clean detached worktree after commit.

If known Nix flake evaluation failure blocks a command, record exact failure and
use only the already-established equivalent pinned NCS toolchain environment.
Do not edit `flake.nix`.

## Commit

After all local checks pass, inspect `git status`, full scoped diff, and recent
log. Stage only P0d files listed in Scope. Confirm no P2-dirty file is staged.
Commit:

```text
test: correct Mode A 7.5 ms oracle recipe
```

Do not commit if the generated right trace exceeds frozen limits, a mutation
passes, any original corpus byte changes, any warning is unexplained, ARM image
exceeds full RRAM, bounded memory is unclear, or P2/production edits become
necessary.

Return changed files, new trace size/hash, exact recipe expansions, test/build
and local calibration results, ARM resource use, commit hash/message, preserved
P2 worktree status, warnings, deviations, and blockers. Do not push, amend,
open PR, or add attribution.

## Escalation

Stop without commit and preserve all work if:

- two materially different fixes fail;
- corrected valid output exceeds frozen limits on local AMD;
- any mutation passes or needs threshold widening;
- diagnostic trace cannot be represented by exact recipe actions;
- new generated trace cannot be made transactional without weakening safety;
- full ARM image does not fit or bounded memory is unclear;
- P2 or production edits appear necessary.

Report exact blocker, attempts, logs, diff, status, one precise question, and
smallest next-step hypothesis. Never repin source corpus, exclude source-valid
output, widen policy, or invent another architecture.

## Implementation facts (2026-09-17)

- Recipe positions five and six are `modea_start_7p5ms_l` and
  `modea_start_7p5ms_r`. Left replays PLC 12 then corpus 0 through 100. Right
  replays PLC 10, corpus 0, PLC 2, then corpus 1 through 100. Both have 113
  actions, 101 source-valid outputs, and 12 PLC actions.
- `stateful_48k_7p5ms_modea_start_r.pcm` is 72720 bytes with SHA-256
  `d76724f3392321a4ce959a00867ae40d82bcb93854099ec5d9abc8c01239d858`.
- Explicit rebase permits this one missing manifest-declared generated trace,
  preserves existing destination modes and backups, assigns new file mode
  `0644`, and removes it if a later transaction step fails. Strict mode only
  verifies temporary candidates and never writes checked-in traces.
- Local AMD schema-3 repeats have 38 identical metric records. All eight
  stateful-valid records pass; all four mutations evaluate `max-error`. At
  implementation completion, formal Intel and ARM execution acceptance remained
  orchestrator work; completed evidence follows below.

## Completed cross-platform acceptance (2026-09-17)

P0d is accepted at reviewed commit
`4fbe9bc135d9077aff90a56f0f6f70fe92637ebb`. Evidence is external and is not
checked into this repository:

- root: `/tmp/opencode/pb031-p0d-acceptance-4fbe9bc`;
- authoritative report:
  `/tmp/opencode/pb031-p0d-acceptance-4fbe9bc/acceptance-report.md`.

Clean detached review-tree checks passed, and main-worktree dirty P2 status
preservation passed. Strict stateful generation, original LC3/PCM corpus
no-diff, 37 Python tests, native `pcm_oracle` 12/12, pristine nRF54L15
calibration build, `backlog doctor`, and `git diff --check` all passed.

Two schema-3 reports per environment contained exactly 38 metric records.
Record identity and order matched across AMD, Intel, and ARM; repeat metrics
matched within each environment.

| Environment | Toolchain | Mode A 7.5 ms left, max/RMS/correlation Q15 | Mode A 7.5 ms right, max/RMS/correlation Q15 |
| --- | --- | --- | --- |
| AMD Ryzen 9 5950X | GCC 14.3.0 | 0 / 0 / 32767 | 0 / 0 / 32767 |
| Intel Core i3-6100U | Clang 21.1.8 | 1902 / 424 / 32761 | 1883 / 426 / 32760 |
| nRF54L15 ARM | Zephyr SDK 0.17.0, GCC 12.2.0 | 1 / 1 / 32767 | 1 / 1 / 32767 |

Across all eight stateful-valid records, maximum absolute error was 1977,
maximum RMS error was 426, and minimum correlation was Q15 32756, inside the
frozen `2048 / 512 / 32750` envelope. All eight stateful-valid records passed;
all four stateful mutations returned `max-error`.

ARM calibration identity: CMSIS-DAP `8EE9B3FF`; DPIDR `0x6ba02477`; AP0/AP1
`0x84770001`; AP2 `0x32880000`; PART `0x00054b15`; VARIANT `0x41414330`.
Calibration image verification covered 845316 bytes. Full RRAM use was
845316/1462272 bytes, with 616956 bytes headroom. RAM use was 27808/192512
bytes, with 164704 bytes headroom. Main stack was 2920/8192 bytes, 35 percent,
in both runs.

Production was restored from clean reviewed source. Cpuapp SHA-256 was
`a3700e9314814e60b48ecf539d28c31e635d61f9bb29bbe43410506c8fba0c8e`; FLPR
SHA-256 was
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Verified flash bytes were 536948 for cpuapp and 32604 for FLPR. Boot banner
was `4fbe9bc135d9`; required BLE, settings, timing, I2S, FLPR, and advertising
markers were present, with no UART warning or error lines.

Only documented production-build diagnostics occurred: `No SOURCES given to
Zephyr library: drivers__watchdog` and `__ASSERT() statements are globally
ENABLED`. No unexplained warning occurred. P0d unblocks P2.
