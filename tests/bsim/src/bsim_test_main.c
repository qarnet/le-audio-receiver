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
		return bsim_observer_get_gate_close() >= 1U &&
		       bsim_observer_get_release_cleanup() >= 1U;
	case BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS:
		return bsim_observer_get_release_cleanup() >= 1U;
	case BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION:
		return bsim_observer_get_config_rejected() >= 1U &&
		       bsim_observer_get_last_config_dir() == (int)BT_AUDIO_DIR_SOURCE &&
		       bsim_observer_get_last_config_code() ==
			       (int)BT_BAP_ASCS_RSP_CODE_CONF_UNSUPPORTED &&
		       bsim_observer_get_last_config_reason() == (int)BT_BAP_ASCS_REASON_NONE &&
		       bsim_observer_get_config_accepted() == 0U;
	case BSIM_SCN_NO_FREE_SINK_SLOT:
		return bsim_observer_get_config_accepted() >= 3U &&
		       bsim_observer_get_config_rejected() == 1U &&
		       bsim_observer_get_last_config_code() == (int)BT_BAP_ASCS_RSP_CODE_NO_MEM &&
		       bsim_observer_get_last_config_reason() == (int)BT_BAP_ASCS_REASON_NONE &&
		       bsim_observer_get_release_cleanup() >= 3U;
	case BSIM_SCN_INVALID_CODEC_FIELDS:
		return bsim_observer_get_config_rejected() >= 9U &&
		       bsim_observer_get_config_accepted() >= 1U &&
		       bsim_observer_get_last_config_code() ==
			       (int)BT_BAP_ASCS_RSP_CODE_CONF_INVALID &&
		       bsim_observer_get_last_config_reason() == (int)BT_BAP_ASCS_REASON_CODEC_DATA;
	default:
		return true;
	}
}

static void emit_segment_fields(const char *suffix, const struct bsim_sink_segment *s)
{
	printk("pushes%s=%u szero%s=%u splc%s=%u total%s=%u derr%s=%u mal%s=%u "
	       "h%s=0x%08X lh%s=0x%08X rh%s=0x%08X lemin%s=%d lemax%s=%d "
	       "remin%s=%d remax%s=%d samples%s=%u\n",
	       suffix, s->pushes, suffix, s->startup_zero, suffix, s->startup_plc, suffix,
	       s->total_frames, suffix, s->decode_errors, suffix, s->malformed_samples, suffix,
	       s->full_hash, suffix, s->l_hash, suffix, s->r_hash, suffix, s->l_energy_min, suffix,
	       s->l_energy_max, suffix, s->r_energy_min, suffix, s->r_energy_max, suffix,
	       s->configured_samples);
}

static void receiver_pass(enum bsim_sink_scenario scn, bool adv_restarted)
{
	struct bsim_sink_segment s0;
	struct bsim_sink_segment s1;
	bool have_s0 = audio_sink_test_get_segment(0, &s0);
	bool have_s1 = audio_sink_test_get_segment(1, &s1);
	uint32_t pushes1 = have_s0 ? s0.pushes : 0U;
	uint32_t szero1 = have_s0 ? s0.startup_zero : 0U;
	uint32_t splc1 = have_s0 ? s0.startup_plc : 0U;
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
	uint32_t szero2 = have_s1 ? s1.startup_zero : 0U;
	uint32_t splc2 = have_s1 ? s1.startup_plc : 0U;
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
	     "pushes1=%u szero1=%u splc1=%u total1=%u derr1=%u mal1=%u "
	     "h1=0x%08X lh1=0x%08X rh1=0x%08X lemin1=%d lemax1=%d remin1=%d remax1=%d "
	     "pushes2=%u szero2=%u splc2=%u total2=%u derr2=%u mal2=%u "
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
	     pushes1, szero1, splc1, total1, derr1, mal1, h1, lh1, rh1, lemin1, lemax1,
	     remin1, remax1, pushes2, szero2, splc2, total2, derr2, mal2, h2, lh2, rh2,
	     lemin2, lemax2, remin2, remax2);
}

static void scenario_main(enum bsim_sink_scenario scn, int dec_calls)
{
	int err;
	bool disc_handled = false;

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
			k_sleep(K_MSEC(100));
		} else {
			k_sleep(K_MSEC(100));
		}
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
