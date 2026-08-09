/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the per-CIS ISO omission trackers (src/audio_iso_seq.c)
 * plus the receiver-side gap-concealment integration contract:
 *
 *   - pure sequence tracker: first, contiguous, single gap, multi-gap,
 *     16-bit wrap, duplicate, backward, over-bound resync, reset,
 *     counters;
 *   - pure timestamp-cadence tracker (audio_iso_cadence): NULL/reset/
 *     first, contiguous 10 ms and 7.5 ms grids, timestamp-only
 *     omissions, +-10 us tolerance boundaries, missing-TS callbacks
 *     without false concealment, missing-TS plus a real omitted event,
 *     LOST-style delivered callbacks, backward timestamp wrap, bounded
 *     resync classes, concealment bound, counters/reset;
 *   - Mode B / mono: an omitted callback produces exactly one PLC push
 *     per omitted SDU through the production decode helper (the helper
 *     mirror is exercised through a fake decoder + fake sink);
 *   - Mode A: simultaneous omission on both CISes produces one
 *     full-PLC stereo event before the current pair, and a one-sided
 *     gap produces one PLC-backed event with the unaffected channel
 *     valid.
 *
 * Both trackers are pure counting/ordering logic (no liblc3).  The
 * integration tests mirror the production stream_recv sequencing: run
 * the trackers, then for each omitted event conceal (Mode B = direct
 * PLC, Mode A = synthetic LOST sentinel into the assembler), then
 * process the current packet — proving the externally observable
 * contract (exact PLC accounting, cadence, no cross-pairing, no
 * double-concealment of a malformed delivered packet).
 *
 * Note: HCI packet sequence continuity does NOT prove delivery
 * continuity.  Controller-side omission (a radio event with no received
 * PDU emits no HCI SDU and consumes no sequence number on SW Split)
 * leaves seq_num contiguous while timestamps jump — the cadence tracker
 * is the independent evidence source for those omissions.
 */

#include <zephyr/ztest.h>

#include "audio_iso_seq.h"
#include "audio_modea.h"

#define INTERVAL 10000U

/* ── pure tracker tests ──────────────────────────────────────────── */

static struct audio_iso_seq sq;

static void setup_seq(void)
{
	audio_iso_seq_reset(&sq);
}

static enum audio_iso_seq_result feed(uint16_t seq, uint32_t *omitted, uint16_t *first)
{
	return audio_iso_seq_update(&sq, seq, omitted, first);
}

ZTEST(iso_seq, test_first_no_gap)
{
	setup_seq();

	uint32_t omitted = 99U;
	uint16_t first = 99U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, &omitted, &first));
	zassert_equal(0U, omitted, "first delivery must not report a gap");
	zassert_equal(0U, first);
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));
	zassert_equal(0U, audio_iso_seq_get_resyncs(&sq));
}

ZTEST(iso_seq, test_contiguous_no_gap)
{
	setup_seq();

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(101, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(102, NULL, NULL));
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));
	zassert_equal(0U, audio_iso_seq_get_resyncs(&sq));
}

ZTEST(iso_seq, test_single_gap)
{
	setup_seq();

	uint32_t omitted = 0U;
	uint16_t first = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, &omitted, &first));
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, feed(103, &omitted, &first));
	zassert_equal(2U, omitted, "seq 103 after 100 = 2 omitted (101, 102)");
	zassert_equal(101U, first);
	zassert_equal(2U, audio_iso_seq_get_concealed(&sq));

	/* Resumes contiguous after the gap. */
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(104, &omitted, &first));
	zassert_equal(0U, omitted);
	zassert_equal(2U, audio_iso_seq_get_concealed(&sq));
}

ZTEST(iso_seq, test_multi_gap_repeated)
{
	setup_seq();

	uint32_t omitted = 0U;
	uint16_t first = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(1, &omitted, &first));
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, feed(6, &omitted, &first));
	zassert_equal(4U, omitted);
	zassert_equal(2U, first);
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, feed(9, &omitted, &first));
	zassert_equal(2U, omitted, "seq 9 after 6 = 2 omitted (7, 8)");
	zassert_equal(7U, first);
	zassert_equal(6U, audio_iso_seq_get_concealed(&sq));
	zassert_equal(0U, audio_iso_seq_get_resyncs(&sq));
}

