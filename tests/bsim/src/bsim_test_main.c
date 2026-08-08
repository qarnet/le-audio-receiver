/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM receiver test — BT init, BAP, scenario driver, and PASS
 * composition.  One BST test ID per T4 scenario; the runner selects the
 * scenario with -testid.
 *
 * The BAP callbacks (bt_bap.c) handle incoming streams and push decoded
 * PCM to the audio sink oracle (audio_sink_stub.c).  For the disconnect
 * scenarios the driver mirrors the production main ownership: wait for
 * disconnect, restart advertising, keep serving.
 */

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>

#include <bstests.h>
#include "bsim_test_helpers.h"
#include "bsim_sink_oracle.h"
#include "bsim_observer.h"
#include "audio_sink.h"
#include "bt_bap.h"

#define TEST_TIMEOUT_US (60 * 1000000) /* 60 seconds */

static const char *scenario_names[] = {
	"",                             /* 0 */
	"mono_10ms",                    /* 1 */
	"mono_7p5ms",                   /* 2 */
	"modea_10ms",                   /* 3 */
	"modea_7p5ms",                  /* 4 */
	"modea_reverse_start_10ms",     /* 5 */
	"modeb_10ms",                   /* 6 */
	"modeb_7p5ms",                  /* 7 */
	"invalid_sdu_resume_10ms",      /* 8 */
	"modea_first_stop_10ms",        /* 9 */
	"release_without_disable_10ms", /* 10 */
	"disconnect_streaming_10ms",    /* 11 */
	"reconnect_second_stream_10ms", /* 12 */
	"unsupported_source_direction", /* 13 */
	"no_free_sink_slot",            /* 14 */
	"invalid_codec_fields",         /* 15 */
	"modea_one_cis_loss_10ms",      /* 16 */
	"duplicate_release_10ms",       /* 17 */
};

static void test_init_f(void)
{
	bst_ticker_set_next_tick_absolute(TEST_TIMEOUT_US);
	bst_result = In_progress;
}

static void test_tick_f(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("le_audio_receiver: timeout after 60 s — no PASS\n");
	}
}

static bool scenario_observer_ok(enum bsim_sink_scenario scn)
{
	switch (scn) {
	case BSIM_SCN_MODEA_FIRST_STOP_10MS:
		/* The first Disable closed the gate exactly once; BOTH
		 * slot cleanups complete (the second runs while the gate is
		 * already closed) and no Release caused the first edge.  The
		 * receiver PASSes only after the second cleanup so the
		 * record proves the full Mode A release sequence. */
		return bsim_observer_get_gate_close() == 1U &&
		       bsim_observer_get_release_cleanup() == 2U &&
		       bsim_observer_get_release_sink_stop() == 0U;
	case BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS:
		/* The ordering proof needs the disconnect event sequence too:
		 * PASS only after the client's ACL disconnect, so
		 * rel_ss_seq < disc_seq is observable in the record. */
		return bsim_observer_get_release_cleanup() == 1U &&
		       bsim_observer_get_release_sink_stop() == 1U &&
		       bsim_observer_get_disconnect_cleanup() == 1U;
	case BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION:
		return bsim_observer_get_config_rejected() >= 1U &&
		       bsim_observer_get_last_config_dir() == (int)BT_AUDIO_DIR_SOURCE &&
		       bsim_observer_get_last_config_code() ==
			       (int)BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED &&
		       bsim_observer_get_last_config_reason() == (int)BT_BAP_ASCS_REASON_NONE &&
		       bsim_observer_get_config_accepted() == 0U;
	case BSIM_SCN_NO_FREE_SINK_SLOT:
		/* Exactly three first-time slot cleanups (2 initial +
		 * 1 reuse); the NO_MEM failure consumed no slot. */
		return bsim_observer_get_config_accepted() >= 3U &&
		       bsim_observer_get_config_rejected() == 1U &&
		       bsim_observer_get_last_rej_code() == (int)BT_BAP_ASCS_RSP_CODE_NO_MEM &&
		       bsim_observer_get_last_rej_reason() == (int)BT_BAP_ASCS_REASON_NONE &&
		       bsim_observer_get_release_cleanup() == 3U;
	case BSIM_SCN_INVALID_CODEC_FIELDS:
		/* Two accepted configs overall (valid mono + missing-frame-
		 * blocks fallback); the last rejection is CONF_REJECTED.
		 * Both post-config releases complete before PASS so the
		 * record proves every accepted config's slot was cleaned. */
		return bsim_observer_get_config_rejected() >= 9U &&
		       bsim_observer_get_config_accepted() >= 2U &&
		       bsim_observer_get_last_rej_code() ==
			       (int)BT_BAP_ASCS_RSP_CODE_CONF_REJECTED &&
		       bsim_observer_get_last_rej_reason() == (int)BT_BAP_ASCS_REASON_CODEC_DATA &&
		       bsim_observer_get_release_cleanup() == 2U;
	case BSIM_SCN_DUPLICATE_RELEASE_10MS:
		/* R7 exact duplicate-release oracle: exactly two first-time
		 * slot cleanups (streaming release + reused-slot release),
		 * exactly one release sink-stop (only the streaming release
		 * closed the gate), one gate close, then the disconnect
		 * cleanup.  The duplicate same-slot release was rejected by
		 * the transport, so the cleanup count stays at two. */
		return bsim_observer_get_release_cleanup() == 2U &&
		       bsim_observer_get_release_sink_stop() == 1U &&
		       bsim_observer_get_gate_close() == 1U &&
		       bsim_observer_get_disconnect_cleanup() >= 1U;
	default:
		return true;
	}
}

