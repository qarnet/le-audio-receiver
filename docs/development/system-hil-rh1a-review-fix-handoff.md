# System HIL RH1A review-fix handoff

Status: focused correction handoff. RH1A remains open after initial
implementation reported 32 control and 20 signal tests passing.

## Goal

Close review defects in parser atomicity/strictness/target stack use, state
memory safety and invariants, real stage sequencing, scored PRNG semantics, and
generator contract. Preserve RH1A scope and wire constants.

## Review findings

1. `hil_source_parse()` writes `command`, IDs, and peer/config fields directly
   into caller output before all peer/config validation completes. A valid
   common header plus invalid peer/mode/profile/count/seed/reconnect value
   returns failure with partially mutated output, violating byte-atomic parser
   contract.
2. Parser places roughly 6 KiB of decoded string storage on stack (`str[512]`
   for every known key), unsuitable for target shell reuse. Maximum semantic
   string is 64 bytes.
3. Nested object/array skipping counts any opening/closing bracket equally and
   does not validate inner JSON grammar. Mismatched or malformed nested values
   can be classified as wrong type instead of syntax despite strict JSON claim.
4. State counter wrappers form `&st->counters[stream]` before stream index is
   validated. A large index forms an invalid pointer and violates memory-safe
   rejection contract.
5. `counter_submit_scored()` increments scored count but not total submitted
   count, allowing a passing run with scored submissions greater than total
   submissions.
6. `configure()` mutates configuration after terminal even though public
   contract says post-terminal snapshot remains immutable until reset or next
   successful start.
7. Signal stage can enter scored before exact preamble completion. Current
   scored and LC3 tests therefore exercise fresh encoder state, not actual
   preamble -> scored lifecycle.
8. Scored entry chooses first target from unadvanced derived PRNG, then advances
   only after first block. Frozen contract says one xorshift update per block;
   first block must consume first update.
9. Generator accepts `--write --check` together with write silently winning,
   and module doc says I/O error exits 2 while implementation returns 1.

## Required fixes

### Parser

- Build one zero-initialized local `struct hil_source_command_in candidate`.
  Perform every common, peer, and configure semantic validation against it.
  Assign `*out = candidate` exactly once after all checks pass.
- Add byte-snapshot failure tests for valid common headers followed by each
  invalid peer address/type and configure mode/profile/count/seed/reconnect
  value. Existing syntax/key/type failures remain covered.
- Reduce stack storage. Known decoded values need at most 65 bytes plus NUL;
  command/address strings are smaller. Do not keep one 512-byte array per key.
  A bounded parser context under 2 KiB is target. Add a compile-time assertion
  in native test that parser-private size is not exposed; instead expose and
  test a public `HIL_SOURCE_PARSER_STACK_BUDGET` constant of 2048 only if needed
  for documentation. Prefer an internal `BUILD_ASSERT`/`_Static_assert` on
  parser context size and no public implementation-size API.
- String lexer must consume overlong strings safely and return `range`; it must
  not turn bounded storage into syntax or write past buffer. Unknown key/value
  storage may be discard-only after lexical validation.
- Replace bracket-depth skipping with bounded recursive or explicit-stack JSON
  grammar validation for rejected nested object/array values. Enforce matching
  delimiters, object key/colon/value/comma rules, array value/comma rules,
  trailing-comma rejection, and a fixed nesting-depth limit that fails syntax.
  Values still return `wrong_type` only when nested JSON itself is valid.
- Add malformed nested tests: mismatched delimiters, missing value/colon/comma,
  nested trailing comma, and depth overflow. Valid object/array in scalar field
  remains `wrong_type`.

### State

- Validate active/configuration/stream index before selecting counter field.
  Never form an out-of-range pointer. Add `UINT32_MAX` index tests with canary
  bytes around state and exact unchanged assertion.