ZTEST(iso_seq, test_wrap_handles_ffff_to_0001)
{
	setup_seq();

	uint32_t omitted = 0U;
	uint16_t first = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(0xFFFE, &omitted, &first));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(0xFFFF, &omitted, &first));
	/* 0xffff -> 0x0001 is a one-SDU gap (0x0000 omitted), NOT a
	 * discontinuity. */
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, feed(0x0001, &omitted, &first));
	zassert_equal(1U, omitted);
	zassert_equal(0x0000, first);
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(0x0002, &omitted, &first));
	zassert_equal(1U, audio_iso_seq_get_concealed(&sq));
	zassert_equal(0U, audio_iso_seq_get_resyncs(&sq));
}

ZTEST(iso_seq, test_duplicate_is_explicit_resync)
{
	setup_seq();

	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, &omitted, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(101, &omitted, NULL));
	/* Same sequence delivered twice: explicit outcome, never a loop. */
	zassert_equal(AUDIO_ISO_SEQ_RES_RESYNC, feed(101, &omitted, NULL));
	zassert_equal(0U, omitted, "duplicate must not synthesize concealment");
	zassert_equal(1U, audio_iso_seq_get_resyncs(&sq));
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));

	/* Re-based on the duplicate position: next contiguous. */
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(102, &omitted, NULL));
}

ZTEST(iso_seq, test_backward_is_explicit_resync)
{
	setup_seq();

	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(1000, &omitted, NULL));
	/* An older sequence (backward) can never be a concealment loop. */
	zassert_equal(AUDIO_ISO_SEQ_RES_RESYNC, feed(500, &omitted, NULL));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_seq_get_resyncs(&sq));

	/* Forward from the re-based position resumes normally. */
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(501, &omitted, NULL));
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));
}

ZTEST(iso_seq, test_over_bound_gap_is_resync)
{
	setup_seq();

	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, &omitted, NULL));
	/* A jump larger than ISO_SEQ_MAX_CONCEAL + 1 is out-of-window:
	 * resync, no synthesis. */
	zassert_equal(AUDIO_ISO_SEQ_RES_RESYNC,
		      feed(100 + (uint16_t)ISO_SEQ_MAX_CONCEAL + 2U, &omitted, NULL));
	zassert_equal(0U, omitted, "over-bound gap must not synthesize");
	zassert_equal(1U, audio_iso_seq_get_resyncs(&sq));
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));
}

ZTEST(iso_seq, test_boundary_gap_still_conceals)
{
	setup_seq();

	uint32_t omitted = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, &omitted, NULL));
	/* Exactly MAX+1 forward = exactly MAX omitted: still a GAP. */
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP,
		      feed(100 + (uint16_t)ISO_SEQ_MAX_CONCEAL + 1U, &omitted, NULL));
	zassert_equal((uint32_t)ISO_SEQ_MAX_CONCEAL, omitted);
	zassert_equal((uint32_t)ISO_SEQ_MAX_CONCEAL, audio_iso_seq_get_concealed(&sq));
	zassert_equal(0U, audio_iso_seq_get_resyncs(&sq));
}

ZTEST(iso_seq, test_reset_clears_all)
{
	setup_seq();

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(100, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, feed(105, NULL, NULL));

	audio_iso_seq_reset(&sq);
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));
	zassert_equal(0U, audio_iso_seq_get_resyncs(&sq));

	/* Fresh start re-bases without a gap. */
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(200, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(201, NULL, NULL));
}

ZTEST(iso_seq, test_lost_callbacks_advance_like_deliveries)
{
	setup_seq();

	/* A LOST-flag callback IS a delivered event with its own sequence:
	 * it must consume its position so the next real packet stays
	 * contiguous (no double concealment of the lost event). */
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(10, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(11, NULL, NULL)); /* LOST event */
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(12, NULL, NULL)); /* real event */
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq));
}

/* ── Mode B / mono integration: omitted callback → PLC pushes ────── */

/*
 * Mirror of the production gap-concealment loop for Mode B / mono:
 * for each omitted SDU run the decode-helper PLC path, then decode the
 * current packet.  Counts pushes, decoder invocations, and PLC frames
 * exactly like the production audio_decode_sdu + audio_sink_push.
 */
struct fake_sink {
	int pushes;       /* audio_sink_push calls */
	int plc_decodes;  /* PLC decoder invocations */
	int real_decodes; /* valid decoder invocations */
	int plc_frames;   /* audio_stats plc increments (2 per Mode B SDU) */
};

