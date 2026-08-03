/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mode A event assembler — implementation (pure logic, no Zephyr deps).
 *
 * See audio_modea.h for the contract.  Summary of the resolution model:
 *
 * Each channel keeps a small FIFO of unresolved halves (compressed data
 * + metadata).  A half is enqueued on delivery; after every enqueue the
 * OLDEST entries of the two channels are compared and at most one event
 * is resolved (emitted) per store() call:
 *
 *   both usable ts, equal        -> decode both halves, emit
 *   left ts newer than right ts  -> right's mate on left channel is
 *                                   missing (left delivered past it) ->
 *                                   emit right + left PLC, keep left
 *   right ts newer (symmetric)   -> emit left + right PLC, keep right
 *   both positional sentinels    -> emit full-PLC pair (startup LOSTs)
 *   mixed sentinel/usable ts     -> no resolution (sentinel position is
 *                                   unknown); it waits for overflow
 *
 * "Newer" uses the same wrap-safe 32-bit ordering as the original
 * half-pairing (difference < 0x80000000 means the subtracted value is
 * older).  Per-channel FIFO order is delivery order, which is ISO
 * in-order per CIS, so decodes always advance each channel's decoder
 * chronologically: a PLC for a missing older event is emitted before any
 * newer packet of the same channel is decoded (the newer packet stays
 * queued until its own event resolves).
 */

#include "audio_modea.h"

#include <string.h>

static const struct modea_half *oldest_c(const struct modea_state *st, enum modea_channel ch)
{
	return &st->queue[ch][st->head[ch]];
}

/*
 * Same-event timestamp tolerance.  The two CISes of one CIG normally
 * deliver the same ISO SDU reference time per event, but a LOST
 * replacement's reference is synthesized controller-side and can sit a
 * few units off its mate's real reference (observed <=10 units on the
 * nRF5340 SW Split and BSim links; the interval is 10000).  Entries
 * whose wrap-safe reference distance is within this tolerance belong to
 * the same CIG event; anything beyond it is a genuinely different
 * event (>= interval apart).  Kept far below the 10000-unit interval.
 */
#define MODEA_TS_TOLERANCE 100U

/* Same CIG event (wrap-safe, tolerance-aware). */
static bool ts_same_event(uint32_t a, uint32_t b)
{
	uint32_t d = (uint32_t)(a - b);

	return d == 0U || d <= MODEA_TS_TOLERANCE || d >= (0xFFFFFFFFU - MODEA_TS_TOLERANCE);
}

static bool ts_newer(uint32_t a, uint32_t b)
{
	/* a is newer than b (wrap-safe): a - b in (TOL, 0x80000000) means
	 * b is older; within tolerance is the same event. */
	uint32_t d = (uint32_t)(a - b);

	return d > MODEA_TS_TOLERANCE && d < 0x80000000U;
}

void modea_config(struct modea_state *st, uint32_t interval_us)
{
	if (st == NULL) {
		return;
	}
	st->interval_us = interval_us;
}

void modea_reset(struct modea_state *st)
{
	if (st == NULL) {
		return;
	}
	memset(st, 0, sizeof(*st));
}

static void enqueue_half(struct modea_state *st, enum modea_channel ch, const uint8_t *data,
			 size_t len, bool src_valid, bool has_ts, uint32_t ts, uint16_t seq)
{
	size_t idx = (st->head[ch] + st->count[ch]) % MODEA_PENDING_DEPTH;
	struct modea_half *h = &st->queue[ch][idx];

	memset(h, 0, sizeof(*h));
	h->present = true;
	h->src_valid = src_valid;
	h->has_ts = has_ts;
	h->ts = ts;
	h->seq = seq;
	h->len = len;
	if (len > 0U && data != NULL) {
		memcpy(h->data, data, len);
	}
	st->count[ch]++;
}

static void pop_half(struct modea_state *st, enum modea_channel ch)
{
	if (st->count[ch] == 0U) {
		return;
	}
	st->queue[ch][st->head[ch]].present = false;
	st->head[ch] = (st->head[ch] + 1U) % MODEA_PENDING_DEPTH;
	st->count[ch]--;
}

/* Copy one resolved half into the event.  For a carried (real data)
 * half the source validity is the half's own ISO flag; for a PLC half
 * the half was NOT delivered, so its source validity is false (the
 * concealment is synthesized receiver-side). */
static void fill_half(struct modea_event *ev, enum modea_channel ch, const struct modea_half *h,
		      bool have_data)
{
	ev->seq[ch] = h->seq;
	if (have_data && h->len > 0U) {
		ev->half_valid[ch] = true;
		ev->half_src[ch] = h->src_valid;
		ev->len[ch] = h->len;
		memcpy(ev->data[ch], h->data, h->len);
	} else {
		/* Missing half: PLC (NULL input) at the same event.  The
		 * decode octets match the available half's (same event). */
		ev->half_valid[ch] = false;
		ev->half_src[ch] = false;
		ev->len[ch] = 0U;
	}
}

/* Emit one resolved event from the channel-head halves.
 *
 * l_data/r_data say which halves carry real data (the other is a PLC
 * half).  pop_l/pop_r say which heads are consumed: for an asymmetric
 * resolution only the OLDER half is consumed and the newer half stays
 * pending (it will pair with the mate's next event).  For an equal-ts
 * or full-PLC resolution both heads are consumed.
 *
 * The event's ts is the OLDER event's ts (the resolved event), never the
 * newer pending half's.  Returns true when an event was emitted.
 */
