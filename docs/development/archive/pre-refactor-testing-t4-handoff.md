# Phase T4 handoff — BAP and Bluetooth behavior matrix

## Goal

Expand the accepted BabbleSim gate from one mono scenario into the full BAP
matrix in the master plan. Execute real `src/bt_bap.c`, `src/audio_decode.c`,
real Zephyr BAP/ASCS/PACS, real ISO transport, and real liblc3 encoding/decoding.
Lock channel routing, codec rejection, teardown, disconnect, and reconnect.

Base: accepted T3 commit `f8f0895` on `test/pre-refactor-behavior`.

## Git/execution

- Executor: `deepseek/deepseek-v4-flash`, variant `max`.
- No amend, push, merge, PR, physical hardware, flash, or serial action.
- BabbleSim execution belongs on `thomas-workstation`; desktop builds may still
  compile but lack provisioned PHY components.
- Include this handoff in T4 commits.

## Scope

- `src/bt_bap.c` and `src/bt_bap.h`
- narrow related lifecycle corrections in `src/stream_lifecycle.c` only if
  required by an observed T4 scenario; broad lifecycle matrix remains T5
- `tests/bsim/`
- `tests/bsim/client/`
- `scripts/bsim-stage1-run.sh` (name retained as canonical gate entry)
- optional parser/unit tests for runner output under `tests/unit/`
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- new `docs/testing/t4-bap-bsim-matrix.md`
- `STATUS.md`

## Non-scope

- Physical RF/audio hardware.
- BlueZ/WirePlumber stock desktop gate changes.
- I2S internals (T3 complete).
- Timing/actuator internals (T5).
- CAP initiator migration; use BAP client APIs already present.
- Alternate codec/rate support.

# Production behavior changes required

## Early codec-shape validation

Add one internal parser/validator used by ASCS Config and Enable. Validate before
slot allocation or lifecycle mutation:

- codec ID is LC3;
- frequency field present, converts successfully, exactly 48000 Hz;
- frame-duration field present, converts successfully, 7500 or 10000 us;
- octets-per-frame field present, 20..120 inclusive (advertised range);
- frame blocks per SDU resolves to exactly 1; missing optional field may use the
  Zephyr/spec fallback of 1, explicit other value is rejected;
- channel allocation absent means mono; present allocation must contain exactly
  one or two channels, never zero or >2.

Return ASCS `CONF_INVALID / CODEC_DATA` for missing/invalid fields. Source
direction remains `CONF_UNSUPPORTED / NONE`. Validation failure must not:

- allocate a sink slot;
- increment `num_sink_ase`;
- mutate a decoder/lifecycle slot;
- consume capacity needed by a later valid request.

Store validated frequency, duration, frame blocks, octets, and channel count in
the selected `bt_sink`; Enable uses stored shape and still fails safely if the
retained codec config no longer matches.

## Exact SDU validation and decode result handling

Before any decode/pull/copy, require exact payload length:

- mono or each Mode A ASE: `octets_per_frame * frame_blocks`;
- Mode B: `octets_per_frame * channel_count * frame_blocks`.

For a valid-flag packet with wrong length:

- increment decode-error/malformed-SDU evidence exactly once;
- do not call liblc3;
- do not mutate left/right pairing state;
- do not apply volume or push stale PCM.

For mono/Mode B, check `audio_decode_sdu()` return. Negative return skips volume
and sink push. For Mode A, hard decoder errors skip that half and cannot pair it.
PLC (`valid=false`) remains supported with configured byte shape and may produce
concealment output.

Add a BSim-only observer event for rejected SDU because production statistics
do not distinguish pre-decode malformed length from liblc3 hard error. Do not
add test-only fields to production stats.

## Mode A pair identity

Track each decoded half's ISO `info->seq_num`. Interleave/push only when both
halves belong to the same sequence. Use wrap-safe 16-bit comparison to discard
only the older unmatched half. Clear half-valid/sequence state on configure,
start-set completion, first gate close, release, stop, and disconnect.

The custom client must hold TX until both Mode A streams are streaming, so
reverse ASE start order begins both transmitted sequence counters at zero. This
makes exact pairing meaningful across CISes and avoids assuming unrelated
controller-start offsets.

## Close/release behavior

Release without prior Disable must:

- close gate before any later receive callback can decode/push;
- stop offload and audio sink exactly once through idempotent APIs;
- clear pending Mode A halves;
- clear lifecycle configuration for released slot;
- reset decoder/slot allocation so it is reusable;
- preserve truthful PACS contexts.

Apply same centralized close helper to stop/disabled/release/disconnect where it
reduces divergence, but preserve summary logging before stats reset.

