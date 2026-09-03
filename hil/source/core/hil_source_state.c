/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure source run-state machine (RH1A).
 *
 * Validation-first mutation: every function checks all failure conditions
 * against the current state and local values before touching a single
 * byte of the state object, so a rejected operation leaves the whole
 * object byte-for-byte unchanged.  Stream indices are validated before
 * any counter field pointer is formed, so an out-of-range index can never
 * produce an invalid pointer.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "hil_source_state.h"

/* Resolve the counter slot for one stream after validating that the run
 * is active and the index is within the configured stream count.  The
 * pointer is only formed after the bounds check passes. */
static int hil_state_prepare_counter(struct hil_source_state *st, uint32_t stream,
				     struct hil_source_stream_counter **out)
{
	uint8_t streams;

	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	streams = hil_source_mode_stream_count(st->config.mode);
	if (streams == 0U || stream >= streams) {
		return -EINVAL;
	}
	*out = &st->counters[stream];
	return 0;
}

void hil_source_state_reset(struct hil_source_state *st)
{
	if (st == NULL) {
		return;
	}
	memset(st, 0, sizeof(*st));
}

int hil_source_state_configure(struct hil_source_state *st, const struct hil_source_config *cfg)
{
	if (st == NULL || cfg == NULL) {
		return -EINVAL;
	}
	if (st->active) {
		return -EBUSY;
	}
	/* A terminal snapshot is immutable until reset or the next
	 * successful start; reconfiguration must not change it. */
	if (st->terminal != HIL_SOURCE_VERDICT_NONE) {
		return -EBUSY;
	}
	if (cfg->mode < HIL_SOURCE_MODE_MONO || cfg->mode > HIL_SOURCE_MODE_B) {
		return -EINVAL;
	}
	if (cfg->profile < HIL_SOURCE_PROFILE_48_3_1 || cfg->profile > HIL_SOURCE_PROFILE_48_4_1) {
		return -EINVAL;
	}
	if (cfg->peer_address_type < HIL_SOURCE_ADDR_PUBLIC ||
	    cfg->peer_address_type > HIL_SOURCE_ADDR_RANDOM) {
		return -EINVAL;
	}
	if (cfg->reconnect_policy < HIL_SOURCE_RECONNECT_NONE ||
	    cfg->reconnect_policy > HIL_SOURCE_RECONNECT_ONCE) {
		return -EINVAL;
	}
	if (cfg->scored_sdu_count < 1U || cfg->scored_sdu_count > HIL_SOURCE_MAX_SCORED_SDUS) {
		return -EINVAL;
	}
	if (cfg->signal_seed == 0U) {
		return -EINVAL;
	}

	st->config = *cfg;
	st->has_config = true;
	return 0;
}

int hil_source_state_start(struct hil_source_state *st)
{
	if (st == NULL) {
		return -EINVAL;
	}
	if (!st->has_config) {
		return -EINVAL;
	}
	if (st->active) {
		return -EBUSY;
	}

	memset(st->counters, 0, sizeof(st->counters));
	memset(st->scored_baseline, 0, sizeof(st->scored_baseline));
	st->first_error = 0;
	st->stop_requested = false;
	st->reconnect_used = false;
	st->terminal = HIL_SOURCE_VERDICT_NONE;
	st->aborted = false;
	st->abort_cause = HIL_SOURCE_ABORT_STOP; /* meaningful only when
						  * aborted */
	st->segment = 0U;
	st->current_state = HIL_SOURCE_RUN_STATE_IDLE;
	st->active = true;
	return 0;
}

int hil_source_state_advance(struct hil_source_state *st, enum hil_source_run_state next)
{
	uint8_t streams;
	uint32_t i;

	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	if (next <= st->current_state || next > HIL_SOURCE_RUN_STATE_TEARDOWN) {
		/* Repeat, regression, skip, or out-of-range enum. */
		return -EINVAL;
	}
	if (next != (enum hil_source_run_state)(st->current_state + 1)) {
		/* Non-adjacent jump within the segment. */
		return -EINVAL;
	}

	if (next == HIL_SOURCE_RUN_STATE_SCORED_COMPLETE) {
		streams = hil_source_mode_stream_count(st->config.mode);
		for (i = 0U; i < streams; i++) {
			uint32_t submitted =
				st->counters[i].submitted_scored_sdus - st->scored_baseline[i];

			if (submitted != st->config.scored_sdu_count) {
				/* Underrun or overrun of the per-segment,
				 * per-stream scored target. */
				return -EINVAL;
			}
		}
	}

	st->current_state = next;
	return 0;
}

int hil_source_state_next_segment(struct hil_source_state *st)
{
	uint8_t streams;
	uint32_t i;

	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	if (st->current_state != HIL_SOURCE_RUN_STATE_TEARDOWN) {
		return -EINVAL;
	}
	if (st->aborted) {
		return -EINVAL; /* abort is terminal for the whole run */
	}
	if (st->config.reconnect_policy != HIL_SOURCE_RECONNECT_ONCE) {
		return -EINVAL;
	}
	if (st->reconnect_used) {
		return -EINVAL;
	}

	streams = hil_source_mode_stream_count(st->config.mode);
	for (i = 0U; i < streams; i++) {
		st->scored_baseline[i] = st->counters[i].submitted_scored_sdus;
	}
	st->reconnect_used = true;
	st->segment = st->segment + 1U;
	st->current_state = HIL_SOURCE_RUN_STATE_CONNECTING;
	return 0;
}

