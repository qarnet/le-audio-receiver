# R7 handoff — stream teardown transition owner

Start commit: `dd4c0a8` (R6 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before implementation; the implementation must match it and the
results document (`docs/development/refactor-r7-results.md`) must record
what actually happened.

## Goal / invariant

Replace the overlapping disable/stop/release/disconnect compositions in
`src/bt_bap.c` with **one explicit private teardown transition owner**:
a single private dispatcher plus small private primitives.  First global
close wins; each app slot releases once independently; a duplicate
release callback for an already-cleaned slot is an observable no-op; ASCS
`struct bt_bap_stream` objects are never touched.  Universal order:

```
close lifecycle gate + sink push admission under lifecycle_lock
  -> release lock
  -> session RX lease drain (audio_stream_session_rx_close)
  -> sink push drain/stop (audio_sink_stop)
  -> offload stop (audio_offload_stream_stop)
  -> state reset
```

No lock spans Bluetooth, decode, offload, or I2S calls.  No new production
file, no new state machine mirroring `stream_lifecycle`, no new global
teardown flags.

**Approved behavioral delta (R7 plan):** the normal BT-close path changes
from offload-before-sink to **sink-drain-before-offload**.  The current
`stream_disabled_cb`/disconnect/release paths stop offload inside
`sink_close_audio_path()` and stop the sink afterwards; the coordinator
stops the sink first, then offload, for both normal and forced paths.
Hardware G3 proves the new order on nRF54L15.  Do not preserve the old
unsafe order.  BSim hashes/counts and every existing pin stay byte-for-byte
identical (the sink-oracle segment is finalized by the first
`audio_sink_stop()` exactly as today; `audio_sink_stop()` never mutates
`audio_stats`, so the disabled summary snapshot is unaffected).

## Coordinator shape (private to bt_bap.c)

One private event enum + context, one private close primitive, one private
dispatcher.  Thin callbacks only translate into events; they do not call
low-level stop/reset APIs.  The dispatcher owns all low-level
session/lifecycle/sink/offload/stats/observer composition.

```c
/* R7: one explicit private teardown transition owner. */
enum teardown_event {
	TEARDOWN_CLOSE,      /* lc3_stop / stream_stopped: normal close, no stats */
	TEARDOWN_DISABLE,    /* lc3_disable: session decoder-disable, serialized */
	TEARDOWN_DISABLED,   /* stream_disabled_cb: normal close + summary + stats reset */
	TEARDOWN_RELEASE,    /* lc3_release(slot) */
	TEARDOWN_DISCONNECT, /* disconnected (default conn): returns advertising-wake bool */
	TEARDOWN_FORCED,     /* shell bt_bap_audio_path_stop */
};

static bool teardown_close_path(bool forced);
static bool teardown_transition(enum teardown_event ev, size_t slot);
```

`stream_started` keeps its accepted R1/R6 transactional open (generation
capture, sink-open failure rollback, stale-open recheck) — see "Start
rollback" below.

### Global close primitive (exact order)

```c
static bool teardown_close_path(bool forced)
{
	bool was_open;

	/* 1. First-edge gate close under the lifecycle lock. */
	k_mutex_lock(&lifecycle_lock, K_FOREVER);
	if (forced) {
		was_open = stream_lifecycle_force_close();
	} else {
		was_open = stream_lifecycle_audio_path_close();
	}
	if (was_open) {
		audio_path_generation++;
	}
	audio_sink_stream_close(); /* nonblocking admission close */
	k_mutex_unlock(&lifecycle_lock);

	/* 2. Session RX lease drain, outside the lifecycle lock. */
	audio_stream_session_rx_close();

	/* 3. First edge only: sink drain/finalize, THEN offload stop. */
	if (was_open) {
		audio_sink_stop();
		audio_offload_stream_stop();
		LOG_INF("Audio path gate CLOSED");
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_gate_close();
#endif
	}

	/* 4. Normal BT close only: clear assembler/sequence after the drain.
	 *    The shell forced path never clears BT-RX-owned state. */
	if (!forced) {
		audio_stream_session_start_clear();
	}
	return was_open;
}
```

Return value = first-edge bool.  Callers/events use it to decide
one-time side effects (`release_sink_stop`, `first disable` log).

### Dispatcher (exact per-event policy)

```c
static bool teardown_transition(enum teardown_event ev, size_t slot)
{
	switch (ev) {
	case TEARDOWN_CLOSE:           /* lc3_stop / stream_stopped */
		teardown_close_path(false);
		return false;

	case TEARDOWN_DISABLE:         /* lc3_disable: session decoder-disable */
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		audio_stream_session_disable(slot);
		k_mutex_unlock(&lifecycle_lock);
		return false;

	case TEARDOWN_DISABLED:        /* stream_disabled_cb */
	{
		bool was_open = teardown_close_path(false);

		if (was_open) {
			LOG_INF("Audio path gate CLOSED (first disable)");
		}

		/* Stream summary: snapshot counters BEFORE the stats reset so
		 * the gate can extract explicit SDUs/decoded/I2S evidence.
		 * Output shape unchanged; audio_sink_stop() never mutates
		 * audio_stats, so the sink-before-offload delta does not
		 * change the summary values. */
		struct audio_stats stats = audio_stats_get();

		LOG_INF("Stream[%zu] summary: SDUs=%zu decoded=%u plc=%u "
			"decode_err=%u i2s_underrun=%u stream_reset=%u",
			slot, audio_stream_session_recv_count(slot), stats.total_frames,
			stats.plc_frames, stats.decode_errors, stats.i2s_underruns,
			stats.stream_resets);

		/* Stats reset once per disabled completion.  No second
		 * unconditional audio_sink_stop on a duplicate disabled event
		 * (the first close already stopped the sink). */
		audio_stats_reset();
		return false;
	}

	case TEARDOWN_RELEASE:         /* lc3_release(slot) */
	{
		/* Duplicate release of an already-cleaned slot is an
		 * observable no-op: no observer, no close, no stats, no
		 * stream touch.  The session configured flag is the
		 * slot-ownership truth (ASCS normally rejects a duplicate
		 * Release PDU before this callback; this guard is the
		 * app-side idempotence boundary).  Check under the fixed
		 * lifecycle -> session lock nesting. */
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		bool configured = audio_stream_session_configured(slot);
		k_mutex_unlock(&lifecycle_lock);
		if (!configured) {
			return false;
		}

		bool was_open = teardown_close_path(false);

		if (was_open) {
			/* Same relative order as today: gate close + sink stop
			 * precede the release observer; the disconnect
			 * cleanup still fires later (rel_ss_seq < disc_seq). */
			LOG_INF("Release: audio sink stopped");
#if defined(CONFIG_BSIM_OBSERVER)
			bsim_observer_release_sink_stop();
#endif
		}
		audio_stream_session_release(slot);
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		stream_lifecycle_sink_release(slot);
		k_mutex_unlock(&lifecycle_lock);
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_cleanup_release(slot);
#endif
		return false;
	}

	case TEARDOWN_DISCONNECT:      /* disconnected (default conn) */
		teardown_close_path(false);
		k_mutex_lock(&lifecycle_lock, K_FOREVER);
		stream_lifecycle_reset();
		k_mutex_unlock(&lifecycle_lock);
		audio_stream_session_reset_all();
		audio_stats_reset();
#if defined(CONFIG_BSIM_OBSERVER)
		bsim_observer_cleanup_disconnect();
#endif
		return true; /* advertising-restart semaphore must fire */

	case TEARDOWN_FORCED:          /* shell bt_bap_audio_path_stop */
		teardown_close_path(true);
		return false;
	}
	return false;
}
```

### Callback mapping (all composition moves into the coordinator)

| bt_bap site | after R7 |
|---|---|
| `lc3_stop` | `teardown_transition(TEARDOWN_CLOSE, sink_idx(stream))` |
| `lc3_disable` | `teardown_transition(TEARDOWN_DISABLE, sink_idx(stream))` |
| `lc3_release` | `teardown_transition(TEARDOWN_RELEASE, sink_idx(stream))` |
| `stream_stopped` | `teardown_transition(TEARDOWN_CLOSE, sink_idx(s))` |
| `stream_disabled_cb` | `teardown_transition(TEARDOWN_DISABLED, sink_idx(s))` (summary + `first disable` log move into the DISABLED case, output unchanged) |
| `disconnected` (default conn only) | `bool wake = teardown_transition(TEARDOWN_DISCONNECT, 0);` then conn unref / `default_conn = NULL`; `if (wake) k_sem_give(&sem_disconnected);` — no direct low-level teardown calls |
| `bt_bap_audio_path_stop` (shell) | `teardown_transition(TEARDOWN_FORCED, 0)` (force latch preserved by `stream_lifecycle_force_close`) |
| `stream_started` | unchanged transactional open (see below) |

### Start rollback

`stream_started` keeps its accepted R1/R6 transaction exactly:

- **Sink-open failure** (closed→open edge but `audio_sink_stream_open()`
  fails): roll the lifecycle gate back to closed and bump the generation
  under the lock; do NOT call sink stop / offload stop ("sink-open
  failure before accepted edge must not invent sink/offload stop").
- **Stale open** (a racing shell force-close or ordinary close invalidated
  the open work): `audio_stream_session_rx_close()` +
  `audio_offload_stream_stop()` — this is the open transaction's own
  rollback (this open started the offload), NOT a teardown event; the
  racing closer already ran the full close (gate closed, so
  `teardown_close_path` would skip the offload stop this open started).

## Event policy (documented decisions)

- `lc3_stop` and `stream_stopped` → `CLOSE` normal; a duplicate (already
  closed gate) has no global side effect.
- `lc3_disable` → `DISABLE`: session decoder-disable transition state,
  serialized under `lifecycle_lock` so config/start/disable/release/
  disconnect share lifecycle serialization (fixed nesting lifecycle →
  session; session APIs never take `lifecycle_lock`).
- `stream_disabled_cb` → `DISABLED`: the first close handles sink/offload;
  exact summary snapshot/log and stats reset once per disabled completion
  are preserved; the old unconditional second `audio_sink_stop()` is gone.
- `lc3_release` → `RELEASE(slot)`: configured check first (no-op for an
  already-cleaned slot); first release runs the global normal close; if the
  release caused the first edge, `release_sink_stop` fires once at the same
  relative order; then `session_release(slot)`, `lifecycle_sink_release(slot)`,
  `cleanup_release(slot)` once each.  The second Mode A slot cleans despite
  the gate already closed.
- `disconnected` (default conn) → `DISCONNECT`: normal global close once;
  lifecycle reset under `lifecycle_lock`; `session_reset_all` after drain;
  stats reset once; `cleanup_disconnect` observer once; return/expose the
  advertising-wake bool (sem decision from the coordinator; the connection
  callback only handles conn unref / `default_conn`).
- Shell `audio stop` → `FORCED`: global forced close; sink then offload
  once on the first edge; no assembler/stats clear; force latch preserved.
- **Stats nuance (documented interpretation of the plan's generic "stats
  once"):** stats reset on `DISABLED` (after the summary) and `DISCONNECT`
  only; `CLOSE`/`RELEASE`/`FORCED` retain stats, because `audio status`
  after a shell stop and release semantics depend on retained stats.  The
  policy lives inside the coordinator, not in callbacks.

## Lock / ownership

- No new global teardown state flags.  Idempotence derives from:
  `stream_lifecycle` first-edge (`audio_path_close`/`force_close` return
  was-open), the session configured flag, the sink stop cohort
  (`audio_sink_stop` overlapping-caller idempotence), and the offload
  generation (stop is idempotent / nRF5340 no-op).
- `lifecycle_lock` remains the only serialization authority; it is held
  only while changing lifecycle/session admission state, never across
  Bluetooth, decode, offload, or I2S calls.
- Session APIs never take `lifecycle_lock`.  Fixed nesting is
  `lifecycle_lock` → session mutex; RX never takes `lifecycle_lock` while
  holding a session lease.
- `audio_stream_session_rx_close()` (the drain wait) runs outside the
  `lifecycle_lock` hold.
- Never clear/overwrite `bt_bap_stream` fields; ASCS detach owns
  conn/ep/codec_cfg/iso (as today).
- Callbacks run on the BT RX thread; the shell forced path may interleave
  (the lifecycle lock + session admission/lease discipline cover it).

## Tests before implementation

Expand the direct lifecycle/session matrices using **public behavior
only** (no private-field/helper-call assertions).  These tests are
committed BEFORE the coordinator so they prove the primitives the
coordinator composes; they must pass on the R6 code unchanged.

### `tests/unit/lifecycle` (5 new tests)

1. `test_mode_a_release_both_slots_cleans_occupancy_no_reopen` — Mode A
   both configured+started (gate open); close → true; `sink_release(0)` →
   occupancy 1; started(0)/started(1) on the released/other slot do not
   open; `sink_release(1)` → occupancy 0; started(0) still false; fresh
   configure(0)+started(0) opens (slot reuse).
2. `test_release_open_gate_both_slots` — Mode A both started (gate open);
   `sink_release(0)` → occupancy 1, gate still open (release never touches
   the gate in the pure lifecycle — the coordinator closes first);
   `sink_release(1)` → occupancy 0; started(0) inert.
3. `test_duplicate_release_same_slot_idempotent` — configure(0),
   started(0) → open; `sink_release(0)`; `sink_release(0)` again →
   occupancy unchanged (0), gate unchanged.
4. `test_force_close_then_release_either_order` — Mode A both started;
   force_close → true (latched); release(1) then release(0) (reverse
   order) → latch cleared by the final release; configure(0)+started(0)
   opens fresh.
5. `test_mixed_close_release_reset_then_reconfigure` — configure(0),
   started(0) → open; close → true; release(0); reset; configure(0),
   started(0) → true; close → true; force_close → false (already closed,
   configured set latched); release(0) → latch cleared; configure(0),
   started(0) → true.

### `tests/unit/audio_stream_session` (6 new tests)

1. `test_release_twice_no_configured_change` — setup_mono; release(0);
   release(0) again → `configured_count` stays 0 (never negative),
   `configured(0)` false, shape NULL, recv 0, pd 0; recv → `-EINVAL`.
2. `test_independent_per_slot_cleanup` — setup_modea; release(0) →
   `configured(1)` still true, count 1; release(1) → count 0.
3. `test_admission_stays_closed_after_release_reset_until_rx_open` —
   setup_mono; rx_close; release(0); reset_all; admission closed; recv →
   `-EINVAL` (unconfigured); config(0)+enable(0); recv → `-EINVAL`
   (admission still closed); rx_open; recv → push (fresh session).
4. `test_release_then_reset_then_reconfigure_fresh` — setup_mono; recv →
   1 push; release(0); reset_all; config(0)+enable(0)+rx_open; recv →
   push count 2, recv_count fresh = 1, no state leakage.
5. `test_duplicate_rx_close_no_deadlock` — setup_mono; rx_close; rx_close
   again (returns, no hang); admission closed; late recv → `-EINVAL`;
   rx_open; recv → push.  (Admitted-lease drain + late-recv rejection are
   already pinned by `test_rx_close_waits_for_admitted_lease`.)
6. `test_modea_first_slot_release_then_second_slot_cleanup` — setup_modea;
   recv pair → 1 push; release(0) → `configured(1)` true, count 1, mode(1)
   MONO; release(1) → count 0.  Mirrors the BSim `modea_first_stop`
   second-slot cleanup while the gate is already closed.

### Matrix (`tests/test-matrix.json`)

- `stream_lifecycle.c`: add the new tests as witnesses (outcomes:
  `sink_release` void → `test_duplicate_release_same_slot_idempotent`;
  transitions: `open->released-both-slots` →
  `test_mode_a_release_both_slots_cleans_occupancy_no_reopen`,
  `force-closed-latched->open-after-reverse-release` →
  `test_force_close_then_release_either_order`).
- `audio_stream_session.c`: add witnesses (outcomes: `release` void →
  `test_release_twice_no_configured_change`; transitions:
  `configured->released-twice` → `test_release_twice_no_configured_change`,
  `released->reset->configured-fresh` →
  `test_release_then_reset_then_reconfigure_fresh`).
- `bt_bap.c` stays integration-only/excluded; update the reason text to
  record the R7 teardown scenarios and the exact strengthened observer
  assertions (see BSim below).

## BSim

### Existing 16 scenarios — pins byte-identical, checks strengthened

Strengthen the strict parser to EXACT observer/count assertions that prove
each R7 item without weakening any existing check (values verified against
a baseline run before pinning; the R6 code already produces these exact
counts):

- normal audio scenarios (mono/modea/modeb/invalid_sdu/one_cis_loss):
  `obs_gate_c == 0`, `obs_rel == 0` (never closed; ends while streaming).
- `modea_first_stop_10ms`: `obs_gate_c == 1` (the first Disable closed the
  gate exactly once; later disable/release events are no-ops),
  `obs_rel == 2` (slot 0 and slot 1 cleanups — the second cleans while the
  gate is already closed), `obs_rel_ss == 0` (no Release caused the first
  edge — the Disable did).
- `release_without_disable_10ms`: `obs_gate_c == 1`, `obs_rel == 1`
  (already partially asserted; make exact), `obs_rel_ss == 1` +
  `rel_ss_seq < disc_seq` (already asserted).
- `disconnect_streaming_10ms`: `obs_gate_c == 1`, `obs_disc == 1`,
  `obs_rel == 0`.
- `reconnect_second_stream_10ms`: `obs_gate_c == 1` (session-1 disconnect
  closed the gate), `obs_disc == 1`, `obs_rel == 0`; second segment still
  equals the fresh mono 10 ms oracle.
- `no_free_sink_slot`: `obs_rel == 3` (2 initial + 1 reuse),
  `obs_gate_c == 0`, `obs_rel_ss == 0`.
- `invalid_codec_fields`: `obs_rel == 2`, `obs_gate_c == 0`,
  `obs_rel_ss == 0`.
- `unsupported_source_direction`: `obs_rel == 0`, `obs_gate_c == 0`.

The `tests/unit/bsim_runner/test_bsim_stage1_parse.py` fixtures for the
strengthened scenarios are updated to the exact contract values (they are
parser-contract fixtures; no existing hash/count pin changes).

### New scenario 17 — `duplicate_release_10ms` (transport-honest)

ASCS evidence (verified in NCS v3.3.0 `bap_stream.c`): a duplicate
`bt_bap_stream_release()` after a completed release returns `-EINVAL`
locally — the client library already detached the stream
(`bt_bap_stream_detach` clears ep/conn after the idle transition), so no
Release PDU is sent and the server's app release callback never fires
again.  This scenario proves the observable no-op at the public boundary
without test-only callback injection; the coordinator's configured check
remains the app-side guard.

Client flow: mono 10 ms stream up (no send cap) → ≥25 sends → first
Release from streaming (SUCCESS rsp; receiver: gate close 1, sink stop 1,
cleanup 1) → duplicate Release of the same stream (client asserts
`-EINVAL`, no rsp) → reconfigure the same endpoint (slot reuse, SUCCESS
rsp) → Release again (SUCCESS rsp; receiver cleanup 2) → disconnect.

Receiver PASS oracle: `seg == 1`, `pushes1 >= 20`, `after == 0`,
`derr1 == 0`, `obs_gate_c == 1`, `obs_rel_ss == 1`, `obs_rel == 2`,
`obs_disc >= 1`.  Client PASS: `sends0 >= 20`, `cfgrsps == 2`,
`relrsps == 2`.  Parser branch asserts all of the above and pins
`known-total` (segment frame total) after a deterministic two-run baseline
(`BSIM_BASELINE=1` twice, identical, then pin).  NEVER alter existing
hashes/counts.  Document the new pin provenance in `stage1-scenarios.json`
notes and the results doc.

Files touched for the new scenario: `tests/bsim/stage1-scenarios.json`
(entry; pin in the separate pin commit), `scripts/bsim_stage1_parse.py`
(parser branch + per-scenario exact checks), `tests/bsim/src/bsim_test_main.c`
(scenario name, `SCENARIO_MAIN`, `test_def`, `scenario_observer_ok`),
`tests/bsim/client/src/bsim_client_main.c` (client enum + scenario
function + `test_def`), `tests/bsim/src/bsim_sink_oracle.h` +
`audio_sink_stub.c` (enum + goal case), `tests/unit/bsim_runner/
test_bsim_stage1_parse.py` (count 16 → 17, fixture updates, new-scenario
fixture, pins in the pin commit), `scripts/test-all.sh` comment
(16 → 17 scenarios).

Gate child count remains 49 (BSim is one child).

## Matrix / coverage

- No production file splits or deletions → **no coverage baseline
  migration expected**.  `bt_bap.c` remains integration-only/excluded.
  New tests may only improve `stream_lifecycle.c` / `audio_stream_session.c`
  ratios; existing files may only improve.  If any ratio decreases, stop
  and report — never rewrite the baseline (report-only/enforcement first).
- Matrix child: 0 errors; bt_bap reason updated as above.

## G1 (canonical)

`./scripts/test-all.sh` → **49 PASS / 0 FAIL / 49 TOTAL** (29 twister + 5
exec-only + 12 Python + coverage + matrix + BSim); coverage enforcement
passes on the unchanged population-30 baseline; matrix 0 errors;
`fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all PASS with zero
compiler warnings; build contract 76/76; BSim pins byte-identical
(mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms `0xBAE24F7E`, 7.5 ms set,
reconnect = fresh mono oracle) plus the deliberate new
`duplicate_release_10ms` total pin; `git diff --check` clean; zero
actionable warnings.

## G3 hardware (planned non-destructive flash; no recovery)

Exact rows from `docs/testing/pre-refactor-hardware-baseline.md`,
autonomous hci0 central per AGENTS, consoles captured before reset,
`nrf-probes` raw DPIDR/AP/FICR identity evidence, unique `/tmp/r7-*`
MANIFEST + SHA256SUMS, production images restored at the end, no
audibility claim:

- nRF54L15: fresh Mode A 120 s, fresh Mode B 120 s, bonded reconnect
  Mode A 120 s — `decode_err=0 i2s_underrun=0 stream_reset=0`, offload
  `submit==success fallback=0`; plus desktop BlueZ/WirePlumber phase-3
  lifecycle if the environment is available; teardown-specific rows
  (midstream disconnect/reconnect/release) where existing
  scripts/scenarios support them.
- nRF5340/E83: fresh Mode A 120 s, fresh Mode B 120 s, bonded reconnect
  Mode B 120 s — same zero decode/i2s/reset faults plus zero
  `ISO seq gap`/`i2s_nrfx` warning lines and APLL ACTIVE evidence
  (`Drift state ACTIVE`/ppm).
- `bt unpair` (receiver) + central `bluetoothctl remove` only for fresh
  rows; preserve bonds on bonded rows.
- Results must include old/new ordering evidence, callback/ASCS evidence,
  the idempotence/observer map, exact hashes/hardware counters.

## Commits (no push/PR/amend/force/attribution)

1. `docs: record R7 handoff — stream teardown transition owner`
   (this document).
2. `test: expand lifecycle/session teardown matrices` (tests before
   implementation; pass on R6 code).
3. `refactor: add stream teardown transition owner in bt_bap` —
   coordinator + callback mapping + BSim scenario extensions + new
   `duplicate_release_10ms` scenario code/parser/runner updates +
   strengthened parser asserts + matrix witnesses.
4. `bsim: pin duplicate-release oracle` (only if the new scenario is
   used; adds the two-run-baselined `known.total` + the runner-test pin
   + provenance note).
5. `docs: accept R7 stream teardown transition owner` (results, after
   G1 + G3 pass).

Coverage migration only if a ratio decreases for a cause that requires
policy migration — then follow the plan's migration rule (report-only +
evidence first; never an unexplained rewrite).

## Non-scope

R8 FLPR boundary; a new state machine mirroring `stream_lifecycle`; stack
API changes; protocol/codec/360-frame support; destructive hardware
actions; changes to existing BSim pins.  Escalate only genuine blockers
(two attempts or contradictory evidence, impossible public behavior, test
weakening, unexplained hash/coverage/warning, hardware unavailable after
safe diagnosis, destructive action, architecture expansion).  Never commit
a knowingly failing state.
