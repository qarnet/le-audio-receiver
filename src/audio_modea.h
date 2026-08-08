/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mode A event assembler — bounded two-channel half pairing with
 * per-channel PLC synthesis for missing CIS SDUs.
 *
 * Production BAP Mode A delivers one mono LC3 SDU per CIS per CIG event
 * (two sink ASEs, L and R).  The two CISes of one CIG carry the same ISO
 * SDU reference time at each event, so equal timestamps identify the two
 * halves of one audio frame; a wrap-safe 32-bit comparison orders them.
 * When one CIS loses an SDU the ISO stack delivers either a LOST-flag
 * callback (no timestamp) or nothing at all.  This module resolves each
 * event exactly once:
 *
 *   - equal timestamps: both halves available, decode both, push once;
 *   - newer evidence proves one channel missing for an older event:
 *     decode the available older half, PLC the missing channel at that
 *     same event, push once, retain the newer pending half;
 *   - repeated gaps produce one PLC-backed stereo output per confirmed
 *     missing event, bounded by the fixed per-channel queues, no heap.
 *
 * The module owns BOUNDED pending COMPRESSED SDU data per channel (never
 * net_buf pointers) plus timestamp/source-validity metadata, and decides
 * when an event is resolved.  Decoding is deferred until resolution so a
 * PLC for a missing earlier event is always generated before that
 * channel's later packet is decoded: the caller decodes each half in the
 * event's channel order, and each channel's decoder advances strictly
 * chronologically.  Actual LC3 decoding stays in the caller
 * (audio_decode / liblc3); this module is pure ordering/state logic,
 * unit-testable with no Zephyr dependencies.
 *
 * Bound and overflow behavior (documented contract): each channel holds
 * up to AUDIO_MODEA_PENDING_DEPTH unresolved events (default 2), which
 * covers the measured <=1-event cross-CIS callback skew plus single-SDU
 * losses and a one-event loss burst on one channel while the other keeps
 * delivering.  A third concurrent unresolved event on one channel (the
 * other channel stalled >=2 events) overflows: the OLDEST entry of that
 * channel is dropped, counted in stats.overflow_drops, and reported to
 * the caller (DROP action) — never silent.  LOST-flag SDUs (no
 * timestamp) before the first timestamped delivery of a channel are
 * positional sentinels: two sentinels pair as a full-PLC event (the
 * historical startup behavior), and a sentinel against a timestamped
 * half resolves positionally as that event with the sentinel channel
 * PLC'd — a lingering sentinel can never block later events.  After the
 * first timestamped delivery a LOST SDU's event position is predicted
 * as last_ts + interval, which is what makes steady-state loss pairing
 * correct.
 */

#ifndef AUDIO_MODEA_H
#define AUDIO_MODEA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_AUDIO_MODEA_MAX_FRAME_OCTETS)
#define MODEA_MAX_FRAME_OCTETS CONFIG_AUDIO_MODEA_MAX_FRAME_OCTETS
#else
#define MODEA_MAX_FRAME_OCTETS 120
#endif

#if defined(CONFIG_AUDIO_MODEA_PENDING_DEPTH)
#define MODEA_PENDING_DEPTH CONFIG_AUDIO_MODEA_PENDING_DEPTH
#else
#define MODEA_PENDING_DEPTH 2
#endif

#define MODEA_CHANNELS 2

enum modea_channel {
	MODEA_CH_LEFT = 0,  /* sink ASE 0 (FL) */
	MODEA_CH_RIGHT = 1, /* sink ASE 1 (FR) */
};

/* One buffered half of one CIG event on one channel. */
struct modea_half {
	bool present;   /* slot holds an unresolved half */
	bool src_valid; /* original ISO BT_ISO_FLAGS_VALID of this half */
	bool has_ts;    /* ts is usable (real ISO ts, or predicted) */
	uint32_t ts;    /* ISO SDU reference time / predicted position / 0 sentinel */
	uint16_t seq;   /* controller-reported ISO seq_num (diagnostics) */
	size_t len;     /* compressed bytes */
	uint8_t data[MODEA_MAX_FRAME_OCTETS];
};