static struct fake_sink sink;

static void sink_reset(void)
{
	memset(&sink, 0, sizeof(sink));
}

/* Decode helper mirror: valid=false → PLC decode (conceal), valid=true →
 * normal decode.  Mode B shape: len is the full SDU length. */
static int fake_decode(bool valid, size_t len)
{
	(void)len;
	if (!valid) {
		sink.plc_decodes++;
		/* Mode B invokes both decoders: mirror the 2-per-SDU PLC
		 * accounting of audio_decode_sdu. */
		sink.plc_frames += 2;
		return 1; /* lc3_decode PLC return */
	}
	sink.real_decodes++;
	return 0;
}

static int fake_push(void)
{
	sink.pushes++;
	return 0;
}

/* Production mirror: conceal omitted SDUs (Mode B / mono), then decode
 * the current packet.  Returns the pushes made for the omitted SDUs. */
static int conceal_mode_b(struct audio_iso_seq *st, uint16_t cur_seq, size_t shape_len)
{
	uint32_t omitted = 0U;
	uint16_t first = 0U;
	enum audio_iso_seq_result sres = audio_iso_seq_update(st, cur_seq, &omitted, &first);
	int concealed_pushes = 0;

	(void)first;
	if (sres == AUDIO_ISO_SEQ_RES_RESYNC || sres == AUDIO_ISO_SEQ_RES_FIRST) {
		return 0;
	}
	for (uint32_t i = 0U; i < omitted; i++) {
		if (fake_decode(false, shape_len) < 0) {
			continue;
		}
		fake_push();
		concealed_pushes++;
	}
	return concealed_pushes;
}

ZTEST(iso_seq, test_modeb_omitted_callback_produces_exact_plc_pushes)
{
	setup_seq();
	sink_reset();

	/* Events 1..3 contiguous, then event 4's callback is omitted:
	 * next delivered is event 5 (seq 5).  One PLC push expected. */
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(2, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(3, NULL, NULL));

	/* Full mirror: current packet seq 5 → seq 4 omitted → one PLC
	 * decode + one push; then the current packet decodes normally. */
	sink_reset();
	zassert_equal(1, conceal_mode_b(&sq, 5, 120U));
	zassert_equal(1, sink.plc_decodes);
	zassert_equal(0, sink.real_decodes);
	zassert_equal(1U, audio_iso_seq_get_concealed(&sq));

	/* The current packet itself then decodes as a normal packet. */
	fake_decode(true, 120U);
	zassert_equal(1, sink.real_decodes);
}

ZTEST(iso_seq, test_modeb_multi_omitted_cadence)
{
	setup_seq();
	sink_reset();

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(2, NULL, NULL));

	/* Three omitted (3,4,5), current = 6: three PLC pushes keep the
	 * output cadence exactly at 100 fps. */
	sink_reset();
	zassert_equal(3, conceal_mode_b(&sq, 6, 120U));
	zassert_equal(3, sink.plc_decodes);
	zassert_equal(3, sink.pushes);

	/* Next contiguous packet produces no concealment. */
	sink_reset();
	zassert_equal(0, conceal_mode_b(&sq, 7, 120U));
	zassert_equal(0, sink.plc_decodes);
	zassert_equal(0, sink.pushes);
}

ZTEST(iso_seq, test_modeb_malformed_current_not_double_concealed)
{
	setup_seq();
	sink_reset();

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, feed(2, NULL, NULL));

	/* A malformed CURRENT packet (delivered, rejected) still consumes
	 * its sequence position: the next valid packet's delta is
	 * contiguous, so the malformed one is never later concealed. */
	sink_reset();
	zassert_equal(0, conceal_mode_b(&sq, 3, 120U)); /* tracker advance */
	/* malformed rejection: no decode, no push (existing contract) */
	zassert_equal(0, sink.plc_decodes);
	zassert_equal(0, sink.real_decodes);
	zassert_equal(0, sink.pushes);

	/* Next packet contiguous → no concealment of the malformed one. */
	sink_reset();
	zassert_equal(0, conceal_mode_b(&sq, 4, 120U));
	zassert_equal(0, sink.plc_decodes);
	zassert_equal(0U, audio_iso_seq_get_concealed(&sq),
		      "malformed delivered packet is never double-concealed");
}

/* ── Mode A integration: simultaneous and one-sided omission ─────── */

