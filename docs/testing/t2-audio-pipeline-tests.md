# T2 — Audio pipeline unit characterization

Version: T2, 2026-08-01.  Phase T2 locks LC3 decode/routing, volume, and
statistics to direct production-source proof with deterministic 48 kHz
golden output, and fixes three decoder defects found while building the
proof.  Base: accepted T1 commit `3899c2f` (branch
`test/pre-refactor-behavior`).

## Production sources linked and executed

| Suite | Production source compiled | Support code |
|-------|----------------------------|--------------|
| `tests/unit/decode` | `src/audio_decode.c`, `src/audio_stats.c` (both real, `CONFIG_LIBLC3=y`, real NCS v3.3.0 liblc3 1.1.2) | `src/lc3_wrap.c` — linker `-Wl,--wrap=lc3_decode` failure injection; checked-in fixtures embedded via `generate_inc_file_for_target()` |
| `tests/unit/volume` | `src/audio_volume.c` (real VCP branch), `src/audio_perf.c` (real) | test-local shadow of `zephyr/bluetooth/audio/vcp.h` (exact NCS v3.3.0 renderer types, zero VOCS/AICS counts) + fake `bt_vcp_vol_rend_register()` |
| `tests/unit/stats` | `src/audio_stats.c` (real) | none — no test seam needed |

The volume suite enables the production VCP branch with test-only compile
definitions (`CONFIG_BT_VCP_VOL_REND=1`, `CONFIG_BT_AUDIO_VOL_DEFAULT=195`)
— no invalid Kconfig assignments, no Bluetooth stack pulled in.

## Fixtures

`tests/fixtures/lc3/` — see `tests/fixtures/lc3/README.md` for the
generator command, input formulas, and all hashes:

| Fixture | Shape | `.lc3` | `.pcm` | SHA-256 (`.pcm`) | CRC-32 full |
|---------|-------|--------|--------|------------------|-------------|
| `mono_48k_7p5ms_60b` | mono 7.5 ms | 60 B | 1440 B | `8541ab2b…93763` | `E272CD4C` |
| `mono_48k_10ms_60b` | mono 10 ms | 60 B | 1920 B | `7740e597…df4a3` | `D546D96C` |
| `modeb_48k_7p5ms_60b` | Mode B 7.5 ms | 120 B | 1440 B | `a5df6d08…7f534` | `446235E4` |
| `modeb_48k_10ms_60b` | Mode B 10 ms | 120 B | 1920 B | `a2c16351…2c12c7a` | `6669E859` |

(Full SHA-256 for all eight binaries and per-channel CRCs are in the
fixture README.)  Generation: `bash tests/fixtures/lc3/generate.sh`,
deterministic and path-independent (verified: two runs in different
directories produce identical SHA-256).  Fixtures are never regenerated
during test runs; tests embed the checked-in binaries.

## Test counts

| Suite | Tests | Result |
|-------|-------|--------|
| `tests/unit/decode` (audio.decode) | 37 | 37/37 PASS |
| `tests/unit/volume` (audio.volume) | 12 | 12/12 PASS |
| `tests/unit/stats` (audio.stats) | 10 | 10/10 PASS |
| **Total new** | **59** | **59/59 PASS** |

## Defects fixed (production `src/audio_decode.c`)

1. **Mono overlap corruption** — the mono path decoded contiguous samples
   into the output buffer, then called
   `audio_decode_mono_to_stereo(stereo_out, stereo_out, spc)` with a
   forward loop.  The first write (`stereo_out[1] = mono[0]`) overwrote
   unread mono sample 1.  In-place expansion is now overlap-safe via
   backward expansion when input and output share the same base; separate
   buffers keep the forward path.  Locked by
   `test_mono_to_stereo_inplace_overlap_safe` and the mono golden tests
   (which decode through the exact production in-place path).
2. **Mode B statistics imbalance** — the right-channel decoder only
   counted hard errors; success and PLC were uncounted.  Every LC3 decoder
   invocation now applies identical accounting (success → decoded, 1 →
   PLC, <0 → decode error).  Locked by `test_stats_modeb_success_counts_both`
   and the Mode B PLC tests.
3. **Hard decode failures reported as success** — `audio_decode_sdu()`
   always returned 0.  It now returns `-EBADMSG` when any decoder
   invocation returns a hard negative, after still completing the second
   Mode B decoder call so independent decoder state stays aligned.  Locked
   by the failure-injection tests below.

## Decoder validation and rejection (locked)

`audio_decode_config()` returns `-EINVAL` without calling liblc3 for:
null context, channel count other than 1 or 2, frequency other than
48000 Hz, frame duration other than 7500/10000 µs, `frames_per_sdu` other
than exactly 1.  Every failed configuration leaves the context fully
reset.  On success: mono creates only the left decoder; Mode B creates two
independent decoders; `samples_per_ch` is exactly 360 (7.5 ms) or 480
(10 ms).  A partial setup failure (right decoder) resets the whole context
— no usable decoder state remains.  `audio_decode_reset(NULL)` is
harmless; resetting a real context clears scalar shape fields and both
decoder pointers.

`audio_decode_sdu()` rejects with `-EINVAL` before touching output or
decoder state for: null context/output, unconfigured/reset context,
stored unsupported shape, Mode B without a right decoder, `valid=true`
with NULL data, zero valid length, valid per-channel frame length outside
the liblc3 basic 20..400 byte range, Mode B length not divisible by the
channel count, and any length that could truncate input (bounds checked
before narrowing casts).  Rejection-then-golden tests prove decoder state
survives every rejection.