static void receiver_pass(enum bsim_sink_scenario scn, bool adv_restarted)
{
	struct bsim_sink_segment s0;
	struct bsim_sink_segment s1;
	bool have_s0 = audio_sink_test_get_segment(0, &s0);
	bool have_s1 = audio_sink_test_get_segment(1, &s1);
	uint32_t pushes1 = have_s0 ? s0.pushes : 0U;
	uint32_t trans1 = have_s0 ? s0.transients : 0U;
	uint32_t szero1 = have_s0 ? s0.startup_zero : 0U;
	uint32_t splc1 = have_s0 ? s0.startup_plc : 0U;
	uint32_t plc1 = have_s0 ? s0.plc_frames : 0U;
	uint32_t total1 = have_s0 ? s0.total_frames : 0U;
	uint32_t derr1 = have_s0 ? s0.decode_errors : 0U;
	uint32_t mal1 = have_s0 ? s0.malformed_samples : 0U;
	uint32_t h1 = have_s0 ? s0.full_hash : 0U;
	uint32_t lh1 = have_s0 ? s0.l_hash : 0U;
	uint32_t rh1 = have_s0 ? s0.r_hash : 0U;
	int32_t lemin1 = have_s0 ? s0.l_energy_min : 0;
	int32_t lemax1 = have_s0 ? s0.l_energy_max : 0;
	int32_t remin1 = have_s0 ? s0.r_energy_min : 0;
	int32_t remax1 = have_s0 ? s0.r_energy_max : 0;
	uint32_t pushes2 = have_s1 ? s1.pushes : 0U;
	uint32_t trans2 = have_s1 ? s1.transients : 0U;
	uint32_t szero2 = have_s1 ? s1.startup_zero : 0U;
	uint32_t splc2 = have_s1 ? s1.startup_plc : 0U;
	uint32_t plc2 = have_s1 ? s1.plc_frames : 0U;
	uint32_t total2 = have_s1 ? s1.total_frames : 0U;
	uint32_t derr2 = have_s1 ? s1.decode_errors : 0U;
	uint32_t mal2 = have_s1 ? s1.malformed_samples : 0U;
	uint32_t h2 = have_s1 ? s1.full_hash : 0U;
	uint32_t lh2 = have_s1 ? s1.l_hash : 0U;
	uint32_t rh2 = have_s1 ? s1.r_hash : 0U;
	int32_t lemin2 = have_s1 ? s1.l_energy_min : 0;
	int32_t lemax2 = have_s1 ? s1.l_energy_max : 0;
	int32_t remin2 = have_s1 ? s1.r_energy_min : 0;
	int32_t remax2 = have_s1 ? s1.r_energy_max : 0;

	PASS("le_audio_receiver: scenario=%s seg=%u after=%u adv_restart=%u pacs=1 "
	     "obs_ok=%u obs_rej=%u obs_dir=%d obs_code=%d obs_reason=%d "
	     "obs_gate_o=%u obs_gate_c=%u obs_mal=%u obs_blk=%u obs_stale=%u "
	     "obs_rel=%u obs_disc=%u obs_rej_code=%d obs_rej_reason=%d "
	     "obs_mts=%u obs_rel_ss=%u rel_ss_seq=%u disc_seq=%u "
	     "pushes1=%u trans1=%u szero1=%u splc1=%u plc1=%u total1=%u derr1=%u mal1=%u "
	     "h1=0x%08X lh1=0x%08X rh1=0x%08X lemin1=%d lemax1=%d remin1=%d remax1=%d "
	     "pushes2=%u trans2=%u szero2=%u splc2=%u plc2=%u total2=%u derr2=%u mal2=%u "
	     "h2=0x%08X lh2=0x%08X rh2=0x%08X lemin2=%d lemax2=%d remin2=%d remax2=%d\n",
	     scenario_names[scn], audio_sink_test_segment_count(),
	     audio_sink_test_after_stop_total(), adv_restarted ? 1U : 0U,
	     bsim_observer_get_config_accepted(), bsim_observer_get_config_rejected(),
	     bsim_observer_get_last_config_dir(), bsim_observer_get_last_config_code(),
	     bsim_observer_get_last_config_reason(), bsim_observer_get_gate_open(),
	     bsim_observer_get_gate_close(), bsim_observer_get_malformed_sdu(),
	     bsim_observer_get_recv_gate_blocked(), bsim_observer_get_stale_half(),
	     bsim_observer_get_release_cleanup(), bsim_observer_get_disconnect_cleanup(),
	     bsim_observer_get_last_rej_code(), bsim_observer_get_last_rej_reason(),
	     bsim_observer_get_missing_ts(), bsim_observer_get_release_sink_stop(),
	     bsim_observer_get_release_sink_stop_seq(), bsim_observer_get_disconnect_seq(), pushes1,
	     trans1, szero1, splc1, plc1, total1, derr1, mal1, h1, lh1, rh1, lemin1, lemax1, remin1,
	     remax1, pushes2, trans2, szero2, splc2, plc2, total2, derr2, mal2, h2, lh2, rh2,
	     lemin2, lemax2, remin2, remax2);
}

