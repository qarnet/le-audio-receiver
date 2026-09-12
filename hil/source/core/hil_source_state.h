/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure source run-state machine (RH1A).
 *
 * Owns no Bluetooth objects, threads, locks, clocks, or I/O.  All state is
 * plain struct fields; every rejected operation leaves every byte of the
 * state object unchanged (each function validates fully before mutating).
 */

#ifndef HIL_SOURCE_STATE_H
#define HIL_SOURCE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "hil_source_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Frozen configuration accepted by hil_source_state_configure(). */
struct hil_source_config {
	enum hil_source_mode mode;
	enum hil_source_profile profile;
	uint8_t peer_address[6];
	enum hil_source_address_type peer_address_type;
	uint32_t scored_sdu_count; /* per segment, per stream */
	uint32_t signal_seed;
	enum hil_source_reconnect_policy reconnect_policy;
};

/* Per-stream counters for the two configured streams (Mode A). */
struct hil_source_stream_counter {
	uint32_t submitted_sdus;
	uint32_t submitted_scored_sdus;
	uint32_t send_failures;
	uint32_t sent_callbacks;
};

struct hil_source_state {
	struct hil_source_config config;
	bool has_config;
	bool active;
	bool stop_requested;
	bool reconnect_used;
	bool aborted;                            /* bounded abort-to-teardown accepted this run */
	enum hil_source_abort_cause abort_cause; /* meaningful only when
						  * aborted */
	uint32_t segment;                        /* current run segment, starts at 0 */
	enum hil_source_run_state current_state;
	enum hil_source_verdict terminal;
	int first_error; /* first nonzero negative errno, 0 = none */
	struct hil_source_stream_counter counters[2];
	/* Scored-submission baseline captured at each segment start. */
	uint32_t scored_baseline[2];
};

/* Reset to an empty idle state: clears configuration and every volatile
 * field.  Always succeeds. */
void hil_source_state_reset(struct hil_source_state *st);
/* Store a whole validated configuration atomically.  Accepted only while
 * inactive and only when no terminal snapshot is present (a snapshot stays
 * immutable until reset or the next successful start); does not start a
 * run.  -EBUSY when active or post-terminal, -EINVAL for a null pointer
 * or out-of-range enum/count/seed. */
int hil_source_state_configure(struct hil_source_state *st, const struct hil_source_config *cfg);

/* Start a run from the stored configuration: requires configuration,
 * clears every volatile counter/error/stop/terminal/abort field (the
 * abort snapshot is cleared while the configuration is preserved), and
 * enters idle at segment 0.  -EINVAL when unconfigured, -EBUSY when
 * active. */
int hil_source_state_start(struct hil_source_state *st);

/* Advance within the current segment along the exact state sequence
 * (idle -> configured -> connecting -> secured -> discovered -> qos ->
 * streaming -> scored_complete -> teardown) with no repeat, skip, or
 * regression.  Advancing to scored_complete requires every configured
 * stream to have submitted exactly this segment's scored target. */
int hil_source_state_advance(struct hil_source_state *st, enum hil_source_run_state next);

/* Start the next segment: allowed only from teardown, only when the
 * reconnect policy is `once`, only once per run, and never after a
 * bounded abort (abort is terminal for the whole run).  Increments the
 * segment, starts at connecting, and records new scored baselines. */
int hil_source_state_next_segment(struct hil_source_state *st);

/* Idempotent stop request: only sets the stop flag; status reads never
 * mutate.  -EINVAL when inactive. */
int hil_source_state_stop_request(struct hil_source_state *st);

/* Per-stream counter operations.  Reject a stream index outside the
 * configured stream count and reject uint32 overflow without changing
 * any state.  -EINVAL when inactive or index out of range, -EOVERFLOW on
 * wrap.  A scored submission is one SDU: it atomically increments both
 * the total submitted count and the scored submitted count (both checked
 * for overflow first), so a pass snapshot always has total submitted >=
 * scored submitted.  `counter_submit` remains the non-scored path for
 * preamble and tail submissions. */
int hil_source_state_counter_submit(struct hil_source_state *st, uint32_t stream);
int hil_source_state_counter_submit_scored(struct hil_source_state *st, uint32_t stream);
int hil_source_state_counter_send_failure(struct hil_source_state *st, uint32_t stream);
int hil_source_state_counter_sent_callback(struct hil_source_state *st, uint32_t stream);

/* Record the first nonzero negative errno; never overwrites a stored
 * error.  -EINVAL when inactive or for a non-negative error. */
int hil_source_state_error_report(struct hil_source_state *st, int err);

/* Accept a terminal verdict once, only after teardown.  Pass requires no
 * first error, zero send failures, and every configured stream's scored
 * submitted count exactly equal to the configured target (policy `none`),
 * or exactly twice the target after both segments (policy `once`); pass
 * is always rejected after a bounded abort.  Failure stays legal after
 * any teardown, including abort.  Terminal clears active but preserves
 * the immutable status snapshot until reset or the next successful
 * start. */
int hil_source_state_terminal(struct hil_source_state *st, enum hil_source_verdict verdict);

/* Bounded abort-to-teardown: legal from any active run state before
 * teardown with a valid cause.  Atomically sets the current state to
 * teardown, marks the run aborted, and stores the cause.  Abort is
 * terminal for the whole run: pass terminal and a later reconnect segment
 * are both rejected.  -EINVAL when inactive, after teardown, on repeat
 * abort, or for an invalid cause. */
int hil_source_state_abort_to_teardown(struct hil_source_state *st,
				       enum hil_source_abort_cause cause);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_STATE_H */
