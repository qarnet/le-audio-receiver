# T4 — BAP and Bluetooth behavior matrix (BabbleSim)

Phase T4 expands the accepted BabbleSim gate from one mono scenario into
the full BAP matrix: real `src/bt_bap.c`, `src/audio_decode.c`, real
Zephyr BAP/ASCS/PACS, real ISO transport, and real liblc3 encode/decode.
Channel routing, codec rejection, teardown, disconnect, and reconnect are
locked by 15 strict scenarios executed twice (scenarios 1–8) or once
(9–15) per gate.

Accepted on commit `f50dcea` (final T4 commit on
`test/pre-refactor-behavior`), after the matrix passed **four consecutive
identical runs** (two baselines, two pinned acceptances) and the full
gate passed **twice consecutively**.

## Architecture and test-only seams

- **One receiver binary, one parameterized client binary.**  Each BST
  test ID selects one scenario on both sides
  (`tests/bsim/src/bsim_test_main.c`,
  `tests/bsim/client/src/bsim_client_main.c`).  The runner selects with
  `-testid`.
- **Resource seam** (`tests/bsim/prj.conf`): the BSim receiver registers
  three sink ASEs and one source PAC
  (`CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT=3`,
  `CONFIG_BT_ASCS_MAX_ASE_SRC_COUNT=1`,
  `CONFIG_BT_ASCS_MAX_ACTIVE_ASES=3`, `CONFIG_BT_PAC_SRC=y`,
  `CONFIG_BSIM_SOURCE_ASE=y`) while the repository stream pool stays at
  two (`CONFIG_BSIM_SINK_POOL_LIMIT=2`) — the third valid sink Config
  reaches the repository callback and returns `NO_MEM / NONE`.
  Production keeps pool == `CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT` (2) with
  zero source ASEs and no seam symbols.
- **Observer seam** (`tests/bsim/src/bsim_observer.[ch]`, compiled only
  under `CONFIG_BSIM_OBSERVER`): passive events from real production flow
  — config accepted/rejected (direction/code/reason), gate opened/closed,
  malformed SDU rejected, receive blocked by closed gate, Mode A stale
  half discarded, release and disconnect cleanup.  It never drives
  production state.
- **Strict sink oracle** (`tests/bsim/src/audio_sink_stub.c`,
  `bsim_sink_oracle.h`): public `audio_sink.h` API unchanged; per-segment
  records of pushes, startup-zero pushes, startup PLC, final
  `audio_stats`, malformed sample counts, pushes after stop, full
  interleaved ordered FNV-1a hash, left/right ordered hashes, per-channel
  energy min/max, configured sample count, and segment index.  Segment
  state resets when a valid new stream shape is set after
  disconnect/reconnect; push-after-stop is never hidden.
- **Deterministic multi-channel TX** (`tests/bsim/client/src/bsim_tx.[ch]`):
  repo-owned replacement for the upstream repeated-tone helpers; real
  `bt_bap_stream_send` and liblc3; per-channel encoders; integer-only PCM
  patterns that differ by channel and evolve by sequence number (swap,
  duplication, stale pairing, overwrite, and cross-pairing all change the
  hashes); TX holds until the scenario-required stream count is
  streaming; per-stream send caps for exact send counts; one injectable
  malformed SDU at a controlled sequence; pause/resume/unregister.

## Scenario commands

```bash
# Full matrix (canonical gate entry; scenarios 1-8 twice, 9-15 once):
bash scripts/bsim-stage1-run.sh
# Baseline mode (prints hashes, skips pinned asserts):
BSIM_BASELINE=1 bash scripts/bsim-stage1-run.sh
# Keep logs on success:
BSIM_KEEP_LOGS=1 bash scripts/bsim-stage1-run.sh
# Strict parser (also unit-tested in tests/unit/bsim_runner):
python3 scripts/bsim_stage1_parse.py check --scenario mono_10ms \
    --receiver R.log --client C.log --known-full 0xD65641A8
```

