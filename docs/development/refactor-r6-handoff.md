# R6 handoff — BAP receive-pipeline decomposition

Start commit: `860ea02` (R5 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before implementation; the implementation must match it and the
results document (`docs/development/refactor-r6-results.md`) must record
what actually happened.

## Goal / invariant

Reduce `src/bt_bap.c` (~1673 lines) to Bluetooth service/lifecycle
orchestration.  New app-owned `src/audio_stream_session.{c,h}` becomes the
**exclusive owner** of app audio receive/session state and of the
decode/conceal/volume/push mechanics.  Preserve every BSim hash/count,
callback order, malformed/gap mutation order, stream behavior, counter,
log/observer event, timing update, and ASCS stack ownership exactly.
No BSim pin change; no mixed-duration feature; no 360-frame FLPR.

`stream_lifecycle.c` stays the pure configured/started/audio-path gate;
`bt_bap.c` still composes lifecycle decisions with session decode state
(teardown coordination is R7).

## Hard ownership split

### `bt_bap.c` retains (never moves)

- plain `struct bt_bap_stream` pool (`sinks[]`) + `stream_ops` registration;
- ASCS callbacks (`lc3_config/qos/enable/start/metadata/disable/stop/
  release`), config response codes, `qos_pref`, `validate_codec_cfg()`;
- `bt_conn`/pairing/PACS/advertising, `default_conn`, `sem_disconnected`;
- `lifecycle_lock`, `audio_path_generation`, gate orchestration
  (`sink_close_audio_path`, `sink_release_slot`), shell `bt_bap_audio_path_stop`;
- timing-reference update in the recv adapter (`idx==0 && valid && has_ts &&
  gate_open` → `audio_timing_sdu_ref_update(info->ts, session_pd(0))`);
- ISO callback perf wrap (`AUDIO_PERF_PATH_ISO_RECV` start/end) and the
  gate-blocked log throttle + `bsim_observer_recv_gate_blocked()`.

### Session owns (moves into session)

- validated codec shape per slot (set at Config, revalidated at Enable);
- decoder ctx + static memories (`struct audio_decode_ctx`);
- per-slot `struct audio_iso_seq`;
- shared Mode A `modea_state`/`modea_event`, `l_buf`/`r_buf`/`stereo_out`;
- configured occupancy, `pd_us`, `recv_cnt`, configured count;
- mode inference (MONO / MODEB / MODEA);
- malformed-SDU rejection (exact order: seq update, then length check),
  omitted-callback synthesis (PLC push vs synthetic LOST sentinel),
  decode/conceal/volume/push and the corresponding stats/perf/observer
  calls (`audio_stats_*`, `audio_perf_*` decode wrap + `push_failure`,
  `bsim_observer_pre_push/malformed_sdu/stale_half/missing_ts`).

### Session NEVER owns or clears

`struct bt_bap_stream`, `conn`, `ep`, `codec_cfg`, `qos`, `iso`.  The recv
adapter copies scalar `info`/`buf` data only (`valid`, `has_ts`, `ts`,
`seq_num`, `data`, `len`); the session retains no `bt_iso_recv_info` or
`net_buf` pointers.

## Session API (exact)

`src/audio_stream_session.h` — kernel/BT-neutral where possible (stdint/
stddef/stdbool; kernel include only for the CONFIG_ZTEST test accessors):

```c
#if defined(CONFIG_BSIM_SINK_POOL_LIMIT)
#define AUDIO_STREAM_SESSION_MAX_SLOTS CONFIG_BSIM_SINK_POOL_LIMIT
#else
#define AUDIO_STREAM_SESSION_MAX_SLOTS CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT
#endif
/* Invariant: session slots must match the lifecycle gate's fixed 2. */
BUILD_ASSERT(AUDIO_STREAM_SESSION_MAX_SLOTS == 2);

struct audio_stream_codec_shape {
	uint16_t freq_hz;
	uint16_t frame_dur_us;
	uint16_t octets_per_frame;
	uint8_t  frame_blocks_per_sdu;
	uint8_t  chan_count;
};

enum audio_stream_mode {
	AUDIO_STREAM_MODE_MONO = 0,  /* 1 ASE, chan_count 1 */
	AUDIO_STREAM_MODE_MODEB,     /* 1 ASE, chan_count 2 */
	AUDIO_STREAM_MODE_MODEA,     /* 2 ASEs, both mono */
};

int  audio_stream_session_init(void);
int  audio_stream_session_config(size_t idx, const struct audio_stream_codec_shape *shape);
int  audio_stream_session_qos(size_t idx, uint32_t pd_us);
int  audio_stream_session_enable(size_t idx);
void audio_stream_session_disable(size_t idx);
void audio_stream_session_start_clear(void);   /* assembler + all seqs reset */
void audio_stream_session_release(size_t idx);
void audio_stream_session_reset_all(void);
void audio_stream_session_rx_open(void);
void audio_stream_session_rx_close(void);
int  audio_stream_session_recv(size_t idx, bool valid, bool has_ts, uint32_t ts,
			       uint16_t seq, const uint8_t *data, size_t len);
size_t audio_stream_session_recv_valid_count(size_t idx); /* valid-recv counting, returns new count */
void audio_stream_session_recv_reset(size_t idx);         /* stream_started per-slot zero */

bool audio_stream_session_configured(size_t idx);
size_t audio_stream_session_configured_count(void);
const struct audio_stream_codec_shape *audio_stream_session_shape(size_t idx);
enum audio_stream_mode audio_stream_session_mode(size_t idx);
uint32_t audio_stream_session_pd(size_t idx);
size_t audio_stream_session_recv_count(size_t idx);
```

Public scalar APIs return exact `0`/`-EINVAL`; invalid slot indices and
NULL shapes are handled safely (bounds-checked, never dereferenced) and
test-pinned.  `recv()` returns `0` on every handled path and `-EINVAL` on
admission-closed / invalid-slot / not-configured rejection.

CONFIG_ZTEST-only accessors (GCOVR_EXCL_START/STOP, stripped from the
public API inventory like `audio_perf_test_inject_cycles`):
`audio_stream_session_test_lock_try/release`, `test_in_flight`,
`test_admission_open`.

## Private state + lease

```c
struct slot {
	bool configured;
	struct audio_stream_codec_shape shape;
	uint32_t pd_us;
	size_t recv_cnt;
	struct audio_decode_ctx decode;   /* CONFIG_LIBLC3: + static mem */
	struct audio_iso_seq seq;
};
static struct {
	struct slot slots[AUDIO_STREAM_SESSION_MAX_SLOTS];
	size_t configured_count;
	struct modea_state modea;
	struct modea_event modea_ev;
	int16_t l_buf[SAMPLES_PER_CHANNEL_MAX];   /* CONFIG_LIBLC3 */
	int16_t r_buf[SAMPLES_PER_CHANNEL_MAX];
	int16_t stereo_out[STEREO_OUT_MAX];
	struct k_mutex mutex;
	bool admission_open;    /* only rx_open() enables */
	uint32_t generation;    /* bumped on every rx_close */
	size_t in_flight;
	struct k_condvar drained;
} s;
```

- **RX acquire** (inside `recv()`): under `mutex` reject if
  `!admission_open || idx >= MAX || !configured`; increment `in_flight`;
  unlock.  All decode/push/observer work runs outside the lock.  Every
  exit path re-locks, decrements `in_flight`, broadcasts `drained` when it
  reaches zero, unlocks.
- **Teardown** (`rx_close()`): lock; `admission_open = false`;
  `generation++`; `while (in_flight > 0) k_condvar_wait(&drained, &mutex,
  K_FOREVER)`; unlock.  Idempotent.  Only the reset of decoder/assembler/
  sequence state runs after `rx_close()` returns (all admitted RX drained).
- **Lock ordering**: adapters hold `lifecycle_lock` → session lock when
  nested; session APIs never acquire `lifecycle_lock`.  `rx_close()` is
  called outside the `lifecycle_lock` hold (the drain wait must never run
  under the lifecycle lock).
- **Self-deadlock**: production recv callbacks and teardown callbacks share
  the BT RX WQ (`CONFIG_BT_RECV_WORKQ_BT=y`) so an in-callback `rx_close()`
  sees `in_flight == 0`.  Shell-thread `rx_close()` waits only for an
  in-flight WQ recv lease, which completes without blocking.  Adapters call
  `rx_close()` only outside an active session lease.  No session API is
  ever called from ISR.
- **Admission semantics (LIFE-006 preserved)**: `config/release/reset_all`
  never reopen admission.  Only `rx_open()` at the successful
  `stream_started` gate-open edge enables recv admission; `rx_close()`
  stays closed until that edge.  A recv callback that passes the adapter's
  gate snapshot but loses a race to a concurrent close is rejected by the
  session admission check (`-EINVAL`, no decode, no observer event, no
  state mutation).

## Callback mapping (mechanical, exact order preserved)

| bt_bap site | session call / retained behavior |
|---|---|
| `lc3_config` success | `validate_codec_cfg()` fills local `struct audio_stream_codec_shape`; `audio_stream_session_config(idx, &shape)` (stores shape, zeroes recv_cnt/pd, resets slot decode ctx + seq, `configured_count++`); log `ASE[%zu] configured: num_sink_ase=%zu chan_count=%u freq=%u dur=%u octets=%u` using `configured_count()`/`shape()`; then lifecycle `stream_lifecycle_sink_configured(idx)` under `lifecycle_lock`; observer + pref unchanged |
| `lc3_qos` | `audio_stream_session_qos(idx, qos->pd)` |
| `lc3_enable` | revalidate retained `stream->codec_cfg` against stored shape (bt_bap, exact CONF_REJECTED/CODEC_DATA on mismatch); then `audio_stream_session_enable(idx)` = `audio_decode_config()` + `modea_config()` + `audio_sink_set_input_frames()` + `LC3 decoder[%zu]` log |
| `lc3_start` | `audio_stream_session_start_clear()` (was `mode_a_halves_clear`) |
| `lc3_metadata` | unchanged (log only) |
| `lc3_disable` | `audio_stream_session_disable(idx)` (decoder/decoder_r = NULL; shape/configured kept) |
| `lc3_stop` | `sink_close_audio_path(false)` (unchanged gate orchestration; it now also closes session admission + clears assembler on the non-forced path) |
| `lc3_release` → `sink_release_slot` | `sink_close_audio_path(false)`; `audio_sink_stop()` + `bsim_observer_release_sink_stop()` if was_open; `audio_stream_session_release(idx)` (decoder/seq reset, recv_cnt/pd/shape cleared, `configured_count--`); lifecycle `stream_lifecycle_sink_release(idx)`; `bsim_observer_cleanup_release(idx)` |
| `stream_recv` adapter | perf start; `idx`/`valid`/`has_ts`; gate snapshot under `lifecycle_lock`; timing update `idx==0 && valid && has_ts && gate_open` with `audio_stream_session_pd(0)`; `if (valid) { cnt = audio_stream_session_recv_valid_count(idx); periodic log }` else `LOG_DBG`; `if (!gate_open) { throttle; observer recv_gate_blocked if valid; perf end; return; }`; `audio_stream_session_recv(idx, valid, has_ts, info->ts, info->seq_num, buf->data, buf->len)`; perf end once |
| `stream_started` | `audio_stream_session_recv_reset(idx)` at entry (was `sinks[idx].recv_cnt = 0U`); open edge: `audio_stream_session_start_clear()` (was `mode_a_halves_clear`) then `audio_stream_session_rx_open()` (before perf reset/offload start), rest unchanged; failed/stale open ensures admission closed (stale → `audio_stream_session_rx_close()`) |
| `stream_disabled_cb` | summary uses `audio_stream_session_recv_count(idx)`; rest unchanged |
| `stream_stopped` | `sink_close_audio_path(false)` unchanged |
| `disconnected` | `sink_close_audio_path(false)`; lifecycle reset; offload stop; `audio_stream_session_reset_all()` (after close/drain); sink stop; stats reset; observer cleanup_disconnect |
| `bt_bap_audio_path_stop` (shell) | `sink_close_audio_path(true)` (forced: closes gate + session admission, NO assembler/seq clear), `audio_sink_stop()`, offload stop |
| `bt_bap_init` | `audio_stream_session_init()` before stream-cb registration |

`sink_close_audio_path()` order (both forced and normal): close gate under
`lifecycle_lock` (+generation, `audio_sink_stream_close()`), unlock, then
`audio_stream_session_rx_close()` (drain, outside lock), then the existing
was-open observer/offload work, then (non-forced only)
`audio_stream_session_start_clear()`.

## Mode inference (exact)

For slot `idx`: `shape.chan_count >= 2` → MODEB; else
`configured_count >= 2` → MODEA; else MONO.  This reproduces bt_bap's
`decode.chan_count >= 2` / `num_sink_ase >= 2` checks exactly at recv time.
Sequence-gap path: MODEB/MONO → one `mode_plc_push` per omitted SDU;
MODEA → one synthetic LOST sentinel per omitted SDU fed to
`mode_a_store_and_process` before the current half.

## stream_lifecycle narrowing

`stream_lifecycle_sink_configured(size_t idx, int chan_count)` →
`stream_lifecycle_sink_configured(size_t idx)`; `sink_chan_count[idx]`
becomes `bool occupied[MAX_SINK_ASE]`; every `chan_count > 0` test becomes
`occupied` (gate decision logic unchanged — it already counted only
configured slots, never the chan_count value).  Tests: replace the
`chan_count` argument at every call site; delete the three now-obsolete
tests (`test_zero_chan_count_sink_absent`, `test_negative_chan_count_inert`,
`test_idle_force_close_clears_stale_latch` — the last depends on
`configured(idx, 0)`, which no longer exists); keep all other lifecycle
tests with the new signature; add an occupancy-equivalent where the
deleted tests had unique coverage (released-slot inert start is already
covered by `test_release_then_slot_reuse`).

## Files

- `src/audio_stream_session.c/.h` (new).
- `src/bt_bap.c` (extraction; gate/pairing/ASCS retained).
- `src/stream_lifecycle.c/.h` (occupancy narrowing).
- Root `CMakeLists.txt` (add `src/audio_stream_session.c` unconditionally).
- `tests/bsim/CMakeLists.txt` (add `src/audio_stream_session.c`).
- `tests/unit/lifecycle/` (signature update + obsolete-test removal).
- `tests/unit/audio_stream_session/` (new Twister suite).
- `tests/test-matrix.json` (new direct stateful `audio_stream_session.c`
  entry; lifecycle ledger/witness updates; `bt_bap.c` stays
  integration-only).
- `tests/coverage-baseline.json` + `docs/testing/coverage-matrix.md`
  (baseline migration commit).
- Docs: `AGENTS.md`, `README.md`, `STATUS.md`, `docs/design.md`,
  `docs/testing/behavior-contract.md`, `docs/development/refactor-plan.md`,
  `docs/development/workstation-transfer-status.md`.

## Direct test suite — `tests/unit/audio_stream_session`

Twister (testcase.yaml, `platform_allow: native_sim/native/64`).  Compiles
production `audio_stream_session.c` + `audio_decode.c` + `audio_modea.c` +
`audio_iso_seq.c` + `audio_stats.c` + `audio_perf.c` + `audio_volume.c`
against a faithful fake sink (`audio_sink.h` seam: records pushes, returns
0/-ENOMEM on demand, open/close/set_input_frames) and a fake observer
(`bsim_observer.c` implementation compiled under the suite's local
`CONFIG_BSIM_OBSERVER=y`), with the checked-in LC3 fixtures +
`-Wl,--wrap=lc3_decode` wrapper (reused from `tests/unit/decode`).
`CONFIG_LIBLC3=y`, `CONFIG_AUDIO_PERF_MEASUREMENT=y` (real perf counters for
push-failure accounting), suite-local Kconfig defining
`CONFIG_BSIM_SINK_POOL_LIMIT=2` and `CONFIG_BSIM_OBSERVER`.

Tests (public-boundary observable behavior only, no private-field/
helper-call assertions):

1. config/accessors + invalid slots (NULL shape, out-of-range idx, recv on
   unconfigured slot → `-EINVAL`);
2. mono / Mode B / Mode A classification and valid decoding+push (fixture
   PCM hash or energy checks against the real decode path);
3. common normal + PLC tails and sink-failure accounting
   (`audio_perf` push_failures + fake-sink failure injection);
4. Mode A equal-TS pair and one-sided loss; missing-TS valid rejection
   (observer `missing_ts`, no push, stats decode-error);
5. malformed length: one decode error, no decode/push/ModeA mutation, then
   valid input proceeds (observer `malformed_sdu`);
6. sequence gaps mono/ModeB exact PLC count; Mode A synthetic-lost
   ordering; wrap/resync no synthesis;
7. valid=false PLC; decoder-not-ready skip; hard decode failures (lc3 wrap)
   skip push and consume the event;
8. admission closed → no decode/push; rx_open restores; rx_close blocks
   until an admitted long fake decode/push lease completes, then rejects
   late RX; generation reset; no lock held during decode/sink (concurrent
   thread acquires the session mutex with K_NO_WAIT while the fake sink is
   inside a push);
9. release slot reuse; reset_all clears shape/recv/seq/assembler; reconnect
   starts fresh.

Pure decode/modea/iso_seq suites stay untouched and still pass.

## BSim

`tests/bsim/CMakeLists.txt` adds the session source.  Observer calls move
with their code without reordering.  Full Stage 1 (all 16 scenarios / 25
runs) must retain byte-identical known hashes/counts (mono 10 ms
`0x22AB5C0D`, Mode A/B 10 ms `0xBAE24F7E`, 7.5 ms `0x01A3EB05` /
`0x2D95D15C` / `0xFF82CADB`, reconnect = fresh mono oracle).  Any mismatch
is a blocker until explained; no repin.

## Matrix / coverage

- `tests/test-matrix.json`: new `src/audio_stream_session.c` entry,
  `classification: direct`, `stateful: true`, exact public outcomes with
  witnesses for every public function, state transitions, hardware
  acceptance preserved for bt_bap only.  `stream_lifecycle.c` ledger
  witnesses updated for the new signature.  `bt_bap.c` remains
  integration-only (`excluded_from_numeric: true`).
- Gate children: **29 Twister + 5 exec-only + 12 Python + coverage + matrix
  + BSim = 49**.
- Coverage population 29 → 30: deliberate baseline migration on the clean
  implementation commit (`--write-baseline` to `/tmp`, inspect, copy,
  separate commit).  Lines move from excluded integration `bt_bap.c` into
  included direct `audio_stream_session.c`; no old aggregate comparison is
  possible — document provenance in `docs/testing/coverage-matrix.md`.
  `stream_lifecycle.c` denominator shrinks (removed chan_count storage);
  prove surviving ratio nondecrease and document the removed
  storage/branches.  No exclusions, no threshold weakening.

## G1 (canonical)

`./scripts/test-all.sh` → **49 PASS / 0 FAIL / 49 TOTAL**; coverage
enforcement passes on the migrated baseline; matrix 0 errors;
`fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all PASS with zero
compiler warnings; build contract 76/76; BSim pins unchanged;
`git diff --check` clean; zero actionable warnings.

## G3 hardware (planned non-destructive flash; no recovery)

Exact rows from `docs/testing/pre-refactor-hardware-baseline.md`,
autonomous hci0 central, consoles captured before reset,
`nrf-probes` identity evidence:

- nRF54L15: fresh Mode A 120 s, fresh Mode B 120 s, bonded reconnect
  Mode A 120 s — 12000 central frames, `decode_err=0 i2s_underrun=0
  stream_reset=0`, offload `submit==success`, `fallback=0`.
- nRF5340/E83: fresh Mode A 120 s, fresh Mode B 120 s, bonded reconnect
  Mode B 120 s — same zero errors plus zero `ISO seq gap` /
  `seq discontinuity` / `i2s_nrfx` lines and APLL evidence
  (`Drift state ACTIVE`/`ppm`).
- `bt unpair` (receiver) + central `bluetoothctl remove` only for fresh
  rows; preserve bond for bonded rows.  No audibility claim.  Unique
  `/tmp/r6-*` evidence manifest + SHA-256, raw logs, commands,
  addresses/probe DPIDR/AP/FICR evidence.  Restore normal production
  images at the end.

## Commits (no push/PR/amend/force/attribution)

1. docs: R6 handoff (this document).
2. refactor: session + bt_bap extraction + lifecycle narrowing + CMake +
   BSim + tests + matrix.
3. coverage: baseline migration (29 → 30) + coverage-matrix.md +
   inventory-truth docs.
4. docs: R6 acceptance results (after G1 + G3 pass).

## Non-scope

R7 unified teardown coordinator; mixed-duration support; stack-ownership
changes; codec/protocol changes; 360-frame FLPR offload; BSim repin;
destructive hardware recovery.  Escalate only genuine blockers (two
attempts or contradictory evidence, test weakening, unexplained
hash/coverage/warning, hardware unavailable after safe diagnosis,
destructive action, architecture expansion).  Never commit a knowingly
failing state.
