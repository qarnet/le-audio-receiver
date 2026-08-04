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
