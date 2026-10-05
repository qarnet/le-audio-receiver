/* SPDX-License-Identifier: Apache-2.0 */
#include <bstests.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>
#include "bsim_test_helpers.h"
#include "audio_sink.h"
#include "bt_bap.h"
#include "lane.h"

static bool configured, accepting;
static uint16_t input_frames = 480;
static uint32_t pushes, bad, phases;
static uint64_t left_energy, right_energy;
static bool distinct;
static K_MUTEX_DEFINE(sink_lock);
int audio_sink_init(void)
{
	configured = true;
	return 0;
}
int audio_sink_stream_open(void)
{
	accepting = true;
	return 0;
}
void audio_sink_stream_close(void)
{
	accepting = false;
}
void audio_sink_stop(void)
{
	accepting = false;
}
void audio_sink_set_input_frames(uint16_t frames)
{
	input_frames = frames;
}
int audio_sink_push(const int16_t *pcm, size_t samples)
{
	if (!configured || !accepting) {
		return -EBUSY;
	}
	k_mutex_lock(&sink_lock, K_FOREVER);
	if (!pcm || samples != (size_t)input_frames * 2) {
		bad++;
		k_mutex_unlock(&sink_lock);
		return -EINVAL;
	}
	for (size_t i = 0; i < samples; i += 2) {
		left_energy += (uint64_t)(pcm[i] < 0 ? -(int32_t)pcm[i] : pcm[i]);
		right_energy += (uint64_t)(pcm[i + 1] < 0 ? -(int32_t)pcm[i + 1] : pcm[i + 1]);
		distinct |= pcm[i] != pcm[i + 1];
	}
	pushes++;
	k_mutex_unlock(&sink_lock);
	return 0;
}
static int load(struct settings_store *store, const struct settings_load_arg *arg)
{
	(void)store;
	(void)arg;
	return 0;
}
static int save(struct settings_store *store, const char *name, const char *value, size_t bytes)
{
	(void)store;
	(void)name;
	(void)value;
	(void)bytes;
	return 0;
}
static const struct settings_store_itf settings_api = {.csi_load = load, .csi_save = save};
static struct settings_store store = {.cs_itf = &settings_api};
static volatile bool disconnected;
static void disc(struct bt_conn *conn, uint8_t reason)
{
	(void)conn;
	(void)reason;
	disconnected = true;
}
BT_CONN_CB_DEFINE(lane_conn) = {.disconnected = disc};
static void setup(void)
{
	bst_result = In_progress;
	bst_ticker_set_next_tick_absolute(240000000);
}
static void timeout(bs_time_t now)
{
	(void)now;
	if (bst_result != Passed) {
		FAIL("ASCS receiver timeout\n");
	}
}
static void run(void)
{
	bt_addr_le_t id = {.type = BT_ADDR_LE_RANDOM, .a = {.val = {0, 0, 0, 0, 0, 0xc0}}};
	settings_src_register(&store);
	settings_dst_register(&store);
	if (bt_id_create(&id, NULL) != BT_ID_DEFAULT || bt_enable(NULL) || settings_load() ||
	    bt_bap_init() || audio_sink_init() || bt_bap_restart_advertising()) {
		FAIL("ASCS receiver startup\n");
		return;
	}
	uint32_t token = 0;
	bool armed = false;
	while (bst_result == In_progress) {
		if (disconnected) {
			disconnected = false;
			if (bt_bap_restart_advertising()) {
				FAIL("ASCS adv restart\n");
				return;
			}
		}
		struct lane_message message;
		int ret = lane_receive(&message);
		if (ret == -EAGAIN) {
			k_sleep(K_MSEC(1));
			continue;
		}
		if (ret) {
			FAIL("ASCS backchannel malformed\n");
			return;
		}
		if (message.kind == ARM && message.token == token + 1 && !armed) {
			token = message.token;
			k_mutex_lock(&sink_lock, K_FOREVER);
			pushes = bad = 0;
			left_energy = right_energy = 0;
			distinct = false;
			k_mutex_unlock(&sink_lock);
			armed = true;
			lane_send(ARMED, token, 0);
		} else if (message.kind == CHECK && message.token == token && armed) {
			k_mutex_lock(&sink_lock, K_FOREVER);
			bool valid = pushes > 0 && !bad && left_energy && right_energy && distinct;
			uint32_t observed = pushes;
			k_mutex_unlock(&sink_lock);
			if (!valid) {
				FAIL("ASCS valid recovery had no complete distinct stereo rendered "
				     "output\n");
				return;
			}
			phases++;
			armed = false;
			lane_send(RENDERED, token, observed);
			printk("ASCS_RENDER phase=%u pushes=%u\n", token, observed);
		} else if (message.kind == FINISH && message.token == token && !armed && phases &&
			   message.value == phases) {
			lane_send(FINISHED, token, phases);
			PASS("ASCS_RECEIVER phases=%u\n", phases);
			return;
		} else {
			FAIL("ASCS stale/invalid phase message\n");
			return;
		}
	}
}
static const struct bst_test_instance cases[] = {
#define CASE(name)                                                                                 \
	{.test_id = name,                                                                          \
	 .test_descr = "Encoded ASCS production receiver",                                         \
	 .test_pre_init_f = setup,                                                                 \
	 .test_main_f = run,                                                                       \
	 .test_tick_f = timeout}
	CASE("control_frame_validation"),
	CASE("metadata_length_validation"),
	CASE("codec_qos"),
	CASE("dual"),
	CASE("lifecycle"),
	CASE("reconnect"),
	BSTEST_END_MARKER};
static struct bst_test_list *install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, cases);
}
bst_test_install_t test_installers[] = {install, NULL};
void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	(void)file;
	(void)line;
	k_panic();
}
int main(void)
{
	bst_main();
	return 0;
}