The runner acquires a `flock` on `${ZEPHYR_BASE}/bsim_out` (shared
build/run tree), compiles both binaries once per gate, runs every
simulation with receiver/client/PHY exit-zero enforcement, parses every
named field of the PASS records via `scripts/bsim_stage1_parse.py`
(no PASS-substring-only validation), enforces pairwise determinism,
pinned hashes/PLC deltas/totals, and prints the final matrix and known
hash tables.  Logs live in one private `mktemp -d` root: cleaned on
success, preserved on failure or `BSIM_KEEP_LOGS=1`.

## Scenario matrix and results

Each row: exact scenario, BST test IDs (receiver = client), runs per
gate, receiver PASS summary.  All runs also require client PASS with
exact send counts and ASCS response counts, zero process exits, zero
fault markers (with per-scenario allowlists), and PACS contexts never
NONE (`pacs=1`).

| # | Scenario | Runs | Receiver PASS record (full/L/R hashes, counts) |
|---|----------|------|-----------------------------------------------|
| 1 | `mono_10ms` | 2 | pushes=100 szero=8 splc=plc=8 total=108 derr=0 mal=0 after=0; h=0xD65641A8, L==R=0x08D96D5C |
| 2 | `mono_7p5ms` | 2 | pushes=100 szero=11 total=111; h=0x3CF61E00, L==R=0xC915A389 |
| 3 | `modea_10ms` | 2 | pushes=100 szero=6 splc=12 plc=15 total=215; h=0x7335E317, L=0x08D96D5C, R=0x2AE744DB |
| 4 | `modea_7p5ms` | 2 | pushes=100 szero=9 splc=18 plc=36 total=236; h=0xC05F0EA7, L=0x96275542, R=0x499BD761 |
| 5 | `modea_reverse_start_10ms` | 2 | identical to scenario 3 (0x7335E317 / 0x08D96D5C / 0x2AE744DB) |
| 6 | `modeb_10ms` | 2 | pushes=100 szero=8 splc=plc=16 total=216; h=0x7335E317, L=0x08D96D5C, R=0x2AE744DB |
| 7 | `modeb_7p5ms` | 2 | pushes=100 szero=11 splc=plc=22 total=222; h=0xE18E30AE, L=0xC915A389, R=0xC4FEFADB |
| 8 | `invalid_sdu_resume_10ms` | 2 | pushes=100, malformed-SDU evidence exactly 1 (obs_mal=1, derr=1); h=0xB29C3A18, L==R=0x2ECAC1C7 |
| 9 | `modea_first_stop_10ms` | 1 | pushes=24, after=0, gate closed 1, closed-gate receives 10, release cleanup 1; h=0x66B39174 |
| 10 | `release_without_disable_10ms` | 1 | pushes=47, after=0, gate closed 1, release cleanup 1, disconnect cleanup 1; h=0x33651EB3 |
| 11 | `disconnect_streaming_10ms` | 1 | pushes=54, after=0, adv_restart=1, disconnect cleanup 1; h=0x70B513F9 |
| 12 | `reconnect_second_stream_10ms` | 1 | seg1 pushes=75; **seg2 = fresh mono 10 ms oracle: pushes=100, h2=0xD65641A8** (== scenario 1), adv_restart=1 |
| 13 | `unsupported_source_direction` | 1 | obs_rej=1 dir=SOURCE(2) code=CONF_UNSUPPORTED(7) reason=NONE(0), obs_ok=0, no audio |
| 14 | `no_free_sink_slot` | 1 | obs_ok=3 (2 configs + 1 reuse), obs_rej=1 code=NO_MEM(0x0D) reason=NONE, release cleanup 3, no audio |
| 15 | `invalid_codec_fields` | 1 | obs_rej=9 (last: CONF_INVALID(9)/CODEC_DATA(2)), obs_ok=1 (valid mono), no audio |

Notes:

- **Mode A == Mode B full hash** (scenarios 3/5/6): both produce the
  same L (channel 0 pattern) and R (channel 1 pattern) PCM — the decode
  topology differs but the deterministic PCM content is identical.  This
  is a deliberate cross-check of channel identity.
- **Reverse start** (5) is byte-identical to normal Mode A start (3):
  the TX hold begins both streams' transmitted counters at zero and the
  receiver pairs by CIG event, so start order does not change the audio.
