# PB-031 P1 handoff: fixed LC3 TX corpus and exact transport oracle

Status: Approved implementation handoff for P1. P0 numerical threshold
selection remains blocked on identified Intel x86_64 evidence and does not
block this independent exact-transport phase.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

## Goal

Replace BabbleSim client's runtime LC3 encoder with sequence-indexed checked-in
LC3 bytes. Add parser-owned exact hashes over every successfully submitted SDU
so corruption, wrong channel, wrong Mode B ordering, sequence omission,
duplication, and reorder fail at client transport boundary.

This phase changes transmitted fixture bytes, but must preserve scenario
topology, BAP calls, sequence numbers, successful-send counts, malformed-SDU
shape, lifecycle behavior, and receiver decoded-PCM oracle. Existing receiver
PCM hashes remain temporary acceptance until P2/P3 replaces them.

## Scope

Modify only:

- `tests/bsim/client/CMakeLists.txt`
- `tests/bsim/client/prj.conf`
- `tests/bsim/client/src/bsim_tx.c`
- `tests/bsim/client/src/bsim_tx.h`
- `tests/bsim/client/src/bsim_client_main.c`
- `tests/bsim/stage1-scenarios.json`
- `scripts/bsim_stage1_parse.py`
- `tests/unit/bsim_runner/test_bsim_stage1_parse.py`
- `tests/fixtures/lc3/README.md`
- PB-031 Implementation Notes
- this handoff, only for factual correction or result appendix

## Non-scope

- No production source or receiver observer/sink changes.
- No numerical PCM limits, Intel calibration claims, or threshold selection.
- No removal or repin of `known.full`, `known.l`, or `known.r`; P2/P3 owns that
  migration. If fixture-driven TX changes those current receiver diagnostics,
  stop and report measured differences. Never repin in P1.
- No BSim scenario count, timing, lifecycle, PLC, teardown, or response-policy
  change.
- No workflow, firmware, release, hardware, full gate, commit, push, or PR.

## Grounded current behavior

- `bsim_tx_encode_sdu()` currently generates deterministic PCM and runs
  `lc3_encode()` for each successful candidate. Encoder state lives in each
  active TX slot.
- Successful `bt_bap_stream_send()` commits `send_count` and 16-bit `seq_num`
  under `tx_lock`. Generation and stream identity protect stale in-flight
  commits. Preserve this ownership and commit point.
- One TX thread means at most one in-flight candidate per slot. `tx_lock` is
  never held across allocation or send. Preserve this rule.
- Malformed scenario currently sends 119 synthetic bytes at sequence 20. P1
  must instead derive same 119-byte shape by taking valid 120-byte left corpus
  frame 20 and removing its last byte.
- Corpus has 128 continuous frames per stream. Every current bounded stream
  sends at most 110 frames. Any sequence at or above 128 is hard TX error, not
  wrap or reuse.
- Reconnect unregisters logical `streams[0]`, then registers fresh
  `streams[1]`. Exact transport evidence must retain first stream history and
  prove second stream restarts from corpus sequence zero.
- Current client PASS fields are `sends0`, `sends1`, `cfgrsps`, `relrsps`, and
  `disrsps`. Parser owns exact or bounded send-count rules.

## Exact fixture selection

Embed only four `.lc3` corpus files with Zephyr's existing
`generate_inc_file_for_target()` helper:

- 10 ms, 120 bytes, channel 0: `bsim_48k_10ms_120b_l.lc3`
- 10 ms, 120 bytes, channel 1: `bsim_48k_10ms_120b_r.lc3`
- 7.5 ms, 90 bytes, channel 0: `bsim_48k_7p5ms_90b_l.lc3`
- 7.5 ms, 90 bytes, channel 1: `bsim_48k_7p5ms_90b_r.lc3`

For mono and Mode A, append one selected frame. For Mode B, append left frame
then right frame for same logical sequence. Validate registration config before
publication: frequency exactly 48000, supported duration/octet pair, channel
count 1 or 2, channel index 0 or 1 for one-channel streams, and corpus capacity.
Unsupported shapes return `-EINVAL`. Remove encoder storage, encoder setup,
PCM generation, `<lc3.h>`, and now-unused client-only
`CONFIG_LIBLC3`/`CONFIG_FPU`/`CONFIG_SPEED_OPTIMIZATIONS` settings.

