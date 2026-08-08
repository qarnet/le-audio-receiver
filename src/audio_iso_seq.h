/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Per-CIS ISO packet sequence tracker — receiver-side gap detection for
 * callbacks the ISO stack omits entirely.
 *
 * Production BAP audio on the nRF5340 SW Split controller usually
 * delivers either a VALID-flag callback or a LOST-flag callback for every
 * CIG event.  Observed on hardware (E83, Mode B and bonded rows): the
 * controller SOMETIMES delivers NO callback at all for a lost SDU — no
 * valid packet, no LOST event.  The next delivered callback's
 * Packet_Sequence_Number then jumps by more than one.  Without a
 * response the audio path starves: the I2S DMA queue drains
 * (`i2s_nrfx: Next buffers not supplied on time`) while the PLC counter
 * stays near zero, because a LOST callback never arrived to trigger
 * concealment.
 *
 * This module tracks one CIS's delivered sequence numbers and reports
 * how many SDUs were omitted between consecutive callbacks, so the
 * caller can conceal exactly those events as PLC BEFORE processing the
 * current SDU.  It is pure ordering/counting logic (no Zephyr
 * dependencies), unit-testable directly.
 *
 * Sequence semantics (documented contract):
 *
 *   - first delivered SDU  -> FIRST (tracker initialized, no gap);
 *   - delta == 1           -> CONTIG (no gap);
 *   - delta in [2, MAX+1]  -> GAP of (delta - 1) omitted SDUs;
 *   - delta == 0 (duplicate), backward wrap, or delta > MAX+1
 *     (out-of-window discontinuity) -> RESYNC: counted, last position
 *     re-based on the delivered sequence, NO synthesis.  Duplicates,
 *     backward, and out-of-window sequences are explicit outcomes and
 *     can never produce a concealment loop.
 *
 * 16-bit wrap (0xffff -> 0) is handled by the wrap-safe delta; a
 * 0xffff -> 1 step is a one-SDU gap, not a discontinuity.
 *
 * Concealment bound: ISO_SEQ_MAX_CONCEAL omitted SDUs per delivered
 * callback (default 8 = 80 ms of PLC at 10 ms frames).  Justification:
 * the I2S slab holds 16 blocks (~160 ms of buffered audio); concealing
 * up to half the slab budget per gap keeps output cadence through
 * realistic loss bursts without ever synthesizing unbounded work.  A
 * gap beyond the bound is a discontinuity (RESYNC), which the caller
 * logs as evidence rather than concealing.
 *
 * The tracker advances on EVERY delivered callback (valid or LOST): a
 * LOST-flag callback IS a delivered event with its own sequence number,
 * so its position is consumed and the next callback's delta stays
 * contiguous.  Only SDUs for which NO callback ever arrives are
 * reported as omitted.
 */

#ifndef AUDIO_ISO_SEQ_H
#define AUDIO_ISO_SEQ_H

#include <stdbool.h>
#include <stdint.h>

#if defined(CONFIG_AUDIO_ISO_SEQ_MAX_CONCEAL)
#define ISO_SEQ_MAX_CONCEAL CONFIG_AUDIO_ISO_SEQ_MAX_CONCEAL
#else
#define ISO_SEQ_MAX_CONCEAL 8
#endif

enum audio_iso_seq_result {
	AUDIO_ISO_SEQ_RES_FIRST = 0, /* first delivered SDU — tracker initialized, no gap */
	AUDIO_ISO_SEQ_RES_CONTIG,    /* contiguous — no gap */
	AUDIO_ISO_SEQ_RES_GAP,       /* 1..MAX omitted SDUs detected (see *omitted) */
	AUDIO_ISO_SEQ_RES_RESYNC,    /* duplicate/backward/out-of-window — rebased, no synthesis */
};

struct audio_iso_seq {
	bool initialized;   /* false until the first delivered SDU */
	uint16_t last_seq;  /* sequence of the most recently delivered SDU */
	uint32_t concealed; /* cumulative omitted SDUs concealed (lifetime) */
	uint32_t resyncs;   /* cumulative non-concealable discontinuities (lifetime) */
};

/** Clear all state and counters (stream configure/start/stop/release/disconnect). */
void audio_iso_seq_reset(struct audio_iso_seq *st);

/**
 * Feed one delivered callback's sequence number.
 *
 * @param st        Per-CIS tracker state.
 * @param seq       Controller-reported ISO packet sequence number of the
 *                  delivered SDU.
 * @param omitted   Receives the number of omitted SDUs to conceal when
 *                  the result is GAP (else 0).  May be NULL.
 * @param first_seq Receives the sequence number of the first omitted SDU
 *                  when the result is GAP (else 0).  May be NULL.
 *
 * @return AUDIO_ISO_SEQ_RES_FIRST / CONTIG / GAP / RESYNC per the
 *         contract above.  On GAP the tracker has advanced to @p seq
 *         and the omitted SDUs are the sequences first_seq .. seq-1
 *         (wrap-safe, at most ISO_SEQ_MAX_CONCEAL entries).
 */
enum audio_iso_seq_result audio_iso_seq_update(struct audio_iso_seq *st, uint16_t seq,
					       uint32_t *omitted, uint16_t *first_seq);

/** Cumulative omitted SDUs concealed (diagnostics/tests). */
uint32_t audio_iso_seq_get_concealed(const struct audio_iso_seq *st);

/** Cumulative non-concealable discontinuities (diagnostics/tests). */
uint32_t audio_iso_seq_get_resyncs(const struct audio_iso_seq *st);

#endif /* AUDIO_ISO_SEQ_H */