- **Reconnect** (12) segment 2 equals a fresh mono 10 ms oracle exactly
  (0xD65641A8), proving fresh decoder/oracle/lifecycle state with no
  stale half/slot/bond state.
- Mono L == R and Mode A/B L != R hold in every audio scenario.

## Exact ASCS response matrix

| Attempt | Scenario | Client-observed response |
|---------|----------|--------------------------|
| Source-direction Config (valid codec) | 13 | `CONF_UNSUPPORTED(0x07) / NONE(0x00)` |
| Sink Config #1, #2 | 14 | `SUCCESS(0x00) / NONE` |
| Sink Config #3 (pool full) | 14 | `NO_MEM(0x0D) / NONE(0x00)` |
| Sink Config after releases (reuse) | 14 | `SUCCESS(0x00) / NONE` |
| Missing frequency | 15 | `CONF_INVALID(0x09) / CODEC_DATA(0x02)` |
| Unsupported frequency (16 kHz) | 15 | `CONF_INVALID / CODEC_DATA` |
| Missing duration | 15 | `CONF_INVALID / CODEC_DATA` |
| Invalid duration encoding (0xFF) | 15 | `CONF_INVALID / CODEC_DATA` |
| Missing octets per frame | 15 | `CONF_INVALID / CODEC_DATA` |
| Octets 19 | 15 | `CONF_INVALID / CODEC_DATA` |
| Octets 121 | 15 | `CONF_INVALID / CODEC_DATA` |
| Explicit frame blocks 2 | 15 | `CONF_INVALID / CODEC_DATA` |
| Channel allocation with 3 channels | 15 | `CONF_INVALID / CODEC_DATA` |
| Valid mono (after the nine failures) | 15 | `SUCCESS / NONE` (proves no slot consumed) |
| Missing optional frame blocks (fallback 1) | 15 | `SUCCESS / NONE` |

## Known hashes (pinned in the runner)

| Scenario | full | L | R |
|----------|------|---|---|
| mono_10ms | 0xD65641A8 | 0x08D96D5C | 0x08D96D5C |
| mono_7p5ms | 0x3CF61E00 | 0xC915A389 | 0xC915A389 |
| modea_10ms | 0x7335E317 | 0x08D96D5C | 0x2AE744DB |
| modea_7p5ms | 0xC05F0EA7 | 0x96275542 | 0x499BD761 |
| modea_reverse_start_10ms | 0x7335E317 | 0x08D96D5C | 0x2AE744DB |
| modeb_10ms | 0x7335E317 | 0x08D96D5C | 0x2AE744DB |
| modeb_7p5ms | 0xE18E30AE | 0xC915A389 | 0xC4FEFADB |
| invalid_sdu_resume_10ms | 0xB29C3A18 | 0x2ECAC1C7 | 0x2ECAC1C7 |
| reconnect_second_stream_10ms (seg2) | 0xD65641A8 | — | — |

These replace the T2/T3 mono-only hashes (0x9225F075 / 0x2011C0F9):
the T4 client uses a deliberately stronger deterministic multi-channel
TX pattern, so new PCM → new hashes.  Baselined from two identical
workstation runs, then pinned; the acceptance runs (twice) and both full
gates reproduced them exactly.

## Lifecycle event/count evidence

- Scenario 9: after ≥20 paired pushes the client disables ASE 0; the
  receiver's gate closes (obs_gate_c=1), pending halves are cleared, the
  second ASE's remaining SDUs hit the closed gate (obs_blk=10), releases
  complete cleanly (obs_rel=1), zero pushes after close (after=0).
- Scenario 10: Release directly from streaming returns SUCCESS; gate
  closes, offload/sink stop exactly once (idempotent), slot cleaned and
  reusable, zero pushes after close, clean disconnect.
- Scenario 11: disconnect while streaming: gate/decoder/lifecycle/sink/
  offload cleanup, zero late pushes (after=0), advertising restarted
  (adv_restart=1).
- Scenario 12: two sessions over one simulation; the second session
  produces a fresh mono oracle (see above).

## Production defects fixed