- `hil_source_state_counter_submit_scored()` is one scored SDU submission: it
  atomically increments both `submitted_sdus` and
  `submitted_scored_sdus`. Check both for overflow before changing either.
  `counter_submit()` remains for non-scored preamble/tail submissions. Update
  tests and documentation; prove pass snapshots always have total submitted >=
  scored submitted.
- Reject configure while terminal snapshot is present. Reset clears terminal;
  start with existing configuration may also clear terminal as already
  documented. Add post-terminal configure rejection with full state unchanged.

### Signal and LC3 lifecycle

- `preamble -> scored` succeeds only when every configured semantic channel has
  rendered exactly `HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES`. Early transition fails
  atomically. Keep scored -> tail ordering.
- Update scored/LC3 tests to consume exact preamble on all channels before
  entering scored. For raw signal tests, use bounded scratch chunks; for LC3
  tests, encode exact profile preamble frame count so LC3 state matches real
  runtime. Test helpers are allowed, but assertions remain public outcomes.
- At scored entry, derive PRNG, apply one `xorshift32`, then choose first block
  target from updated LSB. Every later 5760-sample boundary applies exactly one
  further update. Update exact ramp/target tests.
- Preserve carrier phase across exact preamble -> scored transition. Update
  expected first scored phase to full preamble sample count.
- Replace `preamble_rendered + samples > total` with subtraction-based bounds so
  huge `samples` cannot wrap before rejection. Add huge-count atomic rejection
  test without attempting huge allocation.

### Generator

- Make `--write` and `--check` mutually exclusive. Bare invocation remains
  check-only.
- Make doc and implementation agree: exit 0 clean, 1 drift/I/O, 2 argparse
  usage.
- Add a small stdlib test inside existing `scripts/test_hil_runner.py` invoking
  generator with both flags and asserting exit 2/no file change, plus bare and
  `--check` success. Do not add another Python child.

## Verification

Run from repo root:

```bash
python3 scripts/generate_hil_source_sine_lut.py --check
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-review-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-review-twister
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/generate_hil_source_sine_lut.py
git diff --check
git status --short
```

Inventory remains 65. Warnings fail. Do not run full dirty-tree canonical gate
or hardware commands.

## Constraints and recap

- Edit only RH1A core/generator/tests and this handoff if a fact needs
  correction. Preserve RH0, BSim, receiver, and unrelated user files.
- No runtime architecture, shell, BAP, sysbuild, hardware, dependency, or
  release change.
- Do not commit, push, merge, amend, open PR, or edit git configuration.
- Escalate after two materially different failed attempts or contradictory
  liblc3 evidence.

Return files changed, behavior fixed, exact test results, warnings, deviations,
blockers, and current status.

## Review round 2 corrections

First correction pass closes listed defects and reports 38 control, 21 signal,
and 76 Python tests passing. Close two final fail-closed boundary issues:

1. `hil_source_signal_render()` checks `cap < (size_t)samples *
   sizeof(int16_t)` without guarding multiplication. On 32-bit target,
   `samples=UINT32_MAX` wraps size calculation before scored/tail rendering and
   can bypass capacity rejection. Before multiplication, reject when
   `(size_t)samples > SIZE_MAX / sizeof(int16_t)`. Return `-ENOSPC` with encoder
   and output unchanged. Add scored and tail huge-count tests using a canary
   output and full encoder snapshot. Keep subtraction-based preamble check.
2. Top-level unknown key longer than 16 decoded bytes currently returns `range`
   from key buffer before unknown-key classification. Strict schema promises
   `unknown_key` for every unknown key within 511-byte line. Make key lexer
   consume and validate long key without large per-key storage, preserve enough
   prefix/length metadata to establish it cannot equal a known key, parse its
   value fully, then return `unknown_key`. Do not increase parser context above
   2 KiB. Add long unknown-key tests with valid scalar and nested values; malformed
   value still returns `syntax` because lexical validity precedes schema class.

Rerun full review verification. Inventory remains 65. No other behavior or
scope change.
