# PB-031 P0c handoff: payload identity and PLC-aware reference calibration

Status: Reviewed and accepted 2026-09-17. P2 is unblocked.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

Starting repository HEAD:
`c90f9f5d650905dc92d98161dbc153202072270f`.

## Why P0c exists

The first P2 attempt disproved two load-bearing assumptions:

1. Receiver `bt_iso_recv_info.seq_num` is receiver-controller timeline state,
   not sender fixture identity. It cannot select a fixture frame reliably.
2. LC3 output after PLC depends on decoder history. Comparing a valid frame
   decoded after startup or midstream PLC against the lossless per-frame PCM
   corpus can exceed the frozen portable limits even when decoder behavior is
   correct.

P0c adds exact payload-derived fixture identity and checked-in stateful PCM
references generated from the decoder histories already pinned by Stage 1.
It calibrates those references on Intel, AMD, and ARM before P2 consumes them.

## Worktree warning

The main worktree contains an uncommitted, knowingly failing first P2 attempt.
Do not edit, stage, discard, reset, stash, or clean those files. P0c changes are
deliberately isolated from them. Stage only files named in this handoff.

Existing P2-dirty or untracked paths that must remain untouched include:

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
- `tests/unit/audio_stream_session/`
- `tests/unit/bsim_runner/test_bsim_stage1_parse.py`
- `tests/bsim/src/bsim_pcm_limits.h.in`
- `docs/development/portable-lc3-pcm-oracle-p2-handoff.md`

## Goal

Add one strict, reproducible recipe/reference corpus whose recipes model:

- 10 ms startup with eight PLC actions;
- 7.5 ms mono/Mode B startup with eleven PLC actions;
- 7.5 ms Mode A startup with thirteen PLC actions;
- malformed sequence 20 rejected before decode, followed by fixture sequence
  21 without advancing decoder state for sequence 20;
- 10 ms Mode A right-channel loss after 48 valid frames, with eighteen PLC
  actions before fixture sequence 48 resumes.

Prove every source LC3 frame has unique payload identity within its stream and
that left/right streams of the same duration have no identical frame. Extend
host and ARM calibration from 26 to 38 records. Keep frozen limits unchanged:

```text
max_abs_error=2048
max_rms_error=512
min_correlation_q15=32750
```

## User-observable behavior proved

Calibration command still writes one atomic evidence file or no file on any
failure:

```bash
python3 scripts/lc3_pcm_calibrate.py --output ABSOLUTE_NEW_FILE.json
```

Successful schema-3 evidence proves:

- original 26 records retain exact order and expected evaluations;
- eight PLC-aware valid recipes pass frozen policy;
- four recipe mutations fail `max-error`;
- fixture payload identity is unique and channel-disjoint as specified;
- source corpus, stateful traces, policy, compiler inputs, and repository state
  have bounded SHA-256 provenance.

## Scope

Modify only:

- `docs/development/portable-lc3-pcm-oracle-plan.md`
- this handoff, only for implementation facts or final reviewed result
- `tests/fixtures/lc3/README.md`
- `scripts/lc3_pcm_calibrate.py`
- `tests/fixtures/lc3/calibrate.c`
- `tests/unit/lc3_pcm_calibrate/test_lc3_pcm_calibrate.py`
- `tests/calibration/lc3_pcm_oracle/CMakeLists.txt`
- `tests/calibration/lc3_pcm_oracle/src/pb031_arm_build_info.h.in`
- `tests/calibration/lc3_pcm_oracle/src/main.c`
- PB-031 `Implementation Notes` only

Add only:

- `tests/support/lc3_stateful_recipes.h`
- `tests/support/lc3_stateful_recipes.c`
- `tests/fixtures/lc3/gen_stateful_references.c`
- `tests/fixtures/lc3/generate_stateful_references.sh`
- `tests/fixtures/lc3/stateful-reference-manifest.json`
- two generated stateful PCM files named in the recipe table below

## Non-scope

- No P2 receiver, observer, sink, parser, scenario, runner, or BSim CMake work.
- No production code or production decoder behavior change.
- No controller selection, devicetree, Kconfig, or Bluetooth change.
- No change to `portable-oracle-manifest.json`, existing LC3 files, existing PCM
  files, fixture hashes, transport hashes, send counts, scenario counts, PLC
  counts, or lifecycle behavior.