1. **Codec-shape validation did not store the validated octets per
   frame.**  The validator reused its scratch return value after the
   channel-allocation lookup, storing 0 — silently disabling the exact
   SDU length check.  (Fixed; the scenario-8 malformed-SDU evidence and
   the SDU-length contract now run for every stream.)
2. **Mode A pairing could not match across CISes.**  The SW Split LL
   numbers each CIS from a CIG-global counter, so the two CIS seq spaces
   carry a constant offset (their activation delay) that no TX hold can
   remove; exact-seq or per-half-index pairing never pairs (perpetual
   stale-half discards).  Pairing now uses the ISO SDU reference time
   (BT_ISO_FLAGS_TS): both CISes of one CIG share the reference at each
   event, so equal `half_ts` identifies the two halves of the same audio
   frame; a wrap-safe 32-bit comparison discards only the older half.
3. **Release from streaming crashed the server.**  The release path
   memset the `bt_bap_stream` struct; the ASCS server's streaming-exit
   transition dereferences `stream->iso` after the application release
   callback returns (SEGV in `bt_bap_remove_iso_data_path`).  Release now
   clears only app-owned slot state; the server owns conn/ep/codec_cfg/
   iso and clears them at the ASE idle transition.
4. **Malformed-SDU injection size.**  A one-byte SDU is dropped by the
   ISO stack before the BAP callback (never observable receiver-side);
   the injected malformed SDU is one byte short of the configured shape
   (119 of 120) — still an exact-length violation that reaches the
   receiver's payload validation.

## Deviations from the handoff

- The injected malformed SDU is **119 bytes, not 1 byte** (deviation 4
  above): a 1-byte SDU never reaches the BAP layer in the SW Split ISO
  stack.  The observable contract (exactly one malformed-SDU observer
  event, one decode-error increment, no push, then resume) is unchanged.
- Mode A normal scenarios carry a small deterministic number of
  **post-start PLC frames** (3 for 10 ms, 18 for 7.5 ms) whose
  concealment output is nonzero — indistinguishable from valid audio and
  inaudible; the strict runner pins the exact PLC delta per scenario
  instead of requiring zero.  Zero-energy (silent) pushes after the
  first 20 nonzero pushes remain an immediate fault.
- The stop-finalized segments (scenarios 9–12) carry a bounded number of
  unpaired-half decodes (CIS activation skew / pairing cut mid-frame);
  the exact total decoder invocations are pinned per scenario rather
  than asserted as exactly `dec_calls × pushes`.
- The ASCS server emits a cosmetic `Invalid application error code: 9`
  warning when the application returns `CONF_INVALID` (not in Zephyr's
  allowed app-rsp list); the wire response is exactly what the app
  chose.  Allowlisted for `invalid_codec_fields`.
- Scenario 15's nine invalid variants run as three rounds of three
  attempts on fresh connections (one attempt per endpoint per
  connection): a rejected Config leaves the client endpoint attached and
  there is no public detach API for an idle ASE.  The disconnect
  releases every client endpoint.

## Runtime and gate duration

- Matrix (22 simulations): ~35 min wall on `thomas-workstation`
  (compiles included); each simulation runs 80 s simulated time.
- Full gate (`./scripts/test-all.sh`): 26 children, ~45–55 min wall,
  of which the matrix is the long child.
- No physical RF/audio hardware was used anywhere in this phase.

## Acceptance evidence

- Matrix with pinned hashes: **PASS twice consecutively** on
  `thomas-workstation` (`/tmp/t4-val` detached worktree of the exact
  final commit, bundle-transferred, `thomas-workstation` repo main never
  modified).
- Full gate: **26 PASS / 0 FAIL / 26 TOTAL, twice consecutively** on the
  same worktree.
- Production builds on the final commit: `fw-build-5340`,
  `fw-build-54l15`, `fw-build-dongle` — zero compiler errors/warnings
  (documented non-actionable Kconfig/CMake/DT diagnostics only).
- `git diff --check` clean.
- Desktop compile of both BSim binaries: zero warnings.
- Parser unit tests: 36/36 (`tests/unit/bsim_runner`).