Build SDU after slot snapshot without `tx_lock`. Check destination tailroom.
Build failure decrements `in_flight`, releases buffer, does not call send, and
does not advance count, sequence, or hash. Keep existing generation/stream
check around successful commit.

## Exact TX hash contract

Use unsigned 32-bit FNV-1a:

- offset basis `0x811C9DC5`
- prime `0x01000193`
- wrapping unsigned multiplication

For each SDU whose `bt_bap_stream_send()` returns zero, hash in this order:

1. logical sequence as four little-endian bytes;
2. exact final SDU bytes and exact final length passed to
   `bt_bap_stream_send()`.

Thus malformed sequence 20 hashes valid left frame 20 minus its final byte.
Compute candidate hash before send while buffer remains caller-owned; publish it
only inside same successful generation/stream commit as send count and sequence.
Failed or stale sends never affect audit state.

Add public result type and getter:

```c
struct bsim_tx_result {
	uint32_t send_count;
	uint32_t fnv1a_hash;
};

int bsim_tx_result(const struct bt_bap_stream *bap_stream,
		   struct bsim_tx_result *result);
```

Result getter returns `-EINVAL` for null arguments and `-ENODATA` when stream
was never registered. Retain an audit record keyed by logical
`bt_bap_stream *`, separate from two reusable active slots, so result remains
available after unregister. Two logical transmitting streams are maximum in
current scenarios. Registration resets existing audit for same pointer to
offset basis/count zero or allocates empty audit; fail `-ENOMEM` before
publishing active slot if no audit record exists. Each successful commit updates
active slot and matching audit atomically under `tx_lock`.

Make existing `bsim_tx_send_count()` read same retained audit record. This keeps
active wait behavior unchanged and makes reconnect PASS report historical
session-1 count instead of losing it after unregister.

Extend client PASS with mandatory fields, preserving existing fields and order:

```text
txc0=<decimal> txh0=0x%08X txc1=<decimal> txh1=0x%08X
```

For never-registered logical streams, client emits count zero and offset basis.
Any getter error other than `-ENODATA` fails scenario rather than fabricating
evidence. `txcN` must equal `sendsN` for both streams after retained-count fix.

## Scenario schema 2

Bump `tests/bsim/stage1-scenarios.json` to schema 2. Keep every existing run,
decoder-call, channel-mode, and `known` value unchanged. Require `transport` on
every scenario. It is a list with zero to two entries:

```json
{"stream": 0, "layout": "mono", "fixtures": ["bsim_48k_10ms_120b_l"]}
{"stream": 0, "layout": "stereo-concat", "fixtures": ["bsim_48k_10ms_120b_l", "bsim_48k_10ms_120b_r"]}
```

Rules:

- stream is unique and 0 or 1;
- `mono` has exactly one fixture;
- `stereo-concat` has exactly left then right fixture of same duration;
- optional `malformed_at` is a non-negative integer below 128 and appears only
  on `invalid_sdu_resume_10ms`, stream 0, value 20;
- streaming scenarios describe every transmitting logical stream;
- unsupported-source, no-free-slot, and invalid-codec scenarios use `[]`.

Parser validates schema, exact transport entry keys, known fixture stems, and
layout/fixture consistency. Preserve existing `known` receiver hash handling.

## Parser-owned expected hash

`scripts/bsim_stage1_parse.py` must independently load and validate
`tests/fixtures/lc3/portable-oracle-manifest.json`, including LC3 path, size,
SHA-256, frame geometry, and 128-frame capacity, before deriving hashes. Reuse
the strict `load_manifest()` implementation from `lc3_pcm_calibrate.py` rather
than creating weaker duplicate validation; translate its error to
`ScenarioDataError` or `ParseError` with clear context.