- No threshold widening or copied threshold constants.
- No P3 real-decoder migration.
- No permanent multi-architecture CI.
- No hardware action in Executor session.
- No push, PR, tag, amend, force operation, stash, reset, or cleanup.

## Grounding evidence

- NCS v3.3.0 `bt_bap_stream_send()` forwards host PSN, but receiver
  `bt_iso_recv_info.seq_num` is populated from receiver controller ISOAL/HCI
  output. PSN is not a sender fixture identity carried end-to-end over CIS.
- Current generated BSim netcore config selects SDC. Changing controller is not
  a source-identity fix, so P0c and later P2 must not depend on controller PSN.
- Every existing 128-frame stream has 128 distinct LC3 byte strings. Same-
  duration left/right streams have zero identical-frame overlap.
- Local liblc3 probes show wrong lossless indexing after startup PLC reaches
  maximum error around 25k. Malformed-history and one-CIS-loss wrong recipes
  reach maximum error around 38k and 26k. Correct startup PLC followed by
  fixture sequence zero is exact on local AMD.
- Installed liblc3 v1.1.2 source and first P0c implementation evidence show
  fresh-decoder PLC produces silence and the first valid frame calls
  `lc3_plc_suspend()`. Therefore seven, eight, eleven, or thirteen PLC actions
  before the first valid frame produce identical later valid PCM. Startup PLC
  count remains an exact recipe/lifecycle contract, but it cannot be a
  numerical mutation control. Normal startup recipes reference the existing
  portable PCM directly instead of checking in duplicate bytes.
- Stage 1 exact contracts pin startup histories through totals:
  - 10 ms mono/Mode A/Mode B: eight startup PLC actions before 100 accepted
    pushes;
  - 7.5 ms mono/Mode B: eleven startup PLC actions;
  - 7.5 ms Mode A: thirteen startup PLC actions;
  - malformed case: sequence 20 is rejected before liblc3, so decoder state does
    not advance for it;
  - one-CIS loss: right channel has 48 valid frames, eighteen PLC actions, then
    resumes at fixture sequence 48. Existing exact contract is
    `BSIM_MODEA_LOSS_COUNT == 18`.
- P0b accepted limits and six cross-platform reports are recorded in
  `docs/development/portable-lc3-pcm-oracle-p0b-threshold-handoff.md`.

## Exact recipe API

Create a dependency-free test-support recipe table in
`tests/support/lc3_stateful_recipes.h/.c`. It may include only standard integer,
boolean, and size headers. Use these public shapes and names:

```c
enum lc3_stateful_action {
	LC3_STATEFUL_ACTION_PLC = 0,
	LC3_STATEFUL_ACTION_CORPUS = 1,
};

enum lc3_stateful_reference_kind {
	LC3_STATEFUL_REFERENCE_PORTABLE_PCM = 0,
	LC3_STATEFUL_REFERENCE_GENERATED_PCM = 1,
};

struct lc3_stateful_step {
	enum lc3_stateful_action action;
	uint16_t first_sequence;
	uint16_t count;
};

struct lc3_stateful_recipe {
	const char *id;
	const char *source_stem;
	const char *reference_path;
	enum lc3_stateful_reference_kind reference_kind;
	uint16_t reference_first_frame;
	uint32_t duration_us;
	uint16_t frame_bytes;
	uint16_t samples_per_frame;
	uint16_t output_action_count;
	uint16_t valid_frame_count;
	const struct lc3_stateful_step *steps;
	size_t step_count;
};

extern const struct lc3_stateful_recipe lc3_stateful_recipes[];
extern const size_t lc3_stateful_recipe_count;
```

`first_sequence` must be zero for PLC steps and is ignored there. Add a pure
validator function that rejects null strings/tables, unknown actions, zero
counts, corpus ranges outside 0..127, integer overflow, geometry outside the
four existing source streams, mismatched `output_action_count`, and mismatched
`valid_frame_count`. Generator, host calibrator, and ARM image must call it
before replay.

Define recipes in this exact order. Portable references point into existing
lossless PCM and are never copied. Generated references contain only
source-valid outputs after state-changing midstream history.