int hil_source_state_stop_request(struct hil_source_state *st)
{
	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	st->stop_requested = true;
	return 0;
}

int hil_source_state_counter_submit(struct hil_source_state *st, uint32_t stream)
{
	struct hil_source_stream_counter *c;
	int ret;

	if (st == NULL) {
		return -EINVAL;
	}
	ret = hil_state_prepare_counter(st, stream, &c);
	if (ret != 0) {
		return ret;
	}
	if (c->submitted_sdus == UINT32_MAX) {
		return -EOVERFLOW;
	}
	c->submitted_sdus = c->submitted_sdus + 1U;
	return 0;
}

int hil_source_state_counter_submit_scored(struct hil_source_state *st, uint32_t stream)
{
	struct hil_source_stream_counter *c;
	int ret;

	if (st == NULL) {
		return -EINVAL;
	}
	ret = hil_state_prepare_counter(st, stream, &c);
	if (ret != 0) {
		return ret;
	}
	/* One scored SDU submission atomically increments both the total
	 * and the scored count; check both before changing either. */
	if (c->submitted_sdus == UINT32_MAX || c->submitted_scored_sdus == UINT32_MAX) {
		return -EOVERFLOW;
	}
	c->submitted_sdus = c->submitted_sdus + 1U;
	c->submitted_scored_sdus = c->submitted_scored_sdus + 1U;
	return 0;
}

int hil_source_state_counter_send_failure(struct hil_source_state *st, uint32_t stream)
{
	struct hil_source_stream_counter *c;
	int ret;

	if (st == NULL) {
		return -EINVAL;
	}
	ret = hil_state_prepare_counter(st, stream, &c);
	if (ret != 0) {
		return ret;
	}
	if (c->send_failures == UINT32_MAX) {
		return -EOVERFLOW;
	}
	c->send_failures = c->send_failures + 1U;
	return 0;
}

int hil_source_state_counter_sent_callback(struct hil_source_state *st, uint32_t stream)
{
	struct hil_source_stream_counter *c;
	int ret;

	if (st == NULL) {
		return -EINVAL;
	}
	ret = hil_state_prepare_counter(st, stream, &c);
	if (ret != 0) {
		return ret;
	}
	if (c->sent_callbacks == UINT32_MAX) {
		return -EOVERFLOW;
	}
	c->sent_callbacks = c->sent_callbacks + 1U;
	return 0;
}

int hil_source_state_error_report(struct hil_source_state *st, int err)
{
	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	if (err >= 0) {
		return -EINVAL;
	}
	if (st->first_error != 0) {
		return 0; /* first error already stored: never overwrite */
	}
	st->first_error = err;
	return 0;
}

int hil_source_state_terminal(struct hil_source_state *st, enum hil_source_verdict verdict)
{
	uint8_t streams;
	uint32_t i;

	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	if (st->current_state != HIL_SOURCE_RUN_STATE_TEARDOWN) {
		return -EINVAL;
	}
	if (st->terminal != HIL_SOURCE_VERDICT_NONE) {
		return -EINVAL; /* once only */
	}

	if (verdict == HIL_SOURCE_VERDICT_PASS) {
		if (st->aborted) {
			return -EINVAL; /* abort can only end in fail */
		}
		if (st->first_error != 0) {
			return -EINVAL;
		}
		streams = hil_source_mode_stream_count(st->config.mode);
		for (i = 0U; i < streams; i++) {
			if (st->counters[i].send_failures != 0U) {
				return -EINVAL;
			}
		}
		if (st->config.reconnect_policy == HIL_SOURCE_RECONNECT_NONE) {
			for (i = 0U; i < streams; i++) {
				if (st->counters[i].submitted_scored_sdus !=
				    st->config.scored_sdu_count) {
					return -EINVAL;
				}
			}
		} else {
			if (st->segment != 1U) {
				return -EINVAL; /* both segments required */
			}
			for (i = 0U; i < streams; i++) {
				if (st->counters[i].submitted_scored_sdus !=
				    2U * st->config.scored_sdu_count) {
					return -EINVAL;
				}
			}
		}
	} else if (verdict != HIL_SOURCE_VERDICT_FAIL) {
		return -EINVAL; /* NONE or unknown verdict */
	}

	st->terminal = verdict;
	st->active = false; /* snapshot stays immutable until reset/start */
	return 0;
}

int hil_source_state_abort_to_teardown(struct hil_source_state *st,
				       enum hil_source_abort_cause cause)
{
	if (st == NULL || !st->active) {
		return -EINVAL;
	}
	if (cause < HIL_SOURCE_ABORT_STOP || cause > HIL_SOURCE_ABORT_ERROR) {
		return -EINVAL;
	}
	if (st->current_state == HIL_SOURCE_RUN_STATE_TEARDOWN) {
		return -EINVAL; /* after ordinary teardown */
	}
	if (st->aborted) {
		return -EINVAL; /* repeat abort */
	}

	st->current_state = HIL_SOURCE_RUN_STATE_TEARDOWN;
	st->aborted = true;
	st->abort_cause = cause;
	return 0;
}