For each scenario transport entry, use client `txcN` as frame count, require it
equals `sendsN`, reject count above corpus capacity, build each sequence payload
from checked-in bytes according to layout, apply optional one-byte-short
mutation, and calculate expected FNV-1a using exact C contract. Require emitted
`txhN` equals expected. For absent stream entries require `txcN == sendsN == 0`
and `txhN == 0x811C9DC5`.

Exact hash must therefore reject wrong channel, Mode B right/left reversal,
payload corruption, malformed-byte substitution, sequence omission,
duplication, and reorder without storing CPU-dependent PCM hashes.

## Tests

Extend public parser tests so generated valid client PASS fixtures contain
correct `txc0/txh0/txc1/txh1`. Cover:

1. schema 1 rejected; schema 2 and all repository scenarios accepted;
2. malformed transport keys, duplicate stream, unknown fixture, bad layout,
   wrong fixture count/order, invalid `malformed_at`, and missing transport
   rejected;
3. missing TX PASS fields rejected;
4. emitted TX count differing from `sendsN` rejected;
5. hash bit change rejected;
6. mono wrong-channel hash rejected;
7. Mode A channel swap rejected;
8. Mode B concatenation reversal rejected;
9. omission, duplication, reorder, and payload corruption produce hashes that
   differ from parser-derived expected value;
10. malformed scenario uses truncated fixture frame and synthetic old payload
    hash fails;
11. reconnect stream 1 count-100 hash equals fresh mono 10 ms count-100 hash,
    proving sequence restart; retained stream-0 transport also verifies;
12. no-transport scenarios require zero counts and offset-basis hashes;
13. existing receiver hashes, counts, lifecycle checks, CLI precedence, and
    output-root tests stay green.

## Verification

Run in order:

```bash
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
nix develop -c env BSIM_LOG_ROOT=/tmp/opencode/pb031-p1-bsim \
  bash scripts/bsim-stage1-run.sh
nix develop -c ./scripts/test-all.sh --phase unit
nix develop -c backlog doctor
git diff --check
```

Use a new empty external BSim log root. Require 17 scenarios, 26 runs, all
parser checks pass, two-run scenarios produce identical TX hashes, and existing
receiver PCM hashes/counts remain unchanged. Scan build/run output for warnings
under repository policy. Preserve failed logs and stop instead of repinning.

## Escalation

Stop without commit after two materially different failed fixes, any existing
receiver hash/count drift, corpus exhaustion, inability to preserve reconnect
history, unexplained warning, flaky TX hash, or need to change production,
receiver, lifecycle, scenario timing, or numerical PCM policy. Return exact
logs, diff, and one focused blocker.

## Reviewed amendment: malformed history and warning-clean BSim

Approved after first 25-run execution stopped at
`invalid_sdu_resume_10ms` run 1. This amendment supersedes only conflicting
sentences above that forbid removing any receiver hash or expanding warning
cleanup. All other scope and invariants remain.

### Malformed receiver pin

Remove only `known.full` from `invalid_sdu_resume_10ms`; retain its exact
`known.total`, exact TX hash, 101-send count, malformed observer, one decoder
error, 100 resumed pushes, and all lifecycle/count checks. Do not change any
other receiver hash or count pin.

Reason: old runtime path did not call `lc3_encode()` at sequence 20, so sender
encoder history skipped that logical source frame. Continuous checked-in corpus
encoded frame 20 before its 119-byte truncation. Receiver correctly rejects
that SDU, but later valid LC3 frames carry different encoder history. First
fixture run kept all counts (`pushes1=100`, `total1=108`, `derr1=1`) and exact
transport (`txc0=101`, `txh0=0xDD9C9459`) while receiver diagnostic changed
from `0x0C61918D` to `0x09513FCD`. Repinning either CPU-specific PCM value is
forbidden. P2 restores portable numerical PCM acceptance for this path.

Record this bounded temporary gap in scenario notes, PB-031 Implementation
Notes, fixture README, and P1 result appendix. Tests must assert malformed
scenario has no `known.full`, still has `known.total == 108`, and every named
behavior above remains enforced.

### Warning scope

