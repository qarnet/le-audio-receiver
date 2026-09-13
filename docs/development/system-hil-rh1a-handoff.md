# System HIL RH1A source-core implementation handoff

Status: implementation handoff for RH1A, the first internal slice of RH1.
RH0 is accepted after 72 warning-free host tests. RH1 remains open until a
later RH1B handoff adds the nRF5340DK shell, Bluetooth BAP client runtime,
settings/bond control, sysbuild image, and target build.

Follow `docs/development/system-hil-milestones.md`. This handoff freezes and
tests source-firmware core contracts only. It must not access hardware.

## Goal

Add repository-owned, target-reusable core modules for:

- a strict bounded `hil <raw JSON>` command schema;
- HIL1 record envelope formatting compatible with RH0 parser;
- run lifecycle, reconnect, stop, terminal, error, and counter state;
- deterministic left/right PCM generation with exact preamble, scored envelope,
  and tail behavior;
- real liblc3 frame encoding and mono, Mode A, and Mode B packing for exact
  `48_3_1` and `48_4_1` profiles;
- native public-boundary tests proving chunking, packing, deterministic bytes,
  decoded output, and failure atomicity.

No incomplete shell command or fake radio runtime is added. RH1B will compile
these exact modules into the dedicated source application.

## Grounding

- Plan: `docs/development/system-hil-milestones.md`, especially Frozen source
  codec profiles, Source signal and control contract, Source serial protocol,
  and RH1.
- RH0 wire parser: `scripts/hil/protocol.py`. Outbound records must retain its
  exact common fields and state names.
- Existing real BAP/liblc3 source:
  `tests/bsim/client/src/bsim_client_main.c` and `bsim_tx.c`.
- NCS v3.3.0 reference:
  `zephyr/samples/bluetooth/bap_unicast_client/`.
- Presets:
  `zephyr/include/zephyr/bluetooth/audio/bap_lc3_preset.h` defines
  `48_3_1` as 7.5 ms/90 octets and `48_4_1` as 10 ms/120 octets.
- Encoder API: `modules/lib/liblc3/include/lc3.h`:
  `lc3_setup_encoder()` and `lc3_encode()`. `lc3_encoder_mem_48k_t` covers both
  profiles. Native `CONFIG_FPU=y` + `CONFIG_LIBLC3=y` is already proven by
  `tests/unit/decode`.
- Zephyr `json_obj_parse()` is not strict enough: NCS v3.3.0 accepts unknown
  keys, duplicate keys, and trailing content. Use a small repository parser,
  not Zephyr JSON or cJSON.
- Existing inventory after RH0: 35 Twister + 5 exec-only + 23 Python = 63
  unit children, 66 total with coverage/matrix/BSim. Two new Twister suites
  make prospective inventory 37 + 5 + 23 = 65 unit children, 68 total. No
  clean-tree 68-total canonical result exists yet.

## Scope

Add:

- `hil/source/core/hil_source_types.h`
- `hil/source/core/hil_source_protocol.h`
- `hil/source/core/hil_source_protocol.c`
- `hil/source/core/hil_source_record.h`
- `hil/source/core/hil_source_record.c`
- `hil/source/core/hil_source_state.h`
- `hil/source/core/hil_source_state.c`
- `hil/source/core/hil_source_signal.h`
- `hil/source/core/hil_source_signal.c`
- `hil/source/core/hil_source_sine_lut.inc`
- `scripts/generate_hil_source_sine_lut.py`
- `tests/unit/hil_source_control/CMakeLists.txt`
- `tests/unit/hil_source_control/prj.conf`
- `tests/unit/hil_source_control/testcase.yaml`
- `tests/unit/hil_source_control/src/test_hil_source_control.c`
- `tests/unit/hil_source_signal/CMakeLists.txt`
- `tests/unit/hil_source_signal/prj.conf`
- `tests/unit/hil_source_signal/testcase.yaml`
- `tests/unit/hil_source_signal/src/test_hil_source_signal.c`

Update:

- `scripts/test-all.sh` current prospective inventory comment only, to 37
  Twister + 5 exec-only + 23 Python = 65 unit children and 68 total.