static bool emit_event(struct modea_state *st, struct modea_event *ev, bool l_data, bool r_data,
		       bool pop_l, bool pop_r, uint32_t ts)
{
	const struct modea_half *l = oldest_c(st, MODEA_CH_LEFT);
	const struct modea_half *r = oldest_c(st, MODEA_CH_RIGHT);

	/* A half carries real data only when the resolution assigned it
	 * data AND the original SDU was source-valid (a LOST half is
	 * never decoded from its (unreliable) payload). */
	const bool l_carries = l_data && l->src_valid;
	const bool r_carries = r_data && r->src_valid;

	if (ev != NULL) {
		memset(ev, 0, sizeof(*ev));
		ev->ts = ts;
		fill_half(ev, MODEA_CH_LEFT, l, l_carries);
		fill_half(ev, MODEA_CH_RIGHT, r, r_carries);
	}

	/* A PLC half is any half whose event data is not carried. */
	if (!l_carries || !r_carries) {
		st->plc_backed_events++;
	}
	st->resolved_events++;

	if (pop_l) {
		pop_half(st, MODEA_CH_LEFT);
	}
	if (pop_r) {
		pop_half(st, MODEA_CH_RIGHT);
	}
	return true;
}

/* The caller decoded a half that had data: nothing else needed here —
 * decode failure handling is caller-side (it skips the unsafe push). */

enum modea_action modea_store(struct modea_state *st, enum modea_channel ch, const uint8_t *data,
			      size_t len, bool src_valid, bool has_ts, uint32_t ts, uint16_t seq,
			      struct modea_event *ev)
{
	if (st == NULL) {
		return MODEA_ACTION_NONE;
	}

	/* Reject before any state mutation: an oversized frame block can
	 * never be buffered.  The caller's exact-shape validation runs
	 * first; this is defense in depth and is never silent. */
	if (len > MODEA_MAX_FRAME_OCTETS) {
		st->rejects++;
		return MODEA_ACTION_DROP;
	}

	/* Track the last delivered event position per channel.  A
	 * timestamped SDU updates it directly; a LOST (no-ts) SDU gets a
	 * predicted position one interval past the last delivery so it can
	 * pair with the mate's real timestamp at the same event.  The
	 * queue entry carries the EFFECTIVE position (predicted counts as
	 * usable for resolution); only a LOST before the first
	 * timestamped delivery is a positional sentinel (no usable ts). */
	bool eff_has_ts;
	uint32_t eff_ts;

	if (has_ts) {
		eff_has_ts = true;
		eff_ts = ts;
		st->last_ts[ch] = ts;
		st->last_has_ts[ch] = true;
	} else if (st->last_has_ts[ch]) {
		eff_has_ts = true;
		eff_ts = st->last_ts[ch] + st->interval_us;
		st->last_ts[ch] = eff_ts;
	} else {
		/* Positional sentinel: no usable position yet. */
		eff_has_ts = false;
		eff_ts = 0U;
	}

	/* Enqueue, dropping the oldest entry of this channel first on
	 * overflow (bounded state; the drop is counted, never silent).
	 * A drop is reported to the caller (DROP action) so it can log —
	 * unless a resolution emit happens in the same call, which takes
	 * precedence. */
	bool dropped = false;

	if (st->count[ch] >= MODEA_PENDING_DEPTH) {
		pop_half(st, ch);
		st->overflow_drops++;
		dropped = true;
	}
	enqueue_half(st, ch, data, len, src_valid, eff_has_ts, eff_ts, seq);

	/* Resolve at most one event: the two oldest entries. */
	if (st->count[MODEA_CH_LEFT] == 0U || st->count[MODEA_CH_RIGHT] == 0U) {
		return dropped ? MODEA_ACTION_DROP : MODEA_ACTION_NONE;
	}

	const struct modea_half *l = oldest_c(st, MODEA_CH_LEFT);
	const struct modea_half *r = oldest_c(st, MODEA_CH_RIGHT);

	if (l->has_ts && r->has_ts) {
		if (ts_same_event(l->ts, r->ts)) {
			/* Same CIG event: decode both halves. */
			emit_event(st, ev, true, true, true, true, l->ts);
			return MODEA_ACTION_EMIT;
		}
		if (ts_newer(l->ts, r->ts)) {
			/* Left delivered past right's event: right's mate
			 * on left is missing — PLC left at right's ts,
			 * keep the newer left half pending. */
			emit_event(st, ev, false, true, false, true, r->ts);
			return MODEA_ACTION_EMIT;
		}
		/* Right delivered past left's event: PLC right at left's
		 * ts, keep the newer right half pending. */
		emit_event(st, ev, true, false, true, false, l->ts);
		return MODEA_ACTION_EMIT;
	}

	if (!l->has_ts && !r->has_ts) {
		/* Both positional sentinels (startup LOSTs): pair as a
		 * full-PLC event, matching the historical ts=0==0 pairing. */
		emit_event(st, ev, false, false, true, true, 0U);
		return MODEA_ACTION_EMIT;
	}

	/* Mixed sentinel / usable ts: the sentinel is this channel's first
	 * event (no prior timestamped delivery), which positionally is the
	 * other channel's oldest unresolved event — both channels deliver
	 * their CIG events in order from their own starts.  Resolve them as
	 * the same event: the real half carries data, the sentinel channel
	 * is PLC'd at the real half's ts.  This never blocks the pipeline
	 * (a lingering sentinel would otherwise stall every later event). */
	if (l->has_ts) {
		emit_event(st, ev, true, false, true, true, l->ts);
	} else {
		emit_event(st, ev, false, true, true, true, r->ts);
	}
	return MODEA_ACTION_EMIT;
}

void modea_get_stats(const struct modea_state *st, struct modea_stats *out)
{
	if (st == NULL || out == NULL) {
		return;
	}
	out->resolved_events = st->resolved_events;
	out->plc_backed_events = st->plc_backed_events;
	out->overflow_drops = st->overflow_drops;
	out->rejects = st->rejects;
}