Expand P1 to make BSim runtime warning-clean, except exact deliberate duplicate
Release warning already required by scenario 17. Modify these additional files
only for this warning repair:

- `tests/bsim/prj.conf`
- `tests/bsim/src/bsim_test_main.c`
- `STATUS.md`

First-run warning inventory under
`/tmp/opencode/pb031-p1-bsim-executor-20260915` found these causes:

1. Both BSim hosts configure ACL/ISO TX contexts above controller-reported
   count 3. Set receiver and client `CONFIG_BT_BUF_ACL_TX_COUNT=3` and
   `CONFIG_BT_CONN_TX_MAX=3`; set receiver and client
   `CONFIG_BT_ISO_TX_BUF_COUNT=3`. Do not change MTUs, RX pools, active ASEs,
   or scenario counts.
2. BSim controller has no stored static address. Before each app's
   `bt_enable(NULL)`, create one deterministic, distinct random-static identity
   with `bt_id_create(&addr, NULL)`. Use receiver bytes
   `{0x00, 0x00, 0x00, 0x00, 0x00, 0xC0}` and client bytes
   `{0x01, 0x00, 0x00, 0x00, 0x00, 0xC0}`, both
   `BT_ADDR_LE_RANDOM`. Require returned identity `BT_ID_DEFAULT`; otherwise
   FAIL before Bluetooth enable. Installed NCS v3.3.0 explicitly permits a
   caller-supplied random-static identity before `bt_enable()`.
3. Receiver enables settings without a destination backend, causing save/store
   errors. Keep `CONFIG_BT_SETTINGS=y`, `CONFIG_SETTINGS=y`, and
   `settings_load()` ordering. Add a test-local volatile settings store in
   `bsim_test_main.c`: `csi_load` returns success without entries and
   `csi_save` returns success while retaining nothing. Register same store as
   source and destination once before `bt_enable()`. This proves dynamic
   PACS/ASCS registration and in-process reconnect while deliberately providing
   no cross-process persistence. Check all arguments only enough to compile
   warning-free; no heap, filesystem, flash backend, or production code.
4. Client `stream_ops` omits callbacks required by upstream client teardown.
   Add no-op `stopped(stream, reason)` and `released(stream)` callbacks with
   exact installed header signatures. They acknowledge lifecycle notification;
   do not signal existing scenario semaphores or alter response accounting.
5. `start_streams()` sometimes calls `bt_bap_stream_start()` after endpoint is
   already `STREAMING`, which logs before returning tolerated `-EBADMSG`.
   Query `bt_bap_ep_get_info()` first. If state is already STREAMING, print
   existing informational message and skip Start. A query failure is returned.
   Keep existing return handling for races and other already-started results.
6. Disconnect/reconnect can race one TX candidate and log send `-EBADMSG`.
   In `scenario_disconnect_streaming()` and session 1 of
   `scenario_reconnect_second_stream()`, call `bsim_tx_pause()` immediately
   after existing 300 ms streaming window and before ACL disconnect. Pause
   drains in-flight TX but leaves BAP endpoint in STREAMING, so disconnect path
   remains externally the same. Do not add pause before Release/Disable unless
   new evidence proves another send race.

Expand parser fault scanning to inspect both receiver and client logs. Any ANSI
`<wrn>` or `<err>` line fails, except receiver scenario 17's exact substring
`Invalid operation in state: releasing`. Preserve existing semantic fault
markers. Add tests proving receiver and client warnings/errors fail and exact
scenario-17 allowlist remains narrow.

Document scenario 17's unavoidable warning in `STATUS.md` BSim runtime
diagnostics: duplicate Release deliberately reaches upstream ASCS invalid-state
response; parser permits only exact text for only that scenario while response
and cleanup counts prove expected behavior. No other warning/error is accepted.

Rerun parser tests, then full 17-scenario/26-run Stage 1 in a new external log
root. Require all original receiver pins except approved malformed `known.full`
remain byte-identical, all repeat TX hashes match, and warning scan reports only
the exact scenario-17 line. Then run unit phase, backlog doctor, and diff check.

### Review correction: sink start synchronization

