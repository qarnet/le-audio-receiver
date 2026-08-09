/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Per-CIS ISO packet sequence tracker — implementation (pure logic, no
 * Zephyr deps).
 *
 * See audio_iso_seq.h for the contract.  Summary of the resolution
 * model: a wrap-safe 16-bit delta against the last delivered sequence
 * classifies each delivered callback as FIRST / CONTIG / GAP / RESYNC.
 * Only GAP produces concealment work, and the work is bounded by
 * ISO_SEQ_MAX_CONCEAL omitted SDUs per callback; every other outcome is
 * explicit and counted (resyncs) with no synthesis, so a malformed or
 * adversarial sequence stream can never drive an unbounded PLC loop.
 */

#include "audio_iso_seq.h"

#include <string.h>

void audio_iso_seq_reset(struct audio_iso_seq *st)
{
	if (st == NULL) {
		return;
	}
	memset(st, 0, sizeof(*st));
}

enum audio_iso_seq_result audio_iso_seq_update(struct audio_iso_seq *st, uint16_t seq,
					       uint32_t *omitted, uint16_t *first_seq)
{
	if (omitted != NULL) {
		*omitted = 0U;
	}
	if (first_seq != NULL) {
		*first_seq = 0U;
	}
	if (st == NULL) {
		return AUDIO_ISO_SEQ_RES_FIRST;
	}

	if (!st->initialized) {
		/* First delivered SDU: establish the base, never a gap. */
		st->initialized = true;
		st->last_seq = seq;
		return AUDIO_ISO_SEQ_RES_FIRST;
	}

	/* Wrap-safe forward delta: 0 = duplicate, 1 = contiguous, N =
	 * (N-1) omitted SDUs, large N = backward or out-of-window. */
	uint32_t delta = (uint32_t)(uint16_t)(seq - st->last_seq);

	if (delta == 0U) {
		/* Duplicate delivery: explicit outcome, no synthesis.  The
		 * position is re-based (same value) so the next callback's
		 * delta stays honest. */
		st->resyncs++;
		return AUDIO_ISO_SEQ_RES_RESYNC;
	}

	if (delta == 1U) {
		st->last_seq = seq;
		return AUDIO_ISO_SEQ_RES_CONTIG;
	}

	if (delta <= (uint32_t)ISO_SEQ_MAX_CONCEAL + 1U) {
		/* A bounded forward jump: (delta - 1) SDUs were omitted
		 * (no callback ever delivered for them).  Conceal them. */
		uint32_t n = delta - 1U;

		if (omitted != NULL) {
			*omitted = n;
		}
		if (first_seq != NULL) {
			*first_seq = (uint16_t)(st->last_seq + 1U);
		}
		st->last_seq = seq;
		st->concealed += n;
		return AUDIO_ISO_SEQ_RES_GAP;
	}

	/* Out-of-window forward jump (or backward wrap): a discontinuity
	 * beyond the concealment bound.  Re-base on the delivered sequence
	 * without synthesizing anything; the caller logs the evidence. */
	st->resyncs++;
	st->last_seq = seq;
	return AUDIO_ISO_SEQ_RES_RESYNC;
}

uint32_t audio_iso_seq_get_concealed(const struct audio_iso_seq *st)
{
	return st != NULL ? st->concealed : 0U;
}

uint32_t audio_iso_seq_get_resyncs(const struct audio_iso_seq *st)
{
	return st != NULL ? st->resyncs : 0U;
}

/*
 * ISO timestamp-cadence tracker — implementation (pure logic, no Zephyr
 * deps).  See audio_iso_seq.h for the contract.  Resolution model: each
 * delivered callback with a timestamp advances the cadence base; the
 * forward timestamp delta is converted to the nearest integer event
 * count on the SDU grid, and the delivered callback positions since the
 * previous timestamp are subtracted.  The remainder is the number of
 * ISO events for which no callback arrived (controller-side omission,
 * invisible to the HCI packet sequence number).  Only GAP produces
 * concealment work, bounded by ISO_SEQ_MAX_CONCEAL; every other outcome
 * is explicit and counted (WRAP excluded from resyncs by contract), so
 * a malformed or adversarial timestamp stream can never drive an
 * unbounded PLC loop.
 *
 * 64-bit intermediates: delta_us and event_count * interval_us both fit
 * in uint64_t for any uint32_t timestamp delta / interval combination,
 * so the "cannot be represented safely" resync condition is inherently
 * satisfied by the wide arithmetic (kept as a documented guard).
 */

void audio_iso_cadence_reset(struct audio_iso_cadence *st)
{
	if (st == NULL) {
		return;
	}
	memset(st, 0, sizeof(*st));
}