static void scenario_main(enum bsim_sink_scenario scn, int dec_calls)
{
	int err;
	bool disc_handled = false;
	uint32_t last_disc = 0U;

	printk("=== LE Audio Receiver BSIM Test — scenario %s ===\n", scenario_names[scn]);

	err = bt_enable(NULL);
	if (err) {
		FAIL("le_audio_receiver: Bluetooth init failed: %d\n", err);
		return;
	}
	printk("BLE ready\n");

	/* settings_load required for dynamic PACS/ASCS registration.
	 * No persistent storage in bsim — the settings_none backend
	 * returns success without loading anything. */
	err = settings_load();
	if (err) {
		FAIL("le_audio_receiver: settings_load failed: %d\n", err);
		return;
	}
	printk("settings_load OK\n");

	err = bt_bap_init();
	if (err) {
		FAIL("le_audio_receiver: BAP init failed: %d\n", err);
		return;
	}

	err = audio_sink_init();
	if (err) {
		FAIL("le_audio_receiver: audio sink init failed: %d\n", err);
		return;
	}

	audio_sink_test_begin(scn, dec_calls);

	err = bt_bap_restart_advertising();
	if (err) {
		FAIL("le_audio_receiver: advertising start failed: %d\n", err);
		return;
	}

	printk("Advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);

	while (bst_result == In_progress) {
		if (audio_sink_test_goal_reached() && scenario_observer_ok(scn) &&
		    audio_sink_test_validate()) {
			receiver_pass(scn, disc_handled);
			return;
		}

		if (scn == BSIM_SCN_DISCONNECT_STREAMING_10MS ||
		    scn == BSIM_SCN_RECONNECT_SECOND_STREAM_10MS) {
			if (!disc_handled) {
				/* Production main ownership: wait for the ACL
				 * disconnect, then restart advertising. */
				bt_bap_wait_disconnect();
				err = bt_bap_restart_advertising();
				if (err) {
					FAIL("le_audio_receiver: advertising restart failed: %d\n",
					     err);
					return;
				}
				printk("Advertising restarted after disconnect\n");
				disc_handled = true;
			}
		} else if (bsim_observer_get_disconnect_cleanup() > last_disc) {
			/* Multi-connection scenarios (reconnect, source rejection,
			 * NO_MEM, invalid codec fields): the client disconnects
			 * between rounds — restart advertising exactly like the
			 * production main loop so the next round can reconnect. */
			last_disc = bsim_observer_get_disconnect_cleanup();
			err = bt_bap_restart_advertising();
			if (err) {
				FAIL("le_audio_receiver: advertising restart failed: %d\n", err);
				return;
			}
			printk("Advertising restarted after disconnect\n");
		}
		k_sleep(K_MSEC(100));
	}

	/* If we reach here without PASS, the tick timeout will FAIL */
}

#define SCENARIO_MAIN(_scn, _dec)                                                                  \
	static void test_main_##_scn(void)                                                         \
	{                                                                                          \
		scenario_main(_scn, _dec);                                                         \
	}

SCENARIO_MAIN(BSIM_SCN_MONO_10MS, 1)
SCENARIO_MAIN(BSIM_SCN_MONO_7P5MS, 1)
SCENARIO_MAIN(BSIM_SCN_MODEA_10MS, 2)
SCENARIO_MAIN(BSIM_SCN_MODEA_7P5MS, 2)
SCENARIO_MAIN(BSIM_SCN_MODEA_REVERSE_START_10MS, 2)
SCENARIO_MAIN(BSIM_SCN_MODEB_10MS, 2)
SCENARIO_MAIN(BSIM_SCN_MODEB_7P5MS, 2)
SCENARIO_MAIN(BSIM_SCN_INVALID_SDU_RESUME_10MS, 1)
SCENARIO_MAIN(BSIM_SCN_MODEA_FIRST_STOP_10MS, 2)
SCENARIO_MAIN(BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS, 1)
SCENARIO_MAIN(BSIM_SCN_DISCONNECT_STREAMING_10MS, 1)
SCENARIO_MAIN(BSIM_SCN_RECONNECT_SECOND_STREAM_10MS, 1)
SCENARIO_MAIN(BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION, 1)
SCENARIO_MAIN(BSIM_SCN_NO_FREE_SINK_SLOT, 1)
SCENARIO_MAIN(BSIM_SCN_INVALID_CODEC_FIELDS, 1)
SCENARIO_MAIN(BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS, 2)
SCENARIO_MAIN(BSIM_SCN_DUPLICATE_RELEASE_10MS, 1)

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "mono_10ms",
		.test_descr = "T4 mono 10 ms — strict oracle",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MONO_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "mono_7p5ms",
		.test_descr = "T4 mono 7.5 ms — strict oracle",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MONO_7P5MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_10ms",
		.test_descr = "T4 Mode A 10 ms — strict oracle",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEA_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_7p5ms",
		.test_descr = "T4 Mode A 7.5 ms — strict oracle",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEA_7P5MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_reverse_start_10ms",
		.test_descr = "T4 Mode A 10 ms reverse start",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEA_REVERSE_START_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_one_cis_loss_10ms",
		.test_descr = "T4 Mode A 10 ms with 3 scheduled right-CIS losses",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modeb_10ms",
		.test_descr = "T4 Mode B 10 ms — strict oracle",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEB_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modeb_7p5ms",
		.test_descr = "T4 Mode B 7.5 ms — strict oracle",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEB_7P5MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "invalid_sdu_resume_10ms",
		.test_descr = "T4 malformed SDU then resume",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_INVALID_SDU_RESUME_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_first_stop_10ms",
		.test_descr = "T4 first Mode A ASE stops",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEA_FIRST_STOP_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "release_without_disable_10ms",
		.test_descr = "T4 release without disable",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "disconnect_streaming_10ms",
		.test_descr = "T4 disconnect while streaming",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_DISCONNECT_STREAMING_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "reconnect_second_stream_10ms",
		.test_descr = "T4 reconnect and second stream",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_RECONNECT_SECOND_STREAM_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "unsupported_source_direction",
		.test_descr = "T4 source-direction rejection",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "no_free_sink_slot",
		.test_descr = "T4 NO_MEM on third sink",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_NO_FREE_SINK_SLOT,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "invalid_codec_fields",
		.test_descr = "T4 invalid codec field variants",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_INVALID_CODEC_FIELDS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "modea_one_cis_loss_10ms",
		.test_descr = "T4 Mode A 10 ms with 3 scheduled right-CIS losses",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_MODEA_ONE_CIS_LOSS_10MS,
		.test_tick_f = test_tick_f,
	},
	{
		.test_id = "duplicate_release_10ms",
		.test_descr = "R7 duplicate same-slot release rejected + slot reuse",
		.test_pre_init_f = test_init_f,
		.test_main_f = test_main_BSIM_SCN_DUPLICATE_RELEASE_10MS,
		.test_tick_f = test_tick_f,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_le_audio_receiver_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

bst_test_install_t test_installers[] = {
	test_le_audio_receiver_install,
	NULL,
};