**PLC (`valid=false`) accepts any supplied length and never dereferences
frame data** — liblc3 ignores `nbytes` for NULL input, and rejecting PLC
lengths would silently drop concealment for truncated invalid SDUs.  This
matches pre-T2 production behavior and is documented as the intended
semantic of "supplied valid configured frame-byte shape".

## liblc3 1.1.2 semantics and the failure-injection seam

Verified empirically against the installed NCS v3.3.0 liblc3 (1.1.2):
`lc3_decode()` returns a hard negative **only** for parameter errors
(NULL handle, frame size outside 20..400 bytes).  A malformed bitstream of
valid length returns **1 (PLC)** — the decoder conceals.  Because
`audio_decode_sdu()` pre-validates both parameter-error cases, the
production `err < 0` accounting path is unreachable through real liblc3
input.

The handoff asked for a "hard malformed LC3 data of valid length returns
negative" test; against the pinned SDK that expectation is impossible
(any malformed valid-length frame is concealed, not errored).  Resolved
in scope:

- `test_malformed_valid_length_data_is_plc` locks the real installed
  semantics: malformed valid-length data → one PLC, one total, zero
  decode errors, success return.
- The production hard-error accounting (error counted exactly once per
  failed invocation, negative propagated after the second Mode B decoder
  call, never success) is exercised via a test-only linker wrap of
  `lc3_decode()` (`-Wl,--wrap=lc3_decode`, upstream Zephyr pattern).
  The wrapper delegates to `__real_lc3_decode()` for every normal call
  and injects `-1` only for test-designated decoder handles.  Locked by
  `test_mono_hard_failure_accounting`, `test_modeb_left_failure_counts_and_aligns`,
  `test_modeb_right_failure_accounting`, `test_modeb_both_failures`.

The `lc3_setup_decoder()` failure branch in `audio_decode_config()` is
likewise unreachable by construction: configuration validation rejects
every parameter combination that would make liblc3 return NULL, so the
defensive reset-on-setup-failure path is dead code kept for safety.

## Volume suite highlights

Real `audio_volume.c` VCP branch against the shadow VCP header and fake
registration backend: default registration (volume 195, unmuted, step 16)
and state; registration failure propagated with deterministic default
state; callback success/error packing semantics; volume 0 and mute zero
output; volume 255 bit-exact unity for `INT16_MIN/MAX/-1/0/1`;
intermediate volumes match signed 32-bit `sample * volume / 255` with C
truncation toward zero (including signed extremes); zero samples leave
memory untouched; `apply(NULL, 0)` harmless.  The real `audio_perf.c`
proves the performance hook stays balanced on mute, zero, unity, and
scale exits.  A toggler thread flips between two packed states
((255, unmuted) / (0, muted)) while the test applies volume to buffers —
every buffer reflects one atomic snapshot, never mixed scaling.

## Statistics suite highlights

Real `src/audio_stats.c`: frame decoded → total only; PLC → plc + total
exactly once; decode error / underrun / stream reset → own counter only;
mixed sequence exact snapshot; reset clears every counter; snapshot is
returned by value and never mutates future state; repeated reset/get is
deterministic.  Four concurrent threads increment each API 10000× with
exact final counts including `total = decoded + plc`.

## Unsupported shapes (still rejected at the decode layer)

Sampling rates other than 48 kHz, more than two channels, frame durations
other than 7.5/10 ms, and more than one frame block per SDU are rejected
by `audio_decode_config()` — but translating those rejections into ASCS
response codes in `bt_bap.c` remains **T4** (known gap, unchanged).

## BabbleSim oracle hashes changed with the mono overlap fix

The Stage 1 BSim oracle hashes locked in the *defective* mono output.  The
old forward in-place expansion overwrote unread source samples: with
input and output sharing the same base, every 960-sample push collapsed
to the first decoded sample of its frame (constant energy 12480 in the
10 ms scenario — the observed `energy_min == energy_max`).  The T2B
overlap-safe backward expansion (required fix) produces the correct
interleaved mono PCM, so the oracle values necessarily change:

| Scenario | Pre-T2 (corrupted mono expansion) | T2 (corrected) |
|----------|-----------------------------------|----------------|
| 10 ms (48_4_1) | `0xFE0D4245` (startup_zero=8, energy 12480 const) | `0x9225F075` (startup_zero=7, energy varies 10.4M..13.9M) |
| 7.5 ms (48_3_1) | `0x5853F445` (startup_zero=11) | `0x2011C0F9` (startup_zero=10) |

Both T2 values are deterministic across repeated runs (pairwise hash
equality enforced by `scripts/bsim-stage1-run.sh`, whose accepted-hash
constants were updated accordingly).  The mechanism was verified by
replaying the real BSim LC3 frames on the host: the old forward
expansion reproduces the constant-energy/collapsed stream shape (8
startup-zero pushes, 100 constant-energy pushes), while the corrected
path matches the new oracle counters.

## Non-claims

No audio-quality claim and no hardware claim are made from these
native_sim tests.  Golden PCM is exact for the pinned NCS v3.3.0 /
native_sim host toolchain only.  Production APIs, wire formats, and audio
formats are unchanged except the documented safe rejection and defect
corrections above.