Do not update accepted historical counts in `STATUS.md`, `AGENTS.md`, coverage
baseline, result docs, or release evidence. Do not edit RH0 implementation
except if a cross-contract test exposes a real RH0 defect; escalate before any
such edit. Preserve user-owned untracked
`docs/development/firmware-release-fr4-summary-wait-fix-handoff.md`.

## Non-scope

- No nRF5340DK application, `prj.conf`, sysbuild, build/flash script, shell,
  UART, settings, bonds, scanning, BAP callbacks, ISO send thread, or hardware.
- No BlueZ, `btattach`, receiver firmware, audio capture, ALSA, or analyzer.
- No BSim refactor. Do not move or alter accepted Stage 1 client code.
- No `48_5_1`, extra codec, CAP/CAS/TMAS/CSIS, broadcast, fault injection, or
  analog acceptance.
- No generated reference hash copied from one run as acceptance. Tests prove
  behavior through repeat generation, exact packing, and decoded output.

## Shared types and constants

`hil_source_types.h` is plain C99/C11 with no Zephyr dependency except standard
fixed-width types. Freeze:

```c
enum hil_source_command {
    HIL_SOURCE_CMD_HELLO,
    HIL_SOURCE_CMD_IDLE,
    HIL_SOURCE_CMD_UNPAIR,
    HIL_SOURCE_CMD_CONFIGURE,
    HIL_SOURCE_CMD_START,
    HIL_SOURCE_CMD_STOP,
    HIL_SOURCE_CMD_STATUS,
};

enum hil_source_mode {
    HIL_SOURCE_MODE_MONO,
    HIL_SOURCE_MODE_A,
    HIL_SOURCE_MODE_B,
};

enum hil_source_profile {
    HIL_SOURCE_PROFILE_48_3_1,
    HIL_SOURCE_PROFILE_48_4_1,
};

enum hil_source_reconnect_policy {
    HIL_SOURCE_RECONNECT_NONE,
    HIL_SOURCE_RECONNECT_ONCE,
};

enum hil_source_semantic_channel {
    HIL_SOURCE_CHANNEL_LEFT,
    HIL_SOURCE_CHANNEL_RIGHT,
};

enum hil_source_signal_stage {
    HIL_SOURCE_SIGNAL_PREAMBLE,
    HIL_SOURCE_SIGNAL_SCORED,
    HIL_SOURCE_SIGNAL_TAIL,
};
```

Constants:

- protocol version: `1`;
- firmware ID: `le-audio-hil-source-rh1`;
- sample rate: `48000` Hz;
- left carrier: `997` Hz, Q0.32 phase step `0x05513CC2`;
- right carrier: `1601` Hz, Q0.32 phase step `0x0889E60F`;
- full amplitude: `8192`, low amplitude: `4096`;
- amplitude envelope block: `5760` samples (120 ms);
- transition ramp: `240` samples (5 ms);
- preamble segment: `11520` samples (240 ms);
- preamble segment count: `6`, total `69120` samples (1.44 s);
- tail minimum: `5000000` microseconds;
- default signal seed: `0x48A31C5D`;
- maximum scored SDUs per stream: `20000`;
- maximum command line JSON bytes: `511` excluding terminating NUL;
- command ID length: 1-63 characters;
- run ID length: 1-64 characters, matching RH0 safe pattern
  `[A-Za-z0-9][A-Za-z0-9._-]{0,63}`.

Expose helpers returning canonical wire strings for command, mode, profile,
reconnect policy, state, and verdict. Unknown enum values return `NULL`, never
an invented fallback.

## Strict inbound command schema

RH1B shell will register one raw command, invoked as:

```text
hil {compact-or-spaced JSON object}
```

`hil_source_protocol.c` parses supplied byte span without dynamic allocation or
input mutation. Use caller-owned `struct hil_source_command` output. On failure,
output remains byte-for-byte unchanged.

Every object has exact common keys:

```json
{
  "protocol_version": 1,
  "command": "hello",
  "command_id": "cmd-0001",
  "run_id": "run-0001"
}
```

Command-specific exact keys:

- `hello`, `idle`, `start`, `stop`, `status`: common keys only.
- `unpair`: add `peer_address`, `peer_address_type`.
- `configure`: add `peer_address`, `peer_address_type`, `mode`, `profile`,
  `scored_sdu_count`, `signal_seed`, and `reconnect_policy`.

