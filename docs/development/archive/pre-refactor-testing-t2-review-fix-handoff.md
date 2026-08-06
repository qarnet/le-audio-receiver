# Phase T2 review-fix handoff

## Goal

Close portability/safety defects found during orchestrator review and make gate
evidence complete. T2 remains open until exact final commit passes focused
tests, full gate, and all builds.

## Scope

- `src/audio_decode.{c,h}`
- `src/audio_volume.{c,h}`
- `tests/unit/decode/`
- `tests/unit/volume/`
- `tests/fixtures/lc3/`
- root `.gitattributes`
- `docs/testing/t2-audio-pipeline-tests.md`
- `STATUS.md`
- this handoff

No T3 work, hardware, push, merge, PR, or amend.

## Fix 1 — validate size_t before narrowing

Current `audio_decode_sdu()` computes:

```c
int octets_per_channel = (int)(frame_len / ...);
```

before validating `frame_len`. This contradicts evidence claiming bounds are
checked before narrowing and permits implementation-defined wrap for inputs
larger than `INT_MAX`.

Required:

- keep all length arithmetic in `size_t` until bounds/divisibility pass;
- reject `frame_len > INT_MAX` for valid and PLC calls before cast;
- reject odd Mode B length before division, including PLC calls;
- valid data: enforce per-channel 20..400 bytes;
- PLC: preserve zero-length support required by BSim; if nonzero, require a
  divisible per-channel shape in 20..400 bytes;
- cast to `int` only after these checks;
- all rejection must preserve output and decoder state.

Tests must include `SIZE_MAX`, `(size_t)INT_MAX + 1`, odd Mode B PLC length,
nonzero too-short/too-long PLC lengths, and zero-length mono/Mode B PLC success.

Update public decoder header with exact supported config, PLC length semantics,
output capacity requirement, and `-EINVAL`/`-EBADMSG` outcomes.

## Fix 2 — null/zero volume safety on every state

Current `audio_volume_apply(NULL, 0)` happens to survive only when volume is
nonzero and unmuted. Mute or volume zero takes `memset(NULL, 0)`, which is not a
portable C guarantee.

Add early no-data exit after performance timing starts:

```c
if (buf == NULL || samples == 0) {
    audio_perf_cycle_end(...);
    return;
}
```

This also makes null/nonzero safe and deterministic. Document it. Test null
with zero and nonzero samples under muted, volume-zero, unity, and intermediate
states. Assert one balanced performance sample per call.

## Fix 3 — fixture generator defined behavior

Current source formulas multiply signed `int` by large constants before cast;
left formula has signed overflow. Right-shifting negative `int16_t` while
writing LE is implementation-defined.

Required:

- convert index to `uint32_t` before multiplication;
- use `UINT32_C(...)` constants and unsigned wrap explicitly;
- convert sample to `uint16_t` before shifting to write little-endian;
- avoid a fixed generator executable path; use `mktemp` + cleanup trap;
- run generator twice from clean copies and prove checked-in fixture hashes are
  unchanged or deliberately update binaries/hashes/tests if defined arithmetic
  changes them;
- add root `.gitattributes` entries marking `tests/fixtures/lc3/*.lc3` and
  `*.pcm` binary.

`-Wno-array-bounds` currently suppresses a warning without recorded reason.
First try generation without it. If installed liblc3 itself triggers a known
false positive, retain suppression only with an adjacent comment naming exact
compiler diagnostic, source file, and why repo generator cannot fix third-party
source. Generator project code must compile warning-free.

## Fix 4 — complete transient gate evidence

Executor recap reported one workstation run with both `exec: offload_asrc` and
`bsim: stage1` failing because generated `build.ninja` files were truncated.
Committed STATUS omits this known run.

Recover and record exact available diagnostics: command context, Ninja parse
message/line if retained, affected build paths, whether gate runs overlapped,
and why source code cannot truncate generated build files. Do not claim it is
the same as T1's unidentified failure; no evidence supports that.

If logs are unavailable, state exactly which evidence was lost and make no root
cause claim beyond observed truncated generated files. Run final exact-commit
full gate three consecutive times, serially, on workstation. Any recurrence
blocks T2 and requires diagnosis. Keep all three logs until review completes,
then preserve relevant diagnostic text in STATUS/T2 evidence before cleanup.

## Verification

```bash
bash tests/fixtures/lc3/generate.sh
git diff -- tests/fixtures/lc3/*.lc3 tests/fixtures/lc3/*.pcm
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t2_fix_decode tests/unit/decode -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t2_fix_volume tests/unit/volume -p -t run
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Validate exact final commit on workstation by bundle + detached worktree. Run
full gate three consecutive times serially and all three builds. BSim hashes
must remain the corrected T2 values unless regenerated fixture changes have no
relation to BSim and therefore must not alter them. Remove temporary refs,
worktrees, bundles, generator binaries, and build dirs after evidence capture.

## Commit

Create new commit, no amend:

```text
fix: harden audio test boundaries
```

Return exact fixture hash comparison, focused counts, three full gate results,
build results, full transient evidence/disposition, commit hash, cleanup,
deviations, and blockers.