struct modea_state {
	struct modea_half queue[MODEA_CHANNELS][MODEA_PENDING_DEPTH];
	size_t head[MODEA_CHANNELS];      /* oldest occupied slot index */
	size_t count[MODEA_CHANNELS];     /* occupied slots */
	uint32_t last_ts[MODEA_CHANNELS]; /* ts of last delivered event (real/predicted) */
	bool last_has_ts[MODEA_CHANNELS]; /* channel delivered a timestamped event */
	uint32_t interval_us;             /* SDU interval for predicted ts */
	uint32_t resolved_events;         /* events emitted */
	uint32_t plc_backed_events;       /* events with at least one PLC half */
	uint32_t overflow_drops;          /* halves dropped on queue overflow */
	uint32_t rejects;                 /* oversized inputs rejected before mutation */
};

/* What the caller must do after modea_store(). */
enum modea_action {
	MODEA_ACTION_NONE = 0, /* no output; state updated */
	MODEA_ACTION_EMIT = 1, /* one stereo event resolved — decode halves, push */
	MODEA_ACTION_DROP = 2, /* one half dropped on queue overflow; no output */
};

/* One resolved stereo event.  half_valid[i] false means that channel's
 * half is missing: decode with NULL input (PLC) at the same ts.  Data is
 * copied out of the assembler, so it stays valid for the caller's decode
 * regardless of later store() calls. */
struct modea_event {
	uint32_t ts;
	uint16_t seq[MODEA_CHANNELS];
	bool half_valid[MODEA_CHANNELS];
	bool half_src[MODEA_CHANNELS];
	size_t len[MODEA_CHANNELS];
	uint8_t data[MODEA_CHANNELS][MODEA_MAX_FRAME_OCTETS];
};

struct modea_stats {
	uint32_t resolved_events;
	uint32_t plc_backed_events;
	uint32_t overflow_drops;
	uint32_t rejects;
};

/** Configure the SDU interval (frame duration in us) used for predicted
 *  LOST-event positions.  Call once per stream shape before store(). */
void modea_config(struct modea_state *st, uint32_t interval_us);

/** Clear all pending state and counters (stream start / teardown). */
void modea_reset(struct modea_state *st);

/**
 * Deliver one half of one CIG event.
 *
 * @param st        Assembler state.
 * @param ch        Which channel this half belongs to.
 * @param data      Compressed frame block (ignored when !src_valid).
 * @param len       Byte length; must be <= MODEA_MAX_FRAME_OCTETS
 *                  (an oversized input is rejected with a stats.rejects
 *                  increment and no state mutation).
 * @param src_valid Original ISO VALID flag (false = LOST/concealment).
 * @param has_ts    ISO TS flag present on this SDU.
 * @param ts        ISO SDU reference time (meaningful when has_ts).
 * @param seq       Controller ISO seq_num.
 * @param ev        Receives the resolved event when returning
 *                  MODEA_ACTION_EMIT.  May be NULL when the caller does
 *                  not need the payload (still required for DROP/NONE).
 *
 * @return MODEA_ACTION_EMIT (decode ev and push), MODEA_ACTION_DROP
 *         (a half was dropped — log stats.overflow_drops), or
 *         MODEA_ACTION_NONE.  At most one event is emitted per call;
 *         later stores resolve any further backlog.
 */
enum modea_action modea_store(struct modea_state *st, enum modea_channel ch, const uint8_t *data,
			      size_t len, bool src_valid, bool has_ts, uint32_t ts, uint16_t seq,
			      struct modea_event *ev);

/** Snapshot counters (for logs/tests). */
void modea_get_stats(const struct modea_state *st, struct modea_stats *out);

#endif /* AUDIO_MODEA_H */