First warning-clean matrix attempt failed `modea_one_cis_loss_10ms` with 19
post-boundary concealed pushes instead of 18. Do not change loss duration,
receiver count, hash, or oracle. Evidence showed client stream 1 remained
ENABLING when `start_streams()` treated `bt_bap_stream_start()` returning
`-EINVAL` as already started; receiver had logged only stream 0 started before
loss injection. Earlier warning-producing call on already-STREAMING stream 0
incidentally delayed this race.

Installed NCS v3.3.0 API contract at
`zephyr/include/zephyr/bluetooth/audio/bap.h:1232-1253` says a unicast client
uses `bt_bap_stream_start()` to send Receiver Start Ready only for SOURCE ASEs.
These test streams are SINK ASEs and auto-enter STREAMING when CIS connects.
Calling Start for them is incorrect; accepting `-EINVAL` while still ENABLING
is not synchronization.

Replace amendment step 5's one-shot query/Start handling with bounded sink-state
synchronization. In existing traversal order, poll `bt_bap_ep_get_info()` until
that exact endpoint reaches `BT_BAP_EP_STATE_STREAMING`, sleeping 1 ms between
queries and returning `-ETIMEDOUT` after 10,000 ms. Return any query error.
Print existing `CLI stream %zu already streaming` information once after state
is reached. Do not call `bt_bap_stream_start()` from this helper and do not
alter callback semaphores, connect order, loss duration, send limits, receiver
pins, or scenario schema. Public endpoint state is authoritative; TX already
uses same state gate.

Run focused one-CIS-loss scenario twice using existing built binaries if
practical, then run fresh complete matrix. Both focused/full evidence must keep
18 post-boundary concealed pushes and original hashes/counts. If drift remains,
stop with logs rather than changing timing or acceptance.

### Final review repair: remove timing coupling without changing pins

Second attempt proved waiting for STREAMING is correct state observation but
changes when wall-clock teardown windows begin. Existing lifecycle receiver
pins depend on their established start-relative windows. It also proved the
one-CIS-loss 200 ms wall-clock pause produces 19, not 18, concealments after
warning cleanup. Replace the prior review correction and amendment step 6 with
this final shape:

1. `start_streams()` handles only these SINK ASEs. Query each endpoint once in
   existing traversal order. Return query errors. If STREAMING, print existing
   `CLI stream %zu already streaming`; otherwise require ENABLING and print
   `CLI stream %zu sink auto-start pending`. Return `-EBADMSG` for any other
   state. Never call `bt_bap_stream_start()` and never wait here. This follows
   the installed API contract while preserving established window origin. TX's
   existing public-state gate and each scenario's `wait_for_sends()` provide
   actual data readiness.
2. Make one-CIS loss event-counted, not wall-clock-counted. After both streams
   have at least 50 successful sends, pause right with existing draining
   `bsim_tx_pause()`, snapshot left successful-send count, wait until left has
   exactly 18 additional successful sends, then resume right. Define one named
   client-local constant `MODEA_ONE_CIS_LOSS_SEND_COUNT 18U`; update nearby
   comment. Keep both final send limits at 110. This directly models and proves
   18 absent right-CIS events; no sleep-duration tuning or receiver change.
3. Remove prior added draining `bsim_tx_pause()` calls before disconnect and
   reconnect. They create source-invalid gaps before ACL close. Preserve direct
   disconnect while streaming.
4. `bt_bap_stream_send()` may race an intentional Disable, Release, or ACL
   disconnect after TX's pre-send STREAMING check. On send error, query current
   endpoint state through existing `stream_is_streaming()`. Emit existing
   `LOG_ERR` only if it is still STREAMING. If state is no longer STREAMING,
   treat error as expected lifecycle closure, do not log warning/error, do not
   advance sequence/count/hash, and unref buffer exactly as current error path
   does. Do not suppress build failures, allocation failures, errors while
   STREAMING, or parser log scanning. No new quiesce API and no caller-side
   pause for lifecycle transitions.