/*
 * Production Mode A uses one sequence tracker PER CIS (per sink).  The
 * mirrors below keep two trackers (left/right) like the production
 * per-sink state, feed each CIS's delivered sequences, and route gaps
 * into the shared event assembler as synthetic LOST sentinels.
 */

static struct audio_iso_seq sq_l;
static struct audio_iso_seq sq_r;
static struct modea_state ma;
static struct modea_event maev;
static struct modea_stats mastats;

static void setup_modea(void)
{
	audio_iso_seq_reset(&sq_l);
	audio_iso_seq_reset(&sq_r);
	modea_reset(&ma);
	modea_config(&ma, INTERVAL);
	memset(&maev, 0, sizeof(maev));
}

/* Production mirror: feed one half into the assembler and decode/push
 * any resolved event (modea_store emits at most one event per call). */
static int modea_feed(enum modea_channel ch, const uint8_t *data, size_t len, bool src_valid,
		      bool has_ts, uint32_t ts, uint16_t seq)
{
	enum modea_action act = modea_store(&ma, ch, data, len, src_valid, has_ts, ts, seq, &maev);

	if (act == MODEA_ACTION_EMIT) {
		return 1;
	}
	return 0;
}

/* Production mirror of the Mode A gap path: for each omitted SDU feed a
 * synthetic LOST sentinel, then feed the current half. */
static void modea_conceal_gap(enum modea_channel ch, uint16_t first_seq, uint32_t omitted,
			      int *pushes, int *plc_events)
{
	for (uint32_t i = 0U; i < omitted; i++) {
		*pushes += modea_feed(ch, NULL, 0U, false, false, 0U, (uint16_t)(first_seq + i));
	}
	modea_get_stats(&ma, &mastats);
	*plc_events = (int)mastats.plc_backed_events;
}

ZTEST(iso_seq, test_modea_simultaneous_omission_full_plc_pair)
{
	setup_seq();
	setup_modea();

	/* Left event 1 + right event 1 delivered (pair).  Events 2 and 3
	 * are omitted on BOTH CISes (no callback at all).  Each channel's
	 * next callback (event 4) feeds its own missing sentinels before
	 * the current half: the pairing produces one full-PLC stereo
	 * event per omitted event, then the current pair. */
	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 10000, 1));
	zassert_equal(MODEA_ACTION_EMIT,
		      modea_feed(MODEA_CH_RIGHT, (const uint8_t *)"R", 1, true, true, 10000, 1));
	zassert_true(maev.half_valid[MODEA_CH_LEFT]);
	zassert_true(maev.half_valid[MODEA_CH_RIGHT]);
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, audio_iso_seq_update(&sq_l, 1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, audio_iso_seq_update(&sq_r, 1, NULL, NULL));

	/* Left delivers seq 4 (events 2,3 omitted on left). */
	int pushes = 0;
	int plc_events = 0;
	uint32_t omitted = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, audio_iso_seq_update(&sq_l, 4, &omitted, NULL));
	zassert_equal(2U, omitted);
	modea_conceal_gap(MODEA_CH_LEFT, 2, omitted, &pushes, &plc_events);
	/* Left's sentinels alone cannot resolve (right queue empty). */
	zassert_equal(0, pushes);

	/* Right delivers seq 4 (events 2,3 omitted on right). */
	omitted = 0U;
	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, audio_iso_seq_update(&sq_r, 4, &omitted, NULL));
	zassert_equal(2U, omitted);
	modea_conceal_gap(MODEA_CH_RIGHT, 2, omitted, &pushes, &plc_events);
	/* The two sentinel chains pair: two full-PLC events (each with
	 * both halves concealed), then the current pair stores pending. */
	zassert_equal(2, pushes, "each simultaneous omission emits one full-PLC event");
	zassert_equal(2, plc_events);

	/* Now the current halves (both real, seq 4) pair and emit. */
	pushes += modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 40000, 4);
	pushes += modea_feed(MODEA_CH_RIGHT, (const uint8_t *)"R", 1, true, true, 40000, 4);
	zassert_equal(3, pushes, "2 concealed + 1 current pair");
	modea_get_stats(&ma, &mastats);
	zassert_equal(4U, mastats.resolved_events, "event 1 + 2 concealed + current pair");
	zassert_equal(2U, mastats.plc_backed_events);
}