| ID | Source | Steps | Output actions | Valid frames | Reference |
| --- | --- | --- | ---: | ---: | --- |
| `start8_10ms_l` | `bsim_48k_10ms_120b_l` | PLC 8; corpus 0 count 100 | 108 | 100 | portable `bsim_48k_10ms_120b_l.pcm`, first frame 0 |
| `start8_10ms_r` | `bsim_48k_10ms_120b_r` | PLC 8; corpus 0 count 100 | 108 | 100 | portable `bsim_48k_10ms_120b_r.pcm`, first frame 0 |
| `start11_7p5ms_l` | `bsim_48k_7p5ms_90b_l` | PLC 11; corpus 0 count 100 | 111 | 100 | portable `bsim_48k_7p5ms_90b_l.pcm`, first frame 0 |
| `start11_7p5ms_r` | `bsim_48k_7p5ms_90b_r` | PLC 11; corpus 0 count 100 | 111 | 100 | portable `bsim_48k_7p5ms_90b_r.pcm`, first frame 0 |
| `start13_7p5ms_l` | `bsim_48k_7p5ms_90b_l` | PLC 13; corpus 0 count 100 | 113 | 100 | portable `bsim_48k_7p5ms_90b_l.pcm`, first frame 0 |
| `start13_7p5ms_r` | `bsim_48k_7p5ms_90b_r` | PLC 13; corpus 0 count 100 | 113 | 100 | portable `bsim_48k_7p5ms_90b_r.pcm`, first frame 0 |
| `skip20_10ms_l` | `bsim_48k_10ms_120b_l` | PLC 8; corpus 0 count 20; corpus 21 count 80 | 108 | 100 | generated `stateful_48k_10ms_skip20_l.pcm`, first frame 0 |
| `loss48x18_10ms_r` | `bsim_48k_10ms_120b_r` | PLC 8; corpus 0 count 48; PLC 18; corpus 48 count 34 | 108 | 82 | generated `stateful_48k_10ms_loss48x18_r.pcm`, first frame 0 |

Generated reference files contain little-endian PCM only for `CORPUS` actions,
in recipe order. PLC outputs are deliberately not stored or accepted
numerically. PLC actions still advance decoder state. Exact total new binary
size is 174720 bytes: 96000 bytes for skip20 and 78720 bytes for right loss.

## Stateful manifest and generation

Keep `portable-oracle-manifest.json` at schema 2 as sole numerical-policy
source. Add separate `stateful-reference-manifest.json` schema 1. It must
strictly contain:

- schema version, NCS version, liblc3 semantic label/revision, generator flags;
- source portable-manifest path, size, and SHA-256;
- exact ordered recipe metadata matching support table;
- each reference kind/path, first frame, compared frame count, exact backing
  file size, and lowercase SHA-256. Portable reference hashes must equal their
  authoritative entries in the source portable manifest.

Unknown or missing fields, booleans where integers are required, unsafe paths,
wrong order, geometry, recipe, size, hash, version, revision, or flags fail.
Do not duplicate `pcm_limits` in stateful manifest.

`gen_stateful_references.c` reads existing checked-in LC3 corpus from a supplied
directory. For each generated-reference recipe it creates a fresh decoder,
executes every step in order, requires PLC decode return `1`, requires corpus
decode return `0`, and writes only corpus-action outputs as explicit
little-endian samples. It validates but does not generate portable-reference
recipes. It never writes source LC3 or lossless PCM.

`generate_stateful_references.sh` follows existing generator safety:

- callable from any directory;
- exact NCS v3.3.0/liblc3 pin and exact warning-free flags;
- temporary binary/output with EXIT cleanup;
- validates portable source manifest and all source hashes before compile;
- default mode verifies generated bytes equal checked-in stateful hashes before
  copying anything;
- `--rebase-stateful` permits generated stateful hash differences only after
  checked-in source and checked-in stateful manifest validation, copies only the
  two generated stateful files, and prints a loud manifest/README update
  reminder;
- no mode permits source corpus mismatch or edits any manifest;
- unknown arguments exit 2 with usage.

## Payload identity proof

Strict Python validation must split each LC3 file by manifest frame geometry
and prove:

- exactly 128 frames per stream;
- 128 unique byte strings per stream;
- zero byte-identical overlap between left/right streams of the same duration;
- every recipe corpus range resolves inside its declared source stream.

Report exact counts in schema-3 evidence under `payload_identity`. Any failure
occurs before compiler invocation and leaves no output file. Do not use hashes
alone as payload lookup identity; exact frame bytes are authoritative.

## Host calibration protocol