Canonical string values:

- command: `hello`, `idle`, `unpair`, `configure`, `start`, `stop`, `status`;
- address type: `public` or `random`;
- peer address: uppercase canonical `XX:XX:XX:XX:XX:XX`;
- mode: `mono`, `mode_a`, `mode_b`;
- profile: `48_3_1`, `48_4_1`;
- reconnect policy: `none`, `once`.

`scored_sdu_count` is integer `1..20000`. `signal_seed` is integer
`1..UINT32_MAX`. Command ID uses same safe character set as run ID and maximum
63 characters. No field permits empty string.

Implement a bounded flat-object parser supporting JSON whitespace, string
values, and unsigned canonical decimal integers. It must reject:

- non-object root, malformed/truncated input, trailing non-whitespace;
- unknown or duplicate key;
- missing common or command-specific key;
- extra key for selected command;
- wrong scalar type, booleans/null/arrays/objects, negative/fraction/exponent,
  plus sign, and leading-zero integer except `0`;
- integer overflow and configured range violations;
- string overflow, invalid control character, unsupported `\u` escape, invalid
  escape, or decoded value outside canonical semantic grammar;
- line length above 511 bytes.

Support standard simple JSON escapes (`\"`, `\\`, `\/`, `\b`, `\f`, `\n`,
`\r`, `\t`) during lexical parsing, then let semantic grammar reject decoded
control/unsafe characters. Reject `\u` rather than implementing partial Unicode.

Expose stable parse result enum and `hil_source_parse_result_name()`. Required
classes: `ok`, `too_long`, `syntax`, `unknown_key`, `duplicate_key`,
`missing_key`, `wrong_type`, `range`, `unsupported_value`. Do not expose input
contents in error strings.

## HIL1 record envelope

`hil_source_record.c` formats one complete line without line terminator into
caller buffer. Exact key order:

```json
{"protocol_version":1,"kind":"state","firmware_id":"le-audio-hil-source-rh1","monotonic_ms":123,"command_id":"cmd-0001","run_id":"run-0001","segment":0,"data":{"state":"idle"}}
```

Kinds exactly `ack`, `state`, `terminal`, `status`. Validate kind and safe
firmware/command/run IDs before formatting. `monotonic_ms` and segment are
unsigned 32-bit. Return written length on success and a negative errno on
invalid input or truncation. Never emit partial success; caller buffer must be
an empty C string on failure when capacity is nonzero.

Expose one generic envelope function taking a trusted, compile-generated
`data_json` object string. It must at minimum verify nonempty object delimiters
and reject CR/LF/NUL inside supplied span. Host input is never passed as
`data_json`. Add convenience functions for:

- start ack: data includes command `start` and `accepted:true`;
- state: data contains canonical `state`;
- terminal: data contains verdict `pass` or `fail`;
- status/error response: data contains command, `ok`, and stable error name.

Output must parse through RH0 `parse_hil1_line()`. Add a small Python
cross-contract test inside existing `scripts/test_hil_runner.py` only if needed
to validate checked record examples; do not add another Python child. If this
updates RH0 test count, keep inventory count unchanged.

## Pure run state

`hil_source_state.c` owns no Bluetooth objects, threads, locks, clocks, or I/O.
Freeze state enum in RH0 order:

1. `idle`
2. `configured`
3. `connecting`
4. `secured`
5. `discovered`
6. `qos`
7. `streaming`
8. `scored_complete`
9. `teardown`

State object contains stored configuration, `has_config`, `active`,
`stop_requested`, current segment, terminal-not-set/pass/fail, first error,
and per-stream counters for at most two streams:

- submitted SDUs;
- submitted scored SDUs;
- send failures;
- sent callbacks.

API behavior:

- reset returns empty idle, clears configuration and every volatile field;
- configure is accepted only while inactive, validates enums/count/seed, copies
  whole configuration atomically, and does not start run;
- start requires configuration, clears volatile counters/error/stop/terminal,
  sets active segment 0 at idle;
- advance within segment follows exact state sequence with no repeat, skip, or
  regression;
- next segment is allowed only from teardown, only when reconnect policy is
  `once`, only once, increments segment exactly, and starts at connecting;