ZTEST(iso_seq, test_modea_one_sided_omission_plc_backed_pair)
{
	setup_seq();
	setup_modea();

	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 10000, 1));
	zassert_equal(MODEA_ACTION_EMIT,
		      modea_feed(MODEA_CH_RIGHT, (const uint8_t *)"R", 1, true, true, 10000, 1));
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, audio_iso_seq_update(&sq_l, 1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, audio_iso_seq_update(&sq_r, 1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, audio_iso_seq_update(&sq_l, 2, NULL, NULL));

	/* Only the RIGHT channel omits event 2 (left keeps delivering).
	 * Left's event 2 arrives; then right's event 3 (gap 1).  The
	 * assembler pairs left-event-2 with the synthetic right sentinel
	 * as one PLC-backed event; the unaffected left channel stays
	 * valid. */
	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 20000, 2));

	int pushes = 0;
	int plc_events = 0;
	uint32_t omitted = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, audio_iso_seq_update(&sq_r, 3, &omitted, NULL));
	zassert_equal(1U, omitted);
	modea_conceal_gap(MODEA_CH_RIGHT, 2, omitted, &pushes, &plc_events);
	zassert_equal(1, pushes, "one-sided gap emits one PLC-backed event");
	zassert_equal(1, plc_events);

	/* Event 3 pairs normally. */
	pushes += modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 30000, 3);
	pushes += modea_feed(MODEA_CH_RIGHT, (const uint8_t *)"R", 1, true, true, 30000, 3);
	zassert_equal(2, pushes);
	modea_get_stats(&ma, &mastats);
	zassert_equal(3U, mastats.resolved_events, "event 1 + concealed event 2 + event 3");
	zassert_equal(1U, mastats.plc_backed_events);
}

ZTEST(iso_seq, test_modea_never_decode_newer_before_missing_plc)
{
	setup_seq();
	setup_modea();

	/* Left delivers 1..3 normally (right silently stalls for events
	 * 2..3).  Left's callbacks have no gap (its own stream is
	 * contiguous) — the right channel's omission is revealed only
	 * when right delivers again.  The assembler must never decode a
	 * newer packet before the missing events are concealed: left's
	 * queued halves stay pending until right's sentinels resolve
	 * them in order (event 2, then event 3), and only then does the
	 * current pair (event 4) decode. */
	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 10000, 1));
	zassert_equal(MODEA_ACTION_EMIT,
		      modea_feed(MODEA_CH_RIGHT, (const uint8_t *)"R", 1, true, true, 10000, 1));
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, audio_iso_seq_update(&sq_l, 1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, audio_iso_seq_update(&sq_r, 1, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, audio_iso_seq_update(&sq_l, 2, NULL, NULL));
	zassert_equal(AUDIO_ISO_SEQ_RES_CONTIG, audio_iso_seq_update(&sq_l, 3, NULL, NULL));

	/* Left delivers events 2 and 3; both stay pending (right queue
	 * empty, no overflow at depth 2). */
	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 20000, 2));
	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 30000, 3));

	/* Right resumes at seq 4 (events 2,3 omitted): its sentinels
	 * resolve oldest-first — event 2 (left data + right PLC), then
	 * event 3 — before left's newer event can pair. */
	int pushes = 0;
	int plc_events = 0;
	uint32_t omitted = 0U;

	zassert_equal(AUDIO_ISO_SEQ_RES_GAP, audio_iso_seq_update(&sq_r, 4, &omitted, NULL));
	zassert_equal(2U, omitted);
	modea_conceal_gap(MODEA_CH_RIGHT, 2, omitted, &pushes, &plc_events);
	zassert_equal(2, pushes, "events 2 and 3 concealed in order");
	zassert_equal(2, plc_events);

	/* Left's event 4 arrives after the concealment and stays pending
	 * until right's event 4 pairs it (never decoded early). */
	zassert_equal(MODEA_ACTION_NONE,
		      modea_feed(MODEA_CH_LEFT, (const uint8_t *)"L", 1, true, true, 40000, 4));
	pushes += modea_feed(MODEA_CH_RIGHT, (const uint8_t *)"R", 1, true, true, 40000, 4);
	zassert_equal(3, pushes, "2 concealed + 1 current pair");
	modea_get_stats(&ma, &mastats);
	zassert_equal(4U, mastats.resolved_events);
	zassert_equal(2U, mastats.plc_backed_events);
	zassert_equal(0U, mastats.overflow_drops);
}