enum audio_iso_cadence_result audio_iso_cadence_update(struct audio_iso_cadence *st, bool has_ts,
						       uint32_t ts, uint32_t interval_us,
						       uint32_t *omitted)
{
	if (omitted != NULL) {
		*omitted = 0U;
	}
	if (st == NULL) {
		return AUDIO_ISO_CADENCE_RES_FIRST;
	}

	if (!has_ts) {
		/* Timestamp is optional by the public host contract: count
		 * one delivered callback position (saturated) and never
		 * warn or synthesize — a missing TS says nothing about an
		 * omission. */
		if (st->initialized && st->callbacks_since_ts < UINT32_MAX) {
			st->callbacks_since_ts++;
		}
		return AUDIO_ISO_CADENCE_RES_NO_TS;
	}

	if (interval_us == 0U) {
		/* No grid to measure against: explicit counted outcome,
		 * no synthesis, initialization state untouched. */
		st->resyncs++;
		return AUDIO_ISO_CADENCE_RES_RESYNC;
	}

	if (!st->initialized) {
		/* First timestamp: establish the base, never a gap. */
		st->initialized = true;
		st->last_ts = ts;
		st->callbacks_since_ts = 0U;
		return AUDIO_ISO_CADENCE_RES_FIRST;
	}

	if (ts < st->last_ts) {
		/* Controller timestamp wrap/rebase (nRF5340 SW Split wraps
		 * ~every 512 s; nRF54L15 SDC is a 32-bit GRTC microsecond
		 * view).  Expected: rebase without synthesis and without a
		 * resync increment.  Missing one event across a wrap is
		 * preferable to false or unbounded PLC. */
		st->last_ts = ts;
		st->callbacks_since_ts = 0U;
		return AUDIO_ISO_CADENCE_RES_WRAP;
	}

	/* Delivered callback positions since the prior valid timestamp:
	 * every intervening no-TS callback plus the current one. */
	const uint64_t delivered = (uint64_t)st->callbacks_since_ts + 1U;
	const uint64_t delta_us = (uint64_t)ts - (uint64_t)st->last_ts;

	/* Rebase the stored timestamp and clear the callback count before
	 * returning from every classified result below. */
	st->last_ts = ts;
	st->callbacks_since_ts = 0U;

	/* Nearest integer event count on the SDU grid (64-bit, no float). */
	const uint64_t event_count =
		(delta_us + (uint64_t)interval_us / 2U) / (uint64_t)interval_us;

	/* Unresolvable cadence: counted resync, no synthesis. */
	if (event_count == 0U) {
		/* Duplicate timestamp (ts == last_ts): zero event advance. */
		st->resyncs++;
		return AUDIO_ISO_CADENCE_RES_RESYNC;
	}
	if (event_count < delivered) {
		/* More delivered positions than grid events: cannot be a
		 * real omission. */
		st->resyncs++;
		return AUDIO_ISO_CADENCE_RES_RESYNC;
	}
	{
		const uint64_t nearest_us = event_count * (uint64_t)interval_us;
		const uint64_t err =
			delta_us > nearest_us ? delta_us - nearest_us : nearest_us - delta_us;

		if (err > (uint64_t)ISO_TS_DELTA_TOLERANCE_US) {
			/* Non-integral forward delta beyond tolerance. */
			st->resyncs++;
			return AUDIO_ISO_CADENCE_RES_RESYNC;
		}
	}

	/* Omitted events: grid events minus delivered callback positions.
	 * The wide arithmetic above cannot overflow (delta_us <= UINT32_MAX,
	 * so event_count <= UINT32_MAX and event_count * interval_us fits
	 * uint64_t); the bound check keeps the counter state representable. */
	const uint64_t omitted64 = event_count - delivered;

	if (omitted64 > (uint64_t)ISO_SEQ_MAX_CONCEAL) {
		/* Beyond the concealment bound: discontinuity, not a burst. */
		st->resyncs++;
		return AUDIO_ISO_CADENCE_RES_RESYNC;
	}

	if (omitted64 == 0U) {
		return AUDIO_ISO_CADENCE_RES_CONTIG;
	}
	if (omitted != NULL) {
		*omitted = (uint32_t)omitted64;
	}
	st->concealed += (uint32_t)omitted64;
	return AUDIO_ISO_CADENCE_RES_GAP;
}

uint32_t audio_iso_cadence_get_concealed(const struct audio_iso_cadence *st)
{
	return st != NULL ? st->concealed : 0U;
}

uint32_t audio_iso_cadence_get_resyncs(const struct audio_iso_cadence *st)
{
	return st != NULL ? st->resyncs : 0U;
}