Preserve first 26 records byte-shape/order. Add records 27 through 34 as
`stateful-valid`, one per recipe table order. Replay each recipe through one
fresh decoder. Accumulate only corpus-action outputs against next stored trace
frame. Every record must evaluate `pass` and use:

- `stem`: recipe ID;
- `reference_stem`: reference path;
- `frames`: recipe `valid_frame_count`;
- `samples`: valid frames times samples per frame.

Add four exact mutation records:

35. `stateful-payload-off-by-one`: replay eight PLC actions plus corpus 1 count
    100 from 10 ms left; compare its 100 valid outputs with `start8_10ms_l`;
    require `max-error`. This exercises payload identity/order. Startup PLC
    count is not a numerical mutation because fresh-start PLC is silent and
    reset by first valid decode in pinned liblc3.
36. `stateful-skip-ignored`: replay normal start8 plus corpus 0 count 100;
    compare with `skip20_10ms_l`; require `max-error`.
37. `stateful-loss-burst-omitted`: replay start8 plus corpus 0 count 82;
    compare with valid frames from `loss48x18_10ms_r`; require `max-error`.
38. `stateful-wrong-channel`: replay `start8_10ms_r`; compare with
    `start8_10ms_l`; require `max-error`.

For mutation records, use descriptive actual `stem` values fixed in Python
expected-record data and recipe reference path as `reference_stem`. No direct
metric construction. Run decoder output through
`pcm_oracle_accumulate/finalize` and manifest-owned policy through
`pcm_oracle_evaluate`.

Bump evidence report to schema 3. Add stateful manifest SHA-256, ordered
reference hash records, and payload identity. Include recipe support C/header
and stateful manifest in ordered calibration-input provenance. Keep atomic no-overwrite,
bounded raw output, bounded report, compiler diagnostics, stderr rejection,
CPU/compiler/NCS/liblc3/repository provenance, and exact evaluation
recomputation.

## ARM calibration protocol

Extend same image to emit same 38 records in same order and identity. Embed two
new generated references, existing portable PCM references, and existing LC3
inputs. Configure stateful manifest SHA-256 and support-source SHA-256 into
generated build info. Keep policy read only from portable manifest. Use static
bounded buffers, preserve 8192-byte main stack, thread analyzer, FPU/liblc3
settings, and no Bluetooth subsystem.

Set PASS marker to:

```text
PB031_ARM_PASS metrics=38
```

Direct calibration build has `CONFIG_USE_DT_CODE_PARTITION=n`; full 1428 KiB
RRAM is actual linker capacity. Added generated-reference data is about 175 KiB.
Report exact FLASH, RAM, stack use, and both full-RRAM and production-slot0
headroom. Exceeding full RRAM, losing stack headroom, or adding dynamic
allocation is a stop condition. Do not alter production partitions.

## Focused tests

Extend public-boundary Python tests to prove:

- exact recipe table order, geometry, action counts, valid counts, reference
  kinds, ranges, and sizes;
- malformed/unknown/missing stateful manifest fields fail before compile and
  create no evidence;
- source manifest/hash or reference hash mismatch fails closed;
- payload uniqueness/disjointness failure is detected;
- generator unknown mode, strict mismatch, rebase source protection, and no
  partial-copy behavior;
- report schema 3 contains exact stateful provenance and payload identity;
- calibrator requires exact 38-record count/order/identity/evaluation;
- each stateful-valid record must pass and four recipe mutations must produce
  `max-error`;
- all current atomic output, policy, timestamp, repository provenance, fixture
  integrity, and compiler diagnostic tests remain green.

## Documentation corrections

Update parent plan and PB-031 Implementation Notes with facts only:

- receiver controller sequence is diagnostic, not source identity;
- P2 becomes payload-and-recipe-aware, not controller-sequence-aware;
- valid exact payload bytes select fixture sequence;
- stateful recipe cursor advances for PLC and valid decode actions, while only
  source-valid outputs enter numerical metrics;
- malformed exact-shape rejection creates no decoder action;
- P0c cross-platform calibration is mandatory before P2 resumes.

Do not change PB title, status, priority, type, Description, acceptance criteria,
or product-owned plan section. Append only Implementation Notes.

## Executor verification

Run before commit:

```bash
bash tests/fixtures/lc3/generate_stateful_references.sh
git diff --exit-code -- tests/fixtures/lc3/bsim_*.lc3 tests/fixtures/lc3/bsim_*.pcm
python3 -m unittest discover -s tests/unit/lc3_pcm_calibrate -p 'test_*.py' -v
nix develop -c env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d /tmp/opencode/pb031-p0c-pcm-oracle \
  tests/unit/pcm_oracle -p -t run
nix develop -c west build --no-sysbuild \
  -b xiao_nrf54l15/nrf54l15/cpuapp \
  -d /tmp/opencode/pb031-p0c-arm-calibration \
  tests/calibration/lc3_pcm_oracle -p
nix develop -c backlog doctor
git diff --check
```

Also run host calibration once to a new external path. Dirty repository
provenance is expected in Executor session because preserved P2 work remains;
this run proves local protocol behavior only, not accepted cross-platform P0c
evidence.

If known flake evaluation failure blocks Nix, record exact failure and use only
already-established equivalent dev-shell command. Do not edit `flake.nix`.

## Commit

After checks pass, inspect `git status`, full scoped diff, and recent log. Stage
only P0c files listed here. Confirm no existing P2-dirty file is staged. Commit:

```text
test: add PLC-aware PCM calibration traces
```

Do not commit if any trace valid record fails, any recipe control does not
return exact expected result, any existing corpus byte changes, any warning is
unexplained, ARM image exceeds full RRAM, or implementation needs P2/production
changes.

Return files changed, exact generated-reference hashes/sizes,
test/build/calibration results,
ARM resource use, commit hash/message, preserved worktree status, warnings,
deviations, and blockers. Do not push, amend, open PR, or add attribution.

## Escalation

Stop without commit and preserve all work if:

- two materially different fixes fail;
- valid stateful output exceeds frozen limits on local AMD;
- a mutation passes or needs threshold widening;
- recipe evidence conflicts with Stage 1 contracts;
- source payload identity is not unique/disjoint;
- full ARM image does not fit or bounded memory is unclear;
- P2 or production edits appear necessary.

Report exact blocker, attempts, logs, diff, status, one precise question, and
smallest next-step hypothesis. Do not weaken policy, repin existing corpus,
exclude source-valid output, or invent another architecture.

## Post-implementation orchestrator gate

Executor implementation is not P0c acceptance. Orchestrator will review commit
and actual diff, create a clean detached worktree at that commit, then capture:

1. two AMD schema-3 reports;
2. two Intel schema-3 reports on identified Core i3-6100U host;
3. two ARM 38-record UART reports after fresh target identity and exact image
   flash;
4. cross-platform equality of record identity/order, per-environment repeat
   metrics, all eight valid PASS results, and four exact mutation failures;
5. production nRF54L15 firmware restoration and boot evidence.

Only reviewed cross-platform PASS permits revised P2 handoff work.

## Final reviewed acceptance (2026-09-17)

P0c is reviewed and accepted at implementation commit
`262805eb51731ff7b2511e7e522762e4061bb270`
(`test: add PLC-aware PCM calibration traces`). Review used clean detached
worktree `/tmp/opencode/pb031-p0c-review-262805e`. P2 is unblocked.

### Review verification

- Strict stateful generator passed, and no original corpus diff was present.
- Python calibration suite passed 32/32.
- Native `pcm_oracle` passed 12/12.
- ARM calibration build passed.
- `backlog doctor` and `git diff --check` passed.
- Generated compile-command links were restored after review work. Detached
  worktree status then ended clean.

### Cross-platform schema-3 reports

AMD Ryzen 9 5950X with GCC 14.3.0:

- `/tmp/opencode/pb031-p0c-calibration-262805e/amd-run-1.json`, SHA-256
  `989da5425a69dc7a541a9bc3630d890ff0168be2e5bd8805118f03fff8d58d0e`
- `/tmp/opencode/pb031-p0c-calibration-262805e/amd-run-2.json`, SHA-256
  `1db0e14626ab4fc2ea4deab2333d327a6df8b883700342a51883c7b1f51bb124`

Intel Core i3-6100U with Clang 21.1.8:

- `/tmp/opencode/pb031-p0c-intel-262805e-i3-6100u/intel-run-1.json`, SHA-256
  `630c49794f64adfde3963cf2b0d0890bb37933ac8e7a68aa1e87cdeaa408b53b`