ZTEST(iso_seq, test_modea_over_bound_resync_no_synthesis)
{
	setup_seq();
	setup_modea();

	zassert_equal(AUDIO_ISO_SEQ_RES_FIRST, feed(1, NULL, NULL));

	/* Out-of-window jump: the tracker resyncs and feeds nothing, so
	 * no sentinel ever enters the assembler. */
	uint32_t omitted = 99U;
	zassert_equal(AUDIO_ISO_SEQ_RES_RESYNC,
		      feed(1 + (uint16_t)ISO_SEQ_MAX_CONCEAL + 2U, &omitted, NULL));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_seq_get_resyncs(&sq));

	modea_get_stats(&ma, &mastats);
	zassert_equal(0U, mastats.resolved_events, "resync must not synthesize events");
}

/* ── ISO timestamp-cadence tracker tests ─────────────────────────── */

static struct audio_iso_cadence cad;

static void setup_cad(void)
{
	audio_iso_cadence_reset(&cad);
}

static enum audio_iso_cadence_result cadfeed(bool has_ts, uint32_t ts, uint32_t interval,
					     uint32_t *omitted)
{
	return audio_iso_cadence_update(&cad, has_ts, ts, interval, omitted);
}

ZTEST(iso_seq, test_cadence_null_reset_first)
{
	/* NULL state: FIRST with no output mutation beyond zeroed omitted. */
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST,
		      audio_iso_cadence_update(NULL, true, 10000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(0U, audio_iso_cadence_get_concealed(NULL));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(NULL));

	setup_cad();
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(0U, omitted, "first timestamp must not report a gap");
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));

	/* Reset clears all state: a fresh base re-establishes. */
	audio_iso_cadence_reset(&cad);
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 5000, 10000, &omitted));
	zassert_equal(0U, omitted);
}

ZTEST(iso_seq, test_cadence_contiguous_10ms_and_75ms)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 20000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 30000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));

	setup_cad();
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 7500, 7500, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 15000, 7500, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 22500, 7500, &omitted));
	zassert_equal(0U, omitted, "7.5 ms grid stays contiguous");
}