- stop request is idempotent and only sets flag; status reads do not mutate;
- counter operations reject stream index outside configured stream count and
  reject overflow without changing state;
- first error accepts first nonzero negative errno and never overwrites it;
- `scored_sdu_count` is a per-segment, per-stream target. Track scored counter
  baselines at each segment start. Advancing from streaming to scored_complete
  requires every stream to have submitted exactly that segment target, with no
  overrun. Starting reconnect segment records new baselines;
- terminal is accepted once only after teardown. Pass requires no first error,
  zero send failures, and every configured stream's scored submitted count
  exactly equals configured target for `none`, or two times configured target
  after both segments for `once`. Failure verdict remains legal after any
  teardown so source can report diagnosed failure;
- terminal clears active but preserves immutable status snapshot until reset or
  next successful start;
- every rejected operation leaves all state bytes unchanged.

Stream count derives from mode: mono 1, Mode A 2, Mode B 1.

## Deterministic signal

Use one independent `struct hil_source_signal_channel` per semantic channel.
No heap, floating-point runtime math, or frame-boundary phase reset.

### Carrier

Use Q0.32 phase accumulator with constants above. Use a checked-in 257-entry
quarter-wave signed Q15 sine table, mirrored into 1024 phase indices. No runtime
interpolation. Increment phase once for every generated sample, including
silence. Convert Q15 carrier to selected amplitude as
`(int64_t)wave_q15 * amplitude / 32767`, relying on C99 signed division toward
zero. Clamp only as a defensive invariant; valid constants cannot exceed
int16 range. Avoid implementation-defined signed overflow.

`scripts/generate_hil_source_sine_lut.py` generates the table using
`round(sin(pi*i/512)*32767)` for `i=0..256`, has `--write` and `--check` modes,
and defaults to no destructive rewrite. `--check` returns nonzero on drift.

### Preamble

Six exact 240 ms segments, no transition ramp:

1. silence;
2. left only at full amplitude;
3. silence;
4. right only at full amplitude;
5. silence;
6. both at full amplitude.

After exactly 69120 samples, further preamble render fails without state
mutation. Preamble divides exactly into 192 frames at 7.5 ms or 144 frames at
10 ms.

### Scored envelope

At scored-stage entry, carrier phase continues from preamble. Derive channel
PRNG states from configured seed:

- left: `signal_seed ^ 0x4C454654`;
- right: `signal_seed ^ 0x52494748`;
- replace derived zero with `0x6D2B79F5`.

Use xorshift32 in exact order `x ^= x << 13; x ^= x >> 17; x ^= x << 5`, with
defined `uint32_t` wrap. One update per 5760-sample block. LSB selects low or
full amplitude. First block ramps from full preamble amplitude to first target.
Every later block ramps from previous to next target for first 240 samples,
then holds. For ramp index `0..239`, exact amplitude is
`old + ((int64_t)(next - old) * (index + 1)) / 240`, with C99 truncation toward
zero. Left and right have independent state.

### Tail

Tail emits exact PCM zero while phase continues. Caller chooses frame count.
RH1B will use `ceil(5000000 / frame_duration_us)`: 667 frames for 7.5 ms and
500 frames for 10 ms, guaranteeing at least five seconds.

Stage transitions only allow preamble -> scored -> tail. Rejected transition,
overrun, null pointer, or insufficient output capacity leaves signal/encoder
state unchanged.

## LC3 encoder and mode packing

Keep encoder API in `hil_source_signal.[ch]` or a clearly named sibling only if
needed; do not create another test suite. Use `lc3_encoder_mem_48k_t` and one
`lc3_setup_encoder()` context per logical channel.

Profile contract:

| Profile | samples/channel | octets/channel | mono/Mode A SDU | Mode B SDU |
|---|---:|---:|---:|---:|
| `48_3_1` | 360 | 90 | 90 | 180 |
| `48_4_1` | 480 | 120 | 120 | 240 |

Packing:

- mono: one left semantic encoder, one frame;
- Mode A stream 0: one left encoder; stream 1: one right encoder;
- Mode B: independent left and right encoders, bytes `[L frame][R frame]`.