- `/tmp/opencode/pb031-p0c-intel-262805e-i3-6100u/intel-run-2.json`, SHA-256
  `68b0cf8fe82d84616754fb74d37201137c1e7e874a5d9a0c2e453f8180042b2c`

ARM UART reports:

- `/tmp/opencode/pb031-p0c-arm-262805e/arm-run-1.log`, SHA-256
  `c0ffc50baf7084f70c6c7b93f21d0e9aa3217595ffefad0fa54b19d8e4b1c8d2`
- `/tmp/opencode/pb031-p0c-arm-262805e/arm-run-2.log`, SHA-256
  `f9915a5b5c67d1a8a9cc1cfaeab2a7c83b11cfeb0fc25b011a47d3794b208457`

Both ARM reports carry `PB031_ARM_PASS metrics=38`.

### ARM identity, image, and resource evidence

Fresh ARM identity evidence names CMSIS-DAP serial `8EE9B3FF`, DPIDR
`0x6ba02477`, AP IDRs `0x84770001`, `0x84770001`, `0x32880000`, and
`0x00000000`, FICR PART `0x00054b15`, and VARIANT `0x41414330`. Each of
`/tmp/opencode/pb031-p0c-arm-262805e/identity-preflash.log`,
`/tmp/opencode/pb031-p0c-arm-262805e/identity-run-1.log`, and
`/tmp/opencode/pb031-p0c-arm-262805e/identity-run-2.log` has SHA-256
`8cb5e27fffc8fb0f985ea0fc076582d7555c1b2dd85ee30c1bee4e4c8dee1334`.

Exact calibration flash evidence is
`/tmp/opencode/pb031-p0c-arm-262805e/flash.log`, SHA-256
`a643d1c836d1f281ece9659016f43b709bba69a2be06aade706049f2fffab1c8`.
It records 772472 bytes downloaded and verified.

Exact-ELF `arm-zephyr-eabi-size` resource evidence is text 771132, data 1340,
and bss 26476. Flash payload is 772472 bytes and static RAM is 27816 bytes.
The full 1428 KiB RRAM headroom is 689800 bytes. The calibration image exceeds
the production 664 KiB slot0 by 92536 bytes, expected because the direct
calibration build uses `CONFIG_USE_DT_CODE_PARTITION=n`; production partitions
remain unchanged. Both runs report main stack usage 2920/8192 (35%), unused
5272, with no fault, FAIL, or error.

### Cross-platform verdict

Every environment has 38 records. Record identity and order are equal across
AMD, Intel, and ARM, and metrics are exact between two runs within each
environment. All eight stateful-valid records pass and all four exact stateful
mutations return `max-error`. Embedded ARM manifest, policy, and support hashes
match host provenance.

Stateful-valid max-abs/max-RMS/min-correlation envelopes are AMD
`0/0/32767`, Intel `1977/425/32756`, and ARM `1/1/32767`. Frozen limits remain
unchanged at `2048/512/32750`.

### Production restoration

Clean `262805e` cpuapp and FLPR were rebuilt and flashed. CPUAPP SHA-256 is
`33738ab087f87d42849d604e7d50032b52d4dc8ee691f060d898521cf4a44da7`; FLPR
SHA-256 is
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Flash evidence records 536948 cpuapp bytes and 32604 FLPR bytes downloaded and
verified. Boot evidence is
`/tmp/opencode/pb031-p0c-arm-262805e/production-boot.log`, SHA-256
`b6cd19ecd8e5af374c1b5e925cfccef9059c071c7c36c56b7a3e543562dfacff`.
It reached commit banner `262805eb5173`, `BLE ready`, identity,
`settings_load() OK`, audio timing and I2S ready, FLPR READY/rings/runtime,
and advertising. No boot warning or error occurred.

Production build diagnostics remain known diagnostics, not new defects: the
documented nRF54L15 watchdog no-sources CMake diagnostic and global
`__ASSERT()` CMake diagnostic. Production flash wrapper also logged transient
Nix eval-cache SQLite busy message as `error (ignored)` because reader and
flash entered Nix concurrently; flash verification and boot succeeded. Its
dirty-tree warning came only from build-generated tracked compile-command
symlink targets in detached worktree; those links were restored and final
detached status was clean.

**Verdict:** P0c accepted. P2 may resume. No threshold widening, corpus repin,
production behavior change, or acceptance-criterion completion is claimed.
