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
 *
 * IMPORTANT: HCI packet sequence continuity does NOT prove delivery
 * continuity.  The nRF5340 SW Split controller advances its per-session
 * sequence number only when an SDU is emitted to the host (see
 * isoal.c: isoal_rx_buffered_emit_sdu()/isoal_rx_try_emit_sdu()); a
 * radio event with no received PDU emits no HCI SDU and consumes no
 * sequence number.  Controller-side omissions therefore leave
 * app-visible seq_num contiguous (FR4 mono evidence: 12000 SDUs
 * transmitted, 8876 callbacks, zero sequence gaps, 225 I2S resets).
 * The audio_iso_cadence tracker below detects exactly those omissions
 * from delivered ISO timestamps, which jump by the integer multiple of
 * the SDU interval that the omitted events span.
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

/*
 * ISO timestamp-cadence tracker (audio_iso_cadence) — receiver-side
 * omission detection from delivered ISO timestamps for one-CIS modes
 * (mono and Mode B), independent of the sequence tracker above.
 *
 * HCI ISO timestamps are only present with BT_ISO_FLAGS_TS; the host
 * copies the controller timestamp verbatim and never synthesizes one
 * when the TS flag is absent.  Delivered callbacks whose timestamps
 * advance by more than the SDU interval imply ISO events for which no
 * callback arrived (the same output events a sequence jump would
 * report).  The caller conceals the omitted events as PLC before
 * processing the current SDU, and takes the maximum of the sequence-
 * and timestamp-derived omitted counts so one physical omission is
 * never concealed twice.
 *
 * Cadence semantics (documented contract):
 *
 *   - st == NULL                 -> FIRST (no output mutation);
 *   - has_ts == false            -> NO_TS: counts one delivered
 *     callback position (saturated at UINT32_MAX), never synthesizes —
 *     the TS flag is optional by the public host contract;
 *   - interval_us == 0           -> RESYNC (counted), no synthesis,
 *     initialization state untouched;
 *   - first timestamp            -> FIRST (last_ts stored, base set);
 *   - ts < last_ts               -> WRAP: controller timestamp wrap or
 *     baseline rebase (nRF5340 SW Split wraps ~every 512 s, nRF54L15
 *     SDC uses a 32-bit GRTC microsecond view).  Rebased, no synthesis,
 *     no resync increment;
 *   - forward delta -> the delivered callback positions since the last
 *     valid timestamp (captured callbacks_since_ts + 1) are subtracted
 *     from the nearest integer event count
 *     (delta_us + interval/2) / interval.  Zero or negative remaining
 *     positions -> CONTIG; 1..ISO_SEQ_MAX_CONCEAL -> GAP (counted);
 *     beyond the bound, or any unresolvable arithmetic (duplicate
 *     timestamp, non-integral delta beyond ISO_TS_DELTA_TOLERANCE_US,
 *     event count below delivered positions, zero interval) -> RESYNC
 *     (counted, rebased, no synthesis).
 *
 * ISO_TS_DELTA_TOLERANCE_US (10 us) matches installed nrf5340_audio's
 * SDU_REF_CH_DELTA_MAX_US for 10 ms frames and safely covers the 7.5 ms
 * receiver shape.  All event-count arithmetic uses 64-bit intermediates
 * (no floating point); for uint32_t timestamp deltas and interval the
 * products fit in 64 bits, so the "cannot be represented safely"
 * resync condition is inherently satisfied by the wide intermediates.
 *
 * Missing-TS callbacks must not become false omissions: they count as
 * delivered positions, so timestamps at 10000 and 30000 with one
 * no-TS callback between them represent two delivered positions and
 * zero omissions, while the same timestamp pair without the no-TS
 * callback represents one omitted event.
 */
#define ISO_TS_DELTA_TOLERANCE_US 10U

enum audio_iso_cadence_result {
	AUDIO_ISO_CADENCE_RES_FIRST = 0, /* first timestamp — base set, no gap */
	AUDIO_ISO_CADENCE_RES_NO_TS,     /* callback without a timestamp — position counted, no
					    synthesis */
	AUDIO_ISO_CADENCE_RES_CONTIG,    /* contiguous — no gap */
	AUDIO_ISO_CADENCE_RES_GAP,       /* 1..MAX omitted events detected (see *omitted) */
	AUDIO_ISO_CADENCE_RES_WRAP,   /* timestamp wrap/rebase — rebased, no synthesis, not a resync
				       */
	AUDIO_ISO_CADENCE_RES_RESYNC, /* unresolvable cadence — rebased, counted, no synthesis */
};

struct audio_iso_cadence {
	bool initialized;            /* false until the first delivered timestamp */
	uint32_t last_ts;            /* most recently delivered ISO timestamp (us) */
	uint32_t callbacks_since_ts; /* delivered no-TS callbacks since last_ts (saturated) */
	uint32_t concealed;          /* cumulative omitted events concealed (lifetime) */
	uint32_t resyncs;            /* cumulative non-concealable cadence outcomes (lifetime) */
};

/** Clear all state and counters (stream configure/start/stop/release/disconnect). */
void audio_iso_cadence_reset(struct audio_iso_cadence *st);

/**
 * Feed one delivered callback's timestamp.
 *
 * @param st          Cadence tracker state.
 * @param has_ts      True when the callback carried a valid ISO timestamp
 *                    (BT_ISO_FLAGS_TS); false callbacks count one delivered
 *                    position and are never synthesized.
 * @param ts          Controller-reported ISO timestamp in microseconds
 *                    (valid only when @p has_ts is true).
 * @param interval_us SDU event interval (validated frame duration for
 *                    one-CIS modes; 0 forces a counted RESYNC).
 * @param omitted     Receives the number of omitted events to conceal
 *                    when the result is GAP (else 0).  May be NULL.
 *
 * @return FIRST / NO_TS / CONTIG / GAP / WRAP / RESYNC per the contract
 *         above.  On GAP the tracker has re-based on @p ts and the
 *         omitted events are the grid positions between the previous
 *         timestamp and @p ts not covered by delivered callbacks (at
 *         most ISO_SEQ_MAX_CONCEAL).
 */
enum audio_iso_cadence_result audio_iso_cadence_update(struct audio_iso_cadence *st, bool has_ts,
						       uint32_t ts, uint32_t interval_us,
						       uint32_t *omitted);

/** Cumulative omitted events concealed (diagnostics/tests). */
uint32_t audio_iso_cadence_get_concealed(const struct audio_iso_cadence *st);

/** Cumulative non-concealable cadence outcomes (diagnostics/tests). */
uint32_t audio_iso_cadence_get_resyncs(const struct audio_iso_cadence *st);

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