# BSim-only resource and observation seams

## Resource seam

Production supports two sink ASEs. To exercise the otherwise unreachable
NO_MEM callback in BSim:

- BSim Kconfig registers 3 sink ASE endpoints and 1 source endpoint;
- test-only compile definition limits repo `bt_sink` stream pool to 2;
- production builds keep pool size equal to
  `CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT` (2) with no seam/symbol;
- register stream callbacks only for actual pool objects;
- third valid sink Config must reach repo callback and return `NO_MEM / NONE`.

The BSim source endpoint exists only to invoke repo source-direction rejection.
Production remains sink-only with zero source ASEs.

## Observer seam

Under a BSim-only symbol/header, emit passive events from real production flow:

- config accepted/rejected with direction/code/reason;
- gate opened/closed;
- malformed SDU rejected;
- receive blocked by closed gate;
- Mode A stale half discarded;
- release and disconnect cleanup.

Observer may count/assert/call BSim PASS/FAIL but must never drive production
state. Production builds contain no observer symbols.

# Custom client TX

Replace upstream repeated-tone TX helper with repo-owned test TX derived from
NCS v3.3.0 sample structure. Keep real BAP send and liblc3 APIs.

Required TX behavior:

- up to two active TX streams;
- independent LC3 encoder per channel;
- Mode A: separate one-channel ASEs, Front Left and Front Right;
- Mode B: one ASE with `[L frame][R frame]`, two independent encoders;
- deterministic integer PCM patterns differ by channel and evolve by sequence
  number, so swap, duplication, stale pairing, overwrite, and cross-pairing
  change hashes;
- hold sending until scenario-required stream count is streaming;
- same initial sequence (0) on both Mode A streams, including reverse start;
- record per-stream successful sends;
- pause/resume/unregister controls for lifecycle scenarios;
- inject exactly one malformed one-byte SDU at a controlled sequence, then
  resume valid LC3;
- no copied production receiver algorithm.

Avoid floating-point waveform generation in harness. Use defined unsigned
integer arithmetic and independent encoder state. Encode one frame block only.

# Receiver sink oracle

Refactor `tests/bsim/src/audio_sink_stub.c` into scenario-aware strict oracle.
Keep public sink API exact. Add test-only configuration/query functions.

For every streamed segment record:

- pushes, startup-zero pushes, startup PLC, final stats;
- malformed sample counts and pushes after stop;
- full interleaved ordered FNV-1a hash;
- left-channel ordered hash and right-channel ordered hash;
- per-channel energy min/max;
- configured sample count and segment index.

Normal mono/Mode A/Mode B scenarios:

- 100 nonzero pushes;
- exact expected decoder invocations per push (mono=1, Mode A/B=2), accounting
  for startup pushes;
- zero decode errors, malformed SDUs, post-start PLC, lifecycle faults;
- non-NONE sink contexts;
- exact known full/L/R hashes;
- mono L hash equals R hash;
- Mode A/B L hash differs from R hash.

Reset stopped/segment-local oracle state when a valid new stream shape is set
after disconnect/reconnect. Never hide push-after-stop.

Lifecycle/error scenarios use explicit expected segment counts/events rather
than weakening normal oracle rules.

# Scenario matrix

Build one receiver binary and one parameterized client binary. Install matching
BST test IDs for these 15 scenarios:

1. `mono_10ms`
2. `mono_7p5ms`
3. `modea_10ms`
4. `modea_7p5ms`
5. `modea_reverse_start_10ms`
6. `modeb_10ms`
7. `modeb_7p5ms`
8. `invalid_sdu_resume_10ms`
9. `modea_first_stop_10ms`
10. `release_without_disable_10ms`
11. `disconnect_streaming_10ms`
12. `reconnect_second_stream_10ms`
13. `unsupported_source_direction`
14. `no_free_sink_slot`
15. `invalid_codec_fields`

## Normal scenarios 1–7

Run each twice. Assert exact full/L/R hashes against checked-in runner constants
and pairwise equality. Assert exact frame/stat accounting and no warning/fault
markers. Reverse-start uses stream 1 Enable/Connect before stream 0 while TX
waits for both.

## Scenario 8 — malformed SDU then resume

After stable valid audio begins, send one one-byte SDU at fixed sequence, then
resume valid frames. Require exactly one malformed-SDU observer event and one
decode-error increment, no sink push for rejected SDU, then 100 valid nonzero
pushes with exact deterministic hashes. No crash/stale push.

## Scenario 9 — first Mode A ASE stops