Expose frame/profile helpers and an encoder initialized with profile plus one or
two semantic channel selections. `encode_next()` checks stage, output capacity,
and expected frame size before generating PCM. On success it returns exact SDU
length. On any failure, signal phases, PRNG/envelope counters, and LC3 encoder
state must remain unchanged. Since liblc3 context cannot be rolled back after a
codec failure, validate all caller-controlled failure conditions before first
`lc3_encode`; treat an unexpected liblc3 failure as fatal/non-retryable and mark
context unusable. Tests need not fabricate impossible codec internal rollback.

## Tests

Both suites use `native_sim/native/64` and compile exact core modules.

### `hil_source_control`

Cover at minimum:

1. all seven valid command shapes and canonical enum mapping;
2. whitespace and allowed JSON escapes;
3. non-object, malformed, trailing content, overlong line/string, invalid
   escape/Unicode/control;
4. unknown, duplicate, missing, extra, wrong-type fields;
5. bool/null/array/object/negative/fraction/exponent/plus/leading-zero integer;
6. integer overflow and every semantic range/value boundary;
7. parser failure leaves output bytes unchanged;
8. exact HIL1 envelope and convenience records, truncation/unsafe ID/data
   rejection, empty output on failure;
9. state single-segment lifecycle and one reconnect segment;
10. repeat/skip/regression/second reconnect/early terminal rejection with state
    byte snapshot unchanged;
11. configure/start/reset/stop idempotence and busy/missing-config failures;
12. stream-count derivation, counter bounds/overflow, first-error-wins;
13. pass prerequisites, diagnosed fail terminal, duplicate terminal, immutable
    post-terminal snapshot.

### `hil_source_signal`

Cover at minimum:

1. generated LUT check is clean and quarter-wave endpoints/symmetry are exact;
2. profile helper values and invalid profile rejection;
3. exact preamble segment energy/zero pattern and total frame counts for both
   durations;
4. same semantic PCM sample range is byte-identical when rendered in 360- and
   480-sample chunks;
5. phase continuity across frame and stage boundaries;
6. scored xorshift target sequence, 120 ms boundaries, exact 240-sample ramps,
   left/right independence, same-seed repeatability, and different-seed change;
7. exact tail silence and continued phase position;
8. deterministic LC3 bytes from two independently initialized contexts for
   each profile and every mode across multiple frames;
9. Mode A left/right frame bytes equal corresponding Mode B L/R slices for same
   profile, seed, stage, and frame index;
10. exact SDU lengths and L-before-R packing;
11. decode emitted active frames with real liblc3; decoded sample counts are
    exact, independent repeated runs decode byte-identically, active output has
    nonzero energy, and left/right outputs differ. Tail silence is proved at
    exact input PCM boundary; do not invent a lossy-decoder residual threshold;
12. null/invalid transition/preamble overrun/insufficient-capacity failures are
    atomic; retry after caller error matches clean encoder bytes.

Do not assert private helper calls, copied hashes, or allocation identity.

## Verification

Run from repository root in NCS v3.3.0 shell:

```bash
python3 scripts/generate_hil_source_sine_lut.py --check
west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-twister
west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-twister
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --python
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/generate_hil_source_sine_lut.py
git diff --check
git status --short
```

Expected inventory count: 65. Do not run hardware commands. Do not run full
canonical gate from dirty worktree; report it as unrun to preserve clean-tree
provenance.

## Constraints

- NCS v3.3.0 only.
- Warnings are failures. Fix every compiler, linker, Kconfig, Python, and test
  warning; do not normalize warning text.
- No dynamic allocation in core modules.
- No floating-point runtime signal generation.
- No receiver production-code changes.
- No static probe serial mapping, flash, reset, recovery, serial access,
  Bluetooth host action, sudo, or audio device access.
- Do not weaken accepted Stage 1, RH0, or coverage tests.
- Do not commit, push, merge, amend, open a PR, or edit git configuration.

## Escalation and recap

Stop after two materially different failed attempts, a liblc3/native mismatch,
or any need to invent runtime architecture outside this handoff. Preserve work
and return exact command/error evidence, git status, and one focused question.

On success return files changed, public contracts, exact test/build results,
inventory count, warnings, deviations, blockers, and suggested RH1B follow-up.