Grounding: pre-cleanup evidence shows direct lifecycle behavior preserves pins:
first-stop receiver total 86; release and duplicate-release total 56;
disconnect/reconnect first segment total 63. Warning-clean sink-wait evidence
shifted release/duplicate totals to 63 and draining disconnect pause caused
source-invalid push 60. Preserve original receiver values, not shifted results.

Run parser tests and complete fresh matrix. If event-counted loss is not exactly
18 with original `0x30D6BAF0`/`0x32777D65`/`0x9859F1D8` hashes, stop and return
left count snapshots plus logs. Never tune sleep duration or repin.

### Event-count correction: account for accepted ISO queue

First event-counted run snapped left send count 51, resumed at 69, and receiver
observed 15 post-start losses. This exact three-event difference matches
`CONFIG_BT_ISO_TX_BUF_COUNT=3`: `bsim_tx` counts successful controller
submissions, and pausing app submissions does not remove three already accepted
right-stream SDUs. Those queued right SDUs pair with first three later left
events before absence begins.

Keep `MODEA_ONE_CIS_LOSS_SEND_COUNT 18U`. Wait for left successful-send count
to advance by `MODEA_ONE_CIS_LOSS_SEND_COUNT + CONFIG_BT_ISO_TX_BUF_COUNT`
before resuming right. Add a build assertion that
`CONFIG_BT_ISO_TX_BUF_COUNT == 3`; buffer-depth warning repair and this fixture
model must not silently diverge. Comment must distinguish accepted submissions
from peer-delivered events. This is queue-depth accounting, not sleep tuning or
receiver repinning.

Run fresh complete matrix. Require receiver observe exactly 18 post-start
losses and retain all original one-CIS hashes/counts in both runs.

### Final loss synchronization: controller completions

Queue-depth correction produced 19 losses: successful app submissions are not
a stable event boundary, even after accounting for nominal ISO TX depth. Replace
that correction with controller-completion synchronization; remove its build
assertion and queue-depth addition.

Add `.sent` to client `stream_ops`. Callback increments one `atomic_uint`
completion counter selected by exact `&streams[i]` pointer; unknown pointers do
nothing. Add a bounded helper that waits up to 10,000 ms for one stream's
completion counter to reach a supplied target, sleeping 1 ms and returning
`-ETIMEDOUT` on expiry.

For one-CIS loss only:

1. Keep existing waits for 50 successful submissions on both streams.
2. Pause right with `bsim_tx_pause()` and snapshot its successful-send count.
3. Wait until right completion count reaches that snapshot. This drains every
   right SDU accepted before pause without assuming queue depth.
4. Snapshot left completion count after right drain.
5. Wait for exactly `MODEA_ONE_CIS_LOSS_SEND_COUNT` (18) further left
   completions, then resume right immediately.
6. Keep final successful-send limits and exact transport audit unchanged.

`bt_bap_stream_ops.sent` contract in installed NCS v3.3.0 identifies controller
completion, while noting completion timing is implementation-dependent. Here it
is synchronization only; receiver concealment count remains peer-delivery truth
and must equal 18 in both runs. Do not expose completion counts in PASS/schema,
use them as acceptance evidence, alter parser transport semantics, or repin.

## P1 exact-cap repair result (2026-09-15)

The final one-CIS-loss repair replaced polling pause placement with a per-slot
exact send-cap notification. The right stream starts at cap 48, drains accepted
right completions, waits 17 left pause-window completions, then changes to cap
110 and resumes. Evidence is retained at
`/tmp/opencode/pb031-p1-exact-gap-fix-20260915`.

Parser tests passed `94 PASS / 0 FAIL`; strict Stage 1 passed all 17 scenarios
and 26 runs. Both loss runs retained:

```text
pushes1=100 trans1=8 szero1=8 splc1=16 plc1=34 total1=216 derr1=0 mal1=0
h1=0x30D6BAF0 lh1=0x32777D65 rh1=0x9859F1D8
txc0=110 txh0=0x8980C79D txc1=110 txh1=0xDD25CC21
```

No compiler or non-allowlisted runtime warning appeared. Unit phase, `backlog
doctor`, and `git diff --check` passed.