After at least 20 paired pushes, Disable/release first ASE while second keeps
sending at least 20 more SDUs. Gate must close on first stop. Require zero sink
pushes after close, pending halves cleared, closed-gate receive events observed,
and clean teardown.

## Scenario 10 — release without disable

After at least 20 mono pushes, call Release directly. Require successful ASCS
release response, gate close, sink/offload stop, reusable slot cleanup, zero
pushes after close, then clean disconnect.

## Scenario 11 — disconnect while streaming

After at least 20 pushes, disconnect ACL. Require gate/decoder/lifecycle/sink/
offload cleanup, zero late pushes, advertising restart path ready.

## Scenario 12 — reconnect and second stream

Stream at least 20 pushes, disconnect, restart advertising through BSim receiver
main loop (matching production main ownership), reconnect, rediscover, configure,
and stream second segment to 100 pushes. Require fresh decoder/oracle/lifecycle
state, no stale half/slot/bond state, exact second-segment hash equal to fresh
mono 10 ms oracle.

## Scenario 13 — unsupported source

Discover BSim-only source endpoint, issue Config, and require exact
`CONF_UNSUPPORTED / NONE`. No slot allocation, decoder, gate, or sink push.

## Scenario 14 — no free slot

Configure first two valid sink endpoints successfully, configure third, require
exact `NO_MEM / NONE`. Release first two cleanly. Third failure must not mutate
pool/lifecycle counters.

## Scenario 15 — invalid codec fields

Against an idle sink endpoint, issue separate config attempts for:

- missing frequency;
- unsupported frequency;
- missing duration;
- invalid duration encoding;
- missing octets per frame;
- octets 19;
- octets 121;
- explicit frame blocks 2;
- channel allocation with 3 channels.

Require each exact `CONF_INVALID / CODEC_DATA`. Then configure one valid mono
shape successfully, proving failures consumed no slot. Missing optional frame
blocks may be separately proven to fall back to one and succeed if client API
can construct it unambiguously.

# Runner

Rewrite/extend `scripts/bsim-stage1-run.sh` while retaining path used by
canonical gate.

- acquire `flock` around shared `${ZEPHYR_BASE}/bsim_out` build/run use to stop
  concurrent gates from corrupting shared generated `build.ninja`;
- compile receiver/client once per gate;
- use one private `mktemp -d` log root; clean on success, preserve/print on
  failure or when `BSIM_KEEP_LOGS=1`;
- run scenarios 1–7 twice and 8–15 once;
- require receiver, client, and PHY exit zero;
- parse strict scenario-specific PASS records;
- reject any assertion, warning, decode/lifecycle fault, missing response, wrong
  count, hash mismatch, channel-hash relation mismatch, or PACS NONE context;
- print final compact 15-scenario matrix and known hash table;
- retain corrected T2 mono hash only if new evolving client pattern produces the
  same PCM (unlikely). Hash changes caused by deliberate stronger TX pattern are
  expected, must be baselined from two identical runs, explained, then pinned.

Add shell/parser unit tests if parsing is factored into Python. Do not validate
only PASS substring; parse every named field.

# Production receiver fixes likely exposed

Expected in-scope fixes include early shape validation, decode-return/SDU-length
handling, Mode A pair identity, release sink stop, lifecycle slot clearing, and
BSim advertising restart orchestration. Any other behavior fix requires a
small documented rationale and regression scenario; no unrelated refactor.

# Documentation/evidence

Create `docs/testing/t4-bap-bsim-matrix.md` with:

- architecture and test-only seams;
- all scenario commands/results;
- exact ASCS response matrix;
- normal full/L/R known hashes and pairwise results;
- lifecycle event/count evidence;
- production defects fixed;
- runtime and gate duration;
- no physical RF/audio claim.

Update behavior contract and coverage matrix. Mark T4 ACCEPTED only after exact
final-commit matrix passes twice consecutively and all production builds pass.
T5 next.

# Verification

Focused on workstation:

```bash
bash scripts/bsim-stage1-run.sh
bash scripts/bsim-stage1-run.sh
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Use bundle + detached workstation worktree for exact final commit, never modify
workstation main. Canonical full gate need run twice consecutively if practical;
at minimum matrix twice plus full gate once, because matrix itself is the long
BSim child. Any warning/failure needs diagnosis. Clean temp artifacts.

# Suggested commits

```text
fix: validate BAP codec and SDU shapes
tests: add deterministic multi-channel BAP client
tests: expand BabbleSim BAP scenario matrix
fix: harden BAP teardown and reconnect
docs: record T4 BAP matrix evidence
```

Return exact commits, scenario/hash/response tables, test counts, runner times,
full gate/build results, defects/deviations, cleanup, and blockers.
