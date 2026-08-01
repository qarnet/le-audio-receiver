# T4 — BAP and Bluetooth behavior matrix (BabbleSim)

Phase T4 expands the accepted BabbleSim gate from one mono scenario into
the full BAP matrix: real `src/bt_bap.c`, `src/audio_decode.c`, real
Zephyr BAP/ASCS/PACS, real ISO transport, and real liblc3 encode/decode.
Channel routing, codec rejection, teardown, disconnect, and reconnect are
locked by 15 strict scenarios executed twice (scenarios 1–8) or once
(9–15) per gate.

The T4 review-fix round corrected the ASCS rejection response
(`CONF_REJECTED`), added Mode A ISO timestamp validation, stopped the
sink immediately on Release (with an ordering proof), corrected the
ordered FNV hashes (uint16_t sample conversion, FNV offset basis per
segment), introduced a strict source-valid startup boundary with zero
post-start PLC, synchronized the custom TX cross-thread state, removed
every warning allowlist, and enabled warnings-as-errors.

Accepted on the exact final T4 code commit `8542f1a`
(`tests: pin post-review-fix FNV hashes and totals`) on
`test/pre-refactor-behavior` — the matrix passed **four consecutive
identical runs** (two post-fix baselines, two pinned acceptances) and
the full gate passed **26 PASS / 0 FAIL / 26 TOTAL** once (per the
review-fix handoff, one full gate suffices because the matrix is the
gate's long BSim child and no non-BSim code changed after it).

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
    --receiver R.log --client C.log --known-full 0x22AB5C0D
```

The runner acquires a `flock` on `${ZEPHYR_BASE}/bsim_out` (shared
build/run tree), compiles both binaries once per gate, runs every
simulation with receiver/client/PHY exit-zero enforcement, parses every
named field of the PASS records via `scripts/bsim_stage1_parse.py`
(no PASS-substring-only validation), enforces pairwise determinism,
pinned hashes/totals, and prints the final matrix and known
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
| 1 | `mono_10ms` | 2 | pushes=100 trans=8 szero=8 splc=plc=8 total=108 derr=0 mal=0 after=0 mts=0; h=0x22AB5C0D, L==R=0x32777D65 |
| 2 | `mono_7p5ms` | 2 | pushes=100 trans=11 total=111; h=0x01A3EB05, L==R=0x30F0308C |
| 3 | `modea_10ms` | 2 | pushes=100 trans=6 splc=plc=15 total=215; h=0xBAE24F7E, L=0x32777D65, R=0xD3EE3722 |
| 4 | `modea_7p5ms` | 2 | pushes=100 trans=9 splc=plc=22 total=236; h=0x00A5D3F9, L=0xEE461704, R=0x37E155C8 |
| 5 | `modea_reverse_start_10ms` | 2 | identical to scenario 3 (0xBAE24F7E / 0x32777D65 / 0xD3EE3722) |
| 6 | `modeb_10ms` | 2 | pushes=100 trans=8 splc=plc=16 total=216; h=0xBAE24F7E, L=0x32777D65, R=0xD3EE3722 |
| 7 | `modeb_7p5ms` | 2 | pushes=100 trans=11 splc=plc=22 total=222; h=0xFF82CADB, L=0x30F0308C, R=0x129591EE |
| 8 | `invalid_sdu_resume_10ms` | 2 | pushes=100, malformed-SDU evidence exactly 1 (obs_mal=1, derr=1); h=0x0C61918D, L==R=0x7FFE087A |
| 9 | `modea_first_stop_10ms` | 1 | pushes=35, after=0, gate closed 1, closed-gate receives 10, release cleanup 1; h=0x5A025240 |
| 10 | `release_without_disable_10ms` | 1 | pushes=48, after=0, gate closed 1, release cleanup 1, **sink stopped on Release (obs_rel_ss=1, rel_ss_seq=3 < disc_seq=5)**, disconnect cleanup 1; h=0xAEBD23A1 |
| 11 | `disconnect_streaming_10ms` | 1 | pushes=55, after=0, adv_restart=1, disconnect cleanup 1; h=0x8500C966 |
| 12 | `reconnect_second_stream_10ms` | 1 | seg1 pushes=55; **seg2 = fresh mono 10 ms oracle: pushes=100, h2=0x22AB5C0D** (== scenario 1), adv_restart=1 |
| 13 | `unsupported_source_direction` | 1 | obs_rej=1 dir=SOURCE(2) code=CONF_UNSUPPORTED(7) reason=NONE(0), obs_ok=0, no audio |
| 14 | `no_free_sink_slot` | 1 | obs_ok=3 (2 configs + 1 reuse), obs_rej=1 code=NO_MEM(0x0D) reason=NONE, release cleanup 3, no audio |
| 15 | `invalid_codec_fields` | 1 | obs_rej=9 (last: CONF_REJECTED(8)/CODEC_DATA(2)), **obs_ok=2 (valid mono + missing-frame-blocks fallback)**, no audio |

## Exact ASCS response matrix

| Attempt | Scenario | Client-observed response |
|---------|----------|--------------------------|
| Source-direction Config (valid codec) | 13 | `CONF_UNSUPPORTED(0x07) / NONE(0x00)` |
| Sink Config #1, #2 | 14 | `SUCCESS(0x00) / NONE` |
| Sink Config #3 (pool full) | 14 | `NO_MEM(0x0D) / NONE(0x00)` |
| Sink Config after releases (reuse) | 14 | `SUCCESS(0x00) / NONE` |
| Missing frequency | 15 | `CONF_REJECTED(0x08) / CODEC_DATA(0x02)` |
| Unsupported frequency (16 kHz) | 15 | `CONF_REJECTED / CODEC_DATA` |
| Missing duration | 15 | `CONF_REJECTED / CODEC_DATA` |
| Invalid duration encoding (0xFF) | 15 | `CONF_REJECTED / CODEC_DATA` |
| Missing octets per frame | 15 | `CONF_REJECTED / CODEC_DATA` |
| Octets 19 | 15 | `CONF_REJECTED / CODEC_DATA` |
| Octets 121 | 15 | `CONF_REJECTED / CODEC_DATA` |
| Explicit frame blocks 2 | 15 | `CONF_REJECTED / CODEC_DATA` |
| Channel allocation with 3 channels | 15 | `CONF_REJECTED / CODEC_DATA` |
| Valid mono (after the nine failures) | 15 | `SUCCESS / NONE` (proves no slot consumed) |
| Missing optional frame blocks (fallback 1) | 15 | `SUCCESS / NONE` |

## Known hashes (pinned in the runner)

FNV-corrected ordered hashes (FNV offset basis per segment; each signed
sample converted to `uint16_t` before byte extraction; full/L/R prepend
the 4-byte LE frame index then channel sample bytes).  Baselined from
two pairwise-identical post-fix workstation runs and reproduced exactly
by the two pinned runs and the full gate.

| Scenario | full | L | R |
|----------|------|---|---|
| mono_10ms | 0x22AB5C0D | 0x32777D65 | 0x32777D65 |
| mono_7p5ms | 0x01A3EB05 | 0x30F0308C | 0x30F0308C |
| modea_10ms | 0xBAE24F7E | 0x32777D65 | 0xD3EE3722 |
| modea_7p5ms | 0x00A5D3F9 | 0xEE461704 | 0x37E155C8 |
| modea_reverse_start_10ms | 0xBAE24F7E | 0x32777D65 | 0xD3EE3722 |
| modeb_10ms | 0xBAE24F7E | 0x32777D65 | 0xD3EE3722 |
| modeb_7p5ms | 0xFF82CADB | 0x30F0308C | 0x129591EE |
| invalid_sdu_resume_10ms | 0x0C61918D | 0x7FFE087A | 0x7FFE087A |
| reconnect_second_stream_10ms (seg2) | 0x22AB5C0D | — | — |

These replace the pre-review hashes (e.g. mono 10 ms 0xD65641A8): the
review-fix oracle corrected the FNV initialization (offset basis at
every segment start) and the signed-sample byte extraction (uint16_t
conversion), which deliberately changes every audio hash.  No other
behavior changed them — the TX synchronization rewrite produced
byte-identical hashes (verified with focused runs before the baselines).

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

## Production defects fixed (original T4 + review-fix round)

1. **Codec-shape validation did not store the validated octets per
   frame.**  The validator reused its scratch return value after the
   channel-allocation lookup, storing 0 — silently disabling the exact
   SDU length check.  (Fixed; the scenario-8 malformed-SDU evidence and
   the SDU-length contract now run for every stream.)
2. **Mode A pairing could not match across CISes.**  The SW Split LL
   numbers each CIS from a CIG-global counter, so the two CIS seq spaces
   carry a constant offset (their activation delay) that no TX hold can
   remove; exact-seq or per-half-index pairing never pairs.  Pairing now
   uses the ISO SDU reference time (`BT_ISO_FLAGS_TS`), equal on both
   CISes of one CIG at each event, with a wrap-safe 32-bit comparison
   discarding only the older unmatched half.
3. **Release from streaming crashed the server.**  The release path
   wiped `stream->iso`; the ASCS server's streaming-exit transition
   dereferences it after the application release callback returns (SEGV
   in `bt_bap_remove_iso_data_path`).  Release now clears only app-owned
   slot state and lets the server detach at the ASE idle transition.
4. **Malformed-SDU injection size.**  A one-byte SDU is dropped by the
   ISO stack before the BAP callback (never observable receiver-side);
   the injected malformed SDU is one byte short of the configured shape
   (119 of 120) — still an exact-length violation.
5. **Invalid application ASCS response.**  `CONF_INVALID` is excluded
   from the ASCS application response codes (NCS v3.3.0 `bap.h`,
   `ascs.c` warns); missing/invalid codec shapes now return
   `CONF_REJECTED / CODEC_DATA`.  The expected negative remote-request
   paths (unsupported source, rejected codec shape, pool full, malformed
   SDU) log at INFO level, so the strict runner rejects every remaining
   bt_bap/ASCS warning with no allowlists.
6. **Mode A timestamp validation.**  A VALID-flag SDU missing the ISO TS
   flag skips decoder/pairing/push, increments a receive/decode fault
   counter, emits a test observer event, and logs a real warning — zero
   occurrences in the matrix (the ISOAL's `BT_ISO_FLAGS_LOST`
   sync-boundary SDUs carry no TS by definition and keep the
   concealment/startup-transient path).
7. **Release did not stop the sink immediately.**  Release now calls
   `audio_sink_stop()` before returning to ASCS when it closes an open
   audio path; a passive observer event with a monotonic sequence number
   proves the stop precedes the ACL disconnect (scenario 10:
   `rel_ss_seq=3 < disc_seq=5`).
8. **Custom TX cross-thread races.**  Per-slot `generation` and
   `in_flight` counters under one mutex: the TX candidate snapshot
   increments `in_flight` and records generation/stream/seq under the
   lock, decrements on every path after unlock, and commits counters
   only when generation and stream still match; register selects only
   `bap_stream == NULL && in_flight == 0` slots, initializes encoders
   while protected, reassigns a nonzero generation, and publishes
   `bap_stream` last (the memset can never race an in-flight send);
   unregister/pause clear state under the lock then wait for
   `in_flight == 0` without holding it.  The lock is never held across
   `net_buf_alloc`/`bt_bap_stream_send`.

## Strict startup boundary and zero post-start PLC

The production receive path reports per-push source validity to the test
oracle immediately before every sink push (mono/Mode B: the packet
`BT_ISO_FLAGS_VALID`; Mode A: both paired halves had VALID set).  The
oracle's startup phase stays open until the first nonzero push sourced
entirely from valid ISO input; while open, zero or nonzero concealment
counts as a startup transient and `startup_plc` updates after each push
(including the boundary-closing push, which absorbs the interleaved
sync-boundary LOST decodes).  After the boundary: any source-invalid
push, any zero-energy push, and any post-start PLC (`plc_frames !=
startup_plc` at finalize) is an immediate fault.  Every scenario reports
`plc == splc` exactly — the Mode A sync-boundary PLCs (previously pinned
as deltas 3/18) moved into the startup evidence and the steady-state
delta is zero.  The arbitrary 20-push zero-energy grace and the PLC-delta
pin table are removed.

## Warning policy and warnings-as-errors

The runner compiles both BSim binaries with Zephyr's default
warnings-as-errors (no `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n`) and
rejects every `<wrn> bt_bap:`, `<wrn> bt_ascs:`, and `<err> bt_bap:`
line with no scenario allowlist; the expected control paths log at INFO.
Final builds show zero repo compiler warnings.

## Runtime and gate duration

- Matrix (22 simulations): ~35 min wall on `thomas-workstation`
  (compiles included); each simulation runs 80 s simulated time.
- Full gate (`./scripts/test-all.sh`): 26 children, ~45–55 min wall,
  of which the matrix is the long child.
- No physical RF/audio hardware was used anywhere in this phase.

## Acceptance evidence (T4 review-fix round)

- Focused hash-equivalence: after the TX synchronization rewrite,
  mono_10ms / modea_10ms / release_without_disable hashes were
  byte-identical to the diagnostic baseline (0x22AB5C0D / 0xBAE24F7E /
  0xAEBD23A1), proving the sync fix changes no PCM.
- Two post-fix baseline matrices: **PASS, pairwise identical fields**
  (`/tmp/t4_rf_bl1.log`, `/tmp/t4_rf_bl2.log` on the workstation).
- Two pinned matrices: **PASS** (`/tmp/t4_rf_pin1.log`,
  `/tmp/t4_rf_pin2.log`).
- Parser unit tests: **33/33** (`tests/unit/bsim_runner`).
- Full gate on the exact final code commit `8542f1a`: **26 PASS / 0
  FAIL / 26 TOTAL** (one full gate suffices per the review-fix handoff:
  the matrix is the gate's long BSim child and no non-BSim code changed
  after it).
- Production builds `fw-build-5340`, `fw-build-54l15`,
  `fw-build-dongle`: zero errors/warnings (documented non-actionable
  Kconfig/CMake/DT diagnostics only).
- `git diff --check` clean; both BSim binaries compile with
  warnings-as-errors, zero repo warnings.
- Worktree/repo cleanup: workstation returned to clean `main` with all
  temporary refs/worktrees/bundles removed.
- **T5 is next**: timing/actuator internals and the broad lifecycle
  matrix.
