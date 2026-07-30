/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Forced-include header for BSIM valid-LC3 client.
 *
 * Purpose:
 * 1. First include bap_lc3_preset.h so all preset macros are defined.
 * 2. Redefine BT_BAP_LC3_UNICAST_PRESET_16_2_1 to use 48_4_1.
 *    This overrides the upstream sample's preset selection without
 *    modifying any NCS source files. The server (receiver) advertises
 *    48 kHz; the codec preset must match.
 * 3. Wrap bt_bap_stream_send() to atomically count successful TX
 *    submissions. The test installer checks the counter against a
 *    threshold and calls PASS when reached.
 */

#ifndef BSIM_CLIENT_PRESET_OVERRIDE_H
#define BSIM_CLIENT_PRESET_OVERRIDE_H

#include <stdatomic.h>

/* ── Step 1: pull in all preset definitions ──────────────────────── */
#include <zephyr/bluetooth/audio/bap_lc3_preset.h>

/* ── Step 2: override 16_2_1 → 48_4_1 ────────────────────────────── */
/* bap_lc3_preset.h defines BT_BAP_LC3_UNICAST_PRESET_16_2_1 as a
 * convenience macro. Since the header has already been included (include
 * guard prevents re-expansion), we can safely undef and redefine it.
 * Upstream main.c uses:
 *   static struct bt_bap_lc3_preset codec_configuration =
 *       BT_BAP_LC3_UNICAST_PRESET_16_2_1(...);
 * After this override, it expands to BT_BAP_LC3_UNICAST_PRESET_48_4_1(...).
 */

#undef BT_BAP_LC3_UNICAST_PRESET_16_2_1
#define BT_BAP_LC3_UNICAST_PRESET_16_2_1(loc, ctx) BT_BAP_LC3_UNICAST_PRESET_48_4_1(loc, ctx)

/* ── Step 3: wrap bt_bap_stream_send ──────────────────────────────── */
/*
 * The upstream stream_tx.c calls bt_bap_stream_send() to send LC3 frames.
 * We wrap this with a macro that intercepts the call, forwards to the real
 * API, and atomically counts successful submissions.
 *
 * Parentheses around the function name in the wrapper body prevent the
 * preprocessor from recursively expanding the macro.
 */

#include <zephyr/bluetooth/audio/bap.h>

extern atomic_int bsim_client_tx_count;

static inline int _bsim_client_send_wrap(struct bt_bap_stream *stream, struct net_buf *buf,
					 uint16_t seq_num)
{
	int ret;

	ret = (bt_bap_stream_send)(stream, buf, seq_num);
	if (ret == 0) {
		atomic_fetch_add(&bsim_client_tx_count, 1);
	}
	return ret;
}

#define bt_bap_stream_send(stream, buf, seq) _bsim_client_send_wrap((stream), (buf), (seq))

#endif /* BSIM_CLIENT_PRESET_OVERRIDE_H */