ZTEST(iso_seq, test_cadence_timestamp_only_omissions)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	/* 20000 us span = 2 grid events, one delivered: one omitted. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 30000, 10000, &omitted));
	zassert_equal(1U, omitted);
	/* 30000 us span = 3 grid events, one delivered: two omitted. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 60000, 10000, &omitted));
	zassert_equal(2U, omitted);
	zassert_equal(3U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));
}

ZTEST(iso_seq, test_cadence_tolerance_boundaries)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	/* +10 us within tolerance: still exactly 2 events, 1 omitted. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 30010, 10000, &omitted));
	zassert_equal(1U, omitted);
	/* -10 us within tolerance. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 50000, 10000, &omitted));
	zassert_equal(1U, omitted);
	/* +11 us beyond tolerance: counted resync, no synthesis. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 70011, 10000, &omitted));
	zassert_equal(0U, omitted);
	/* -11 us beyond tolerance. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 90000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(2U, audio_iso_cadence_get_resyncs(&cad));
	zassert_equal(2U, audio_iso_cadence_get_concealed(&cad), "only the in-tolerance gaps");

	/* Rebased after resyncs: contiguous continues from the new base. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 100000, 10000, &omitted));
	zassert_equal(0U, omitted);
}

ZTEST(iso_seq, test_cadence_missing_ts_no_false_omission)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	/* One delivered no-TS callback between valid timestamps: two
	 * delivered positions, two grid events — zero omissions. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 30000, 10000, &omitted));
	zassert_equal(0U, omitted, "no-TS callback consumed its grid position");

	/* Multiple no-TS callbacks: every delivered position consumed. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 70000, 10000, &omitted));
	zassert_equal(0U, omitted, "4 delivered positions cover 4 grid events");
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));
}

ZTEST(iso_seq, test_cadence_missing_ts_plus_real_omission)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	/* One no-TS callback (covers position 2) then a 3-event timestamp
	 * span: 2 delivered positions, 3 grid events — exactly one
	 * omitted. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 40000, 10000, &omitted));
	zassert_equal(1U, omitted, "3 events minus 2 delivered positions");
	zassert_equal(1U, audio_iso_cadence_get_concealed(&cad));
}

ZTEST(iso_seq, test_cadence_lost_callbacks_no_extra_omission)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	/* LOST-style callback (delivered, no TS) for the next grid event
	 * consumes its position: the following valid timestamp at the
	 * following grid position stays contiguous. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 30000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));

	/* Same LOST-style callback inside a real burst: it covers one grid
	 * position and the remaining span is exactly one true omission. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 60000, 10000, &omitted));
	zassert_equal(1U, omitted, "3 grid events minus 2 delivered positions");
	zassert_equal(1U, audio_iso_cadence_get_concealed(&cad));
}

ZTEST(iso_seq, test_cadence_backward_wrap_rebase)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 20000, 10000, &omitted));
	/* Backward timestamp: nRF5340 SW Split wraps ~every 512 s.  WRAP
	 * rebases with NO synthesis and NO resync increment. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_WRAP, cadfeed(true, 3000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));

	/* Contiguous from the rebased base. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 13000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));
}

ZTEST(iso_seq, test_cadence_resync_classes)
{
	uint32_t omitted = 99U;

	/* Duplicate timestamp: zero event advance. */
	setup_cad();
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_cadence_get_resyncs(&cad));
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));
	/* Contiguous after the duplicate rebase. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 20000, 10000, &omitted));

	/* Non-integral forward delta (half-interval offset): resync. */
	setup_cad();
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 15000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_cadence_get_resyncs(&cad));

	/* Zero interval: counted resync, initialization state untouched. */
	setup_cad();
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 10000, 0, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_cadence_get_resyncs(&cad));
	/* Still uninitialized: the next valid-interval call is FIRST. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(0U, omitted);

	/* Event count below delivered positions: three no-TS callbacks then
	 * a one-interval timestamp cannot be a real omission. */
	setup_cad();
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_NO_TS, cadfeed(false, 0, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 20000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_cadence_get_resyncs(&cad));
	/* Rebased: the next interval is contiguous. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_CONTIG, cadfeed(true, 30000, 10000, &omitted));
	zassert_equal(0U, omitted);
}

ZTEST(iso_seq, test_cadence_concealment_bound)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	/* Exactly MAX+1 events in the span, one delivered: MAX omitted —
	 * accepted at the exact bound. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP,
		      cadfeed(true, 10000U + (uint32_t)(ISO_SEQ_MAX_CONCEAL + 1U) * 10000U, 10000,
			      &omitted));
	zassert_equal((uint32_t)ISO_SEQ_MAX_CONCEAL, omitted);
	zassert_equal((uint32_t)ISO_SEQ_MAX_CONCEAL, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));

	/* One more event beyond the bound: resync, no synthesis. */
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC,
		      cadfeed(true,
			      10000U + (uint32_t)(ISO_SEQ_MAX_CONCEAL + 1U) * 10000U +
				      (uint32_t)(ISO_SEQ_MAX_CONCEAL + 2U) * 10000U,
			      10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(1U, audio_iso_cadence_get_resyncs(&cad));
	zassert_equal((uint32_t)ISO_SEQ_MAX_CONCEAL, audio_iso_cadence_get_concealed(&cad),
		      "over-bound gap must not synthesize");
}

ZTEST(iso_seq, test_cadence_counters_and_reset)
{
	setup_cad();
	uint32_t omitted = 99U;

	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 10000, 10000, &omitted));
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 40000, 10000, &omitted));
	zassert_equal(2U, omitted);
	zassert_equal(AUDIO_ISO_CADENCE_RES_RESYNC, cadfeed(true, 40000, 10000, &omitted));
	zassert_equal(0U, omitted);
	zassert_equal(AUDIO_ISO_CADENCE_RES_GAP, cadfeed(true, 80000, 10000, &omitted));
	zassert_equal(3U, omitted);

	zassert_equal(5U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(1U, audio_iso_cadence_get_resyncs(&cad));

	audio_iso_cadence_reset(&cad);
	zassert_equal(0U, audio_iso_cadence_get_concealed(&cad));
	zassert_equal(0U, audio_iso_cadence_get_resyncs(&cad));
	zassert_equal(AUDIO_ISO_CADENCE_RES_FIRST, cadfeed(true, 70000, 10000, &omitted));
	zassert_equal(0U, omitted, "fresh base after reset");
}

ZTEST_SUITE(iso_seq, NULL, NULL, NULL, NULL, NULL);
