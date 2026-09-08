/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Native public-boundary suite for the dedicated source app coordinator.
 *
 * Compiles the real coordinator + output formatter against the scripted
 * fake backend and the production core modules. Tests prove public
 * behavior: exact ordered HIL1 records, state/counters, the backend
 * operation ledger, cleanup outcomes, output queue validation, and
 * formatter truncation fail-closed behavior.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fake_hil_source_backend.h"
#include "hil_source_app.h"
#include "hil_source_output.h"
#include "hil_source_types.h"

#define RUN_ID    "run-1"
#define CMD_CFG   "cmd-configure"
#define CMD_START "cmd-start"
#define CMD_STAT  "cmd-status"
#define CMD_STOP  "cmd-stop"
#define CMD_IDLE  "cmd-idle"
#define CMD_UNP   "cmd-unpair"
#define CMD_HELLO "cmd-hello"
#define PEER_ADDR "AA:BB:CC:DD:EE:FF"

#define PUMP_BUDGET_MS 180000U

/* ── output capture ──────────────────────────────────────────────── */

#define CAP_MAX  256
#define CAP_LINE 1100

static char captured[CAP_MAX][CAP_LINE];
static uint32_t cap_count;

static void capture_fn(const char *line, size_t len, void *arg)
{
	ARG_UNUSED(arg);
	if (cap_count < CAP_MAX) {
		size_t n = MIN(len, CAP_LINE - 1U);

		memcpy(captured[cap_count], line, n);
		captured[cap_count][n] = '\0';
		cap_count++;
	}
}

static void cap_reset(void)
{
	cap_count = 0U;
}

static void drain(void)
{
	hil_source_output_test_drain();
}

static bool cap_find(const char *needle)
{
	uint32_t i;

	for (i = 0U; i < cap_count; i++) {
		if (strstr(captured[i], needle) != NULL) {
			return true;
		}
	}
	return false;
}

static bool cap_terminal(const char *verdict)
{
	char needle[64];

	snprintf(needle, sizeof(needle), "\"verdict\":\"%s\"", verdict);
	return cap_find(needle);
}

/* Collect (segment, state) pairs from state records in capture order. */
#define STATE_PAIRS_MAX 64
static struct state_pair {
	uint32_t segment;
	char state[24];
} state_pairs[STATE_PAIRS_MAX];
static uint32_t state_pair_count;

static void collect_state_pairs(void)
{
	uint32_t i;

	state_pair_count = 0U;
	for (i = 0U; i < cap_count && state_pair_count < STATE_PAIRS_MAX; i++) {
		const char *seg;
		const char *st;
		const char *end;

		if (strstr(captured[i], "\"kind\":\"state\"") == NULL) {
			continue;
		}
		seg = strstr(captured[i], "\"segment\":");
		st = strstr(captured[i], "\"state\":\"");
		if (st == NULL) {
			continue;
		}
		st += strlen("\"state\":\"");
		end = strchr(st, '"');
		if (end == NULL) {
			continue;
		}
		{
			struct state_pair *p = &state_pairs[state_pair_count++];

			p->segment = (seg != NULL) ? (uint32_t)strtoul(seg + strlen("\"segment\":"),
								       NULL, 10)
						   : 0U;
			snprintf(p->state, sizeof(p->state), "%.*s",
				 (int)MIN((size_t)(end - st), sizeof(p->state) - 1U), st);
		}
	}
}

static int cap_extract_int(const char *needle)
{
	uint32_t i;
	int last = INT_MIN;

	for (i = 0U; i < cap_count; i++) {
		const char *hit = strstr(captured[i], needle);

		if (hit != NULL) {
			last = (int)strtol(hit + strlen(needle), NULL, 10);
		}
	}
	return last;
}

static uint32_t cap_state_record_count(void)
{
	uint32_t i;
	uint32_t n = 0U;

	for (i = 0U; i < cap_count; i++) {
		if (strstr(captured[i], "\"kind\":\"state\"") != NULL) {
			n++;
		}
	}
	return n;
}

static uint32_t cap_segment_of_state(const char *state)
{
	uint32_t i;

	for (i = 0U; i < cap_count; i++) {
		const char *st;
		char needle[64];

		snprintf(needle, sizeof(needle), "\"state\":\"%s\"", state);
		if (strstr(captured[i], needle) == NULL) {
			continue;
		}
		st = strstr(captured[i], "\"segment\":");
		if (st != NULL) {
			return (uint32_t)strtoul(st + strlen("\"segment\":"), NULL, 10);
		}
	}
	return UINT32_MAX;
}

static bool state_pairs_match(const char *const *expect, uint32_t count, uint32_t segment)
{
	uint32_t i;
	uint32_t j = 0U;

	for (i = 0U; i < state_pair_count; i++) {
		if (state_pairs[i].segment != segment) {
			continue;
		}
		if (j >= count || strcmp(state_pairs[i].state, expect[j]) != 0) {
			return false;
		}
		j++;
	}
	return j == count;
}

/* ── pump ────────────────────────────────────────────────────────── */

static uint8_t sig_count[FAKE_OP_COUNT];
static uint32_t serviced[FAKE_OP_COUNT];
static bool pump_sents_enabled;
static uint64_t pump_skip;
static uint32_t pump_iterations;

static void pump_reset(uint8_t streams)
{
	memset(sig_count, 0, sizeof(sig_count));
	memset(serviced, 0, sizeof(serviced));
	sig_count[FAKE_OP_CONNECT] = 1U;
	sig_count[FAKE_OP_SECURITY] = 1U;
	sig_count[FAKE_OP_DISCOVER] = 1U;
	sig_count[FAKE_OP_CONFIGURE] = streams;
	sig_count[FAKE_OP_QOS] = streams;
	/* Coordinator submits enable one stream at a time.  Each enable kick
	 * therefore gets exactly one completion token. */
	sig_count[FAKE_OP_ENABLE] = 1U;
	/* One completion per accepted stream-connect kick.  Fake backend keeps
	 * one request pending at a time, matching NCS bt_bap_stream_connect(). */
	sig_count[FAKE_OP_STREAM_CONNECT] = 1U;
	sig_count[FAKE_OP_START] = streams;
	sig_count[FAKE_OP_DISABLE] = 1U;
	sig_count[FAKE_OP_RELEASE] = 0U; /* auto-detach on release */
	sig_count[FAKE_OP_DISCONNECT] = 1U;
	pump_sents_enabled = true;
	pump_skip = 0U;
	pump_iterations = 0U;
}

static void pump_once(void)
{
	uint32_t op;
	uint8_t i;

	drain();
	for (op = 0U; op < FAKE_OP_COUNT; op++) {
		uint32_t kicks;

		if (op == FAKE_OP_ENABLE) {
			kicks = fake_enable_accepted_count();
		} else if (op == FAKE_OP_STREAM_CONNECT) {
			kicks = fake_stream_connect_accepted_count();
		} else {
			kicks = fake_kick_count((enum fake_op)op);
		}

		if (sig_count[op] == 0U || (pump_skip & (1ULL << op)) != 0U) {
			continue;
		}
		while (serviced[op] < kicks) {
			serviced[op]++;
			fake_signal((enum fake_op)op, sig_count[op]);
		}
	}
	if (pump_sents_enabled) {
		for (i = 0U; i < 2U; i++) {
			if (fake_sent_suppressed(i)) {
				continue;
			}
			while (fake_sent_count(i) < fake_send_count(i)) {
				fake_signal_sent(i);
			}
		}
	}
	k_sleep(K_MSEC(1));
}

static bool pump_until(bool (*done)(void), uint32_t budget_ms)
{
	uint32_t waited = 0U;

	while (waited < budget_ms) {
		if (done()) {
			return true;
		}
		pump_once();
		waited += 1U;
	}
	return done();
}

static bool terminal_seen(void)
{
	return cap_terminal("pass") || cap_terminal("fail");
}

static bool pump_until_terminal(void)
{
	return pump_until(terminal_seen, PUMP_BUDGET_MS);
}

/* ── scenario helpers ────────────────────────────────────────────── */

static void app_reset(void)
{
	char json[256];

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"idle\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_IDLE, RUN_ID);
	(void)hil_source_app_dispatch(json, strlen(json));
	drain();
}

static void dispatch_json(const char *json)
{
	(void)hil_source_app_dispatch(json, strlen(json));
	drain();
}

static void scenario_setup(enum hil_source_mode mode, enum hil_source_profile profile,
			   uint32_t target, const char *policy)
{
	uint8_t streams = hil_source_mode_stream_count(mode);

	/* Self-sufficient environment: output first, backend injected, then
	 * the coordinator (all idempotent; test_boot probes the order guard
	 * before any scenario runs). */
	zassert_equal(hil_source_output_init(), 0, "output init");
	hil_source_app_test_set_backend(fake_backend_ops());
	zassert_equal(hil_source_app_init(), 0, "app init");
	app_reset();
	fake_backend_reset();
	cap_reset();
	pump_reset(streams);
	/* Happy-path scripting defaults. */
	fake_set_conn_present(true);
	fake_set_security_level(2U);
	fake_set_discovered_sinks(2U);
	fake_set_bond_count(0);
}

static void dispatch_configure(enum hil_source_mode mode, enum hil_source_profile profile,
			       uint32_t target, const char *policy)
{
	const char *mode_name = hil_source_mode_name(mode);
	const char *profile_name = hil_source_profile_name(profile);
	char json[512];

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"configure\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\",\"peer_address\":\"%s\",\"peer_address_type\":\"random\","
		 "\"mode\":\"%s\",\"profile\":\"%s\",\"scored_sdu_count\":%u,"
		 "\"signal_seed\":123456789,\"reconnect_policy\":\"%s\"}",
		 CMD_CFG, RUN_ID, PEER_ADDR, mode_name, profile_name, (unsigned int)target, policy);
	dispatch_json(json);
}

static void dispatch_start(void)
{
	char json[256];

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"start\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_START, RUN_ID);
	dispatch_json(json);
}

static void dispatch_stop(void)
{
	char json[256];

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"stop\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_STOP, RUN_ID);
	dispatch_json(json);
}

static void dispatch_status(void)
{
	char json[256];

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"status\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_STAT, RUN_ID);
	dispatch_json(json);
}

static int dispatch_idle_command(void)
{
	char json[256];

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"idle\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_IDLE, RUN_ID);
	return hil_source_app_dispatch(json, strlen(json));
}

static void dispatch_idle(void)
{
	(void)dispatch_idle_command();
	drain();
}

static K_THREAD_STACK_DEFINE(idle_dispatch_stack, 4096);
static struct k_thread idle_dispatch_thread;
static K_SEM_DEFINE(sem_idle_dispatch_request, 0, 1);
static K_SEM_DEFINE(sem_idle_dispatch_done, 0, 1);
static bool idle_dispatch_thread_started;
static int idle_dispatch_result;

static void idle_dispatch_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&sem_idle_dispatch_request, K_FOREVER);
		idle_dispatch_result = dispatch_idle_command();
		k_sem_give(&sem_idle_dispatch_done);
	}
}

static void start_idle_dispatch(void)
{
	if (!idle_dispatch_thread_started) {
		k_thread_create(&idle_dispatch_thread, idle_dispatch_stack,
				K_THREAD_STACK_SIZEOF(idle_dispatch_stack), idle_dispatch_thread_fn,
				NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
		idle_dispatch_thread_started = true;
	}
	k_sem_reset(&sem_idle_dispatch_done);
	k_sem_give(&sem_idle_dispatch_request);
}

static bool idle_dispatch_done(void)
{
	return k_sem_take(&sem_idle_dispatch_done, K_NO_WAIT) == 0;
}

/* Completion choreography for TX pacing regression.  Priority 5 is lower
 * than the coordinator worker's priority 4: the worker records the final
 * submission needed to reach the outstanding target and reaches its
 * backpressure wait before this thread delivers the completion. */
static K_THREAD_STACK_DEFINE(tx_completion_stack, 2048);
static struct k_thread tx_completion_thread;
static K_SEM_DEFINE(sem_tx_depth_target, 0, 1);
static K_SEM_DEFINE(sem_tx_completion_done, 0, 1);
static K_SEM_DEFINE(sem_tx_next_send, 0, 1);
static bool tx_completion_thread_started;

static void tx_completion_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&sem_tx_depth_target, K_FOREVER);
		fake_signal_sent(0);
		k_sem_give(&sem_tx_completion_done);
	}
}

static void start_tx_completion(void)
{
	if (!tx_completion_thread_started) {
		k_thread_create(&tx_completion_thread, tx_completion_stack,
				K_THREAD_STACK_SIZEOF(tx_completion_stack), tx_completion_thread_fn,
				NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
		tx_completion_thread_started = true;
	}
	k_sem_reset(&sem_tx_depth_target);
	k_sem_reset(&sem_tx_completion_done);
	k_sem_reset(&sem_tx_next_send);
	fake_set_depth_target_signal(0, &sem_tx_depth_target);
}

static bool tx_completion_done(void)
{
	return k_sem_take(&sem_tx_completion_done, K_NO_WAIT) == 0;
}

static uint32_t profile_preamble(enum hil_source_profile profile)
{
	return (profile == HIL_SOURCE_PROFILE_48_3_1) ? 192U : 144U;
}

static uint32_t profile_tail(enum hil_source_profile profile)
{
	return (profile == HIL_SOURCE_PROFILE_48_3_1) ? 667U : 500U;
}

/* Cleanup phase order for the ledger contract: TX_STOP -> all DISABLE ->
 * all RELEASE -> DISCONNECT -> GROUP_DELETE -> optional CONN_UNREF ->
 * RESET_SEGMENT.  Operations may be absent when their resources were
 * never acquired, but phase regression is never allowed. */
enum cl_phase {
	CL_TX_STOP,
	CL_DISABLE,
	CL_RELEASE,
	CL_DISCONNECT,
	CL_GROUP,
	CL_UNREF,
	CL_RESET,
};

/* Assert the cleanup ledger subsequence for a full clean teardown with
 * strict global phase ordering (not just per-op counts). */
static void assert_cleanup_order_full(uint8_t streams)
{
	const struct fake_record *ledger = fake_ledger();
	uint32_t n = fake_ledger_count();
	enum cl_phase phase = CL_TX_STOP;
	uint32_t i;
	uint32_t disables = 0U;
	uint32_t releases = 0U;
	bool saw_disconnect = false;
	bool saw_group = false;
	bool saw_unref = false;
	bool saw_reset = false;

	for (i = 0U; i < n; i++) {
		switch (ledger[i].op) {
		case FAKE_OP_TX_STOP:
			zassert_true(phase <= CL_TX_STOP, "tx stop phase");
			phase = CL_DISABLE;
			break;
		case FAKE_OP_DISABLE:
			zassert_true(phase <= CL_DISABLE, "disable phase");
			phase = CL_DISABLE;
			zassert_equal(ledger[i].stream_idx, disables, "disable order");
			disables++;
			break;
		case FAKE_OP_RELEASE:
			zassert_true(phase <= CL_RELEASE, "release phase");
			phase = CL_RELEASE;
			zassert_equal(ledger[i].stream_idx, releases, "release order");
			releases++;
			break;
		case FAKE_OP_DISCONNECT:
			zassert_true(phase <= CL_DISCONNECT, "disconnect phase");
			phase = CL_DISCONNECT;
			zassert_false(saw_disconnect, "duplicate disconnect");
			saw_disconnect = true;
			break;
		case FAKE_OP_GROUP_DELETE:
			zassert_true(phase <= CL_GROUP, "group delete phase");
			phase = CL_GROUP;
			zassert_false(saw_group, "duplicate group delete");
			saw_group = true;
			break;
		case FAKE_OP_CONN_UNREF:
			zassert_true(phase <= CL_UNREF, "conn unref phase");
			phase = CL_UNREF;
			zassert_false(saw_unref, "duplicate conn unref");
			saw_unref = true;
			break;
		case FAKE_OP_RESET_SEGMENT:
			zassert_true(phase <= CL_RESET, "reset phase");
			phase = CL_RESET;
			zassert_false(saw_reset, "duplicate reset");
			saw_reset = true;
			break;
		default:
			break;
		}
	}
	zassert_equal(disables, streams, "disable count");
	zassert_equal(releases, streams, "release count");
	zassert_true(saw_disconnect, "disconnect missing");
	zassert_true(saw_group, "group delete missing");
	zassert_true(saw_unref, "conn unref missing");
	zassert_true(saw_reset, "reset missing");
}

static void run_full_lifecycle(enum hil_source_mode mode, enum hil_source_profile profile,
			       uint32_t target)
{
	uint8_t streams = hil_source_mode_stream_count(mode);
	uint32_t total = profile_preamble(profile) + target + profile_tail(profile);
	uint8_t i;

	scenario_setup(mode, profile, target, "none");
	dispatch_configure(mode, profile, target, "none");
	zassert_true(cap_find("\"command\":\"configure\",\"ok\":true"), "configure failed");
	zassert_true(cap_find("\"mode\":\"mono\"") || cap_find("\"mode\":\"mode_a\"") ||
			     cap_find("\"mode\":\"mode_b\""),
		     "configure mode missing");

	dispatch_start();
	zassert_true(cap_find("\"kind\":\"ack\""), "start ack missing");

	zassert_true(pump_until_terminal(), "terminal not reached");
	zassert_true(cap_terminal("pass"), "expected pass, got fail");

	collect_state_pairs();
	{
		const char *expect[] = {"idle",      "configured",      "connecting",
					"secured",   "discovered",      "qos",
					"streaming", "scored_complete", "teardown"};

		zassert_true(state_pairs_match(expect, ARRAY_SIZE(expect), 0U), "state order");
	}
	for (i = 0U; i < streams; i++) {
		/* Stream 0 carries one additional untimestamped CIG-anchor SDU.
		 * It is outside source `sub` and scored-signal accounting. */
		zassert_equal(fake_send_count(i), total + ((i == 0U) ? 1U : 0U),
			      "stream send count");
	}
	zassert_equal(fake_kick_count(FAKE_OP_RESET_SEGMENT), 1U, "reset count");
	zassert_equal(fake_run_mode(), mode, "run shape mode");
	zassert_equal(fake_run_profile(), profile, "run shape profile");
	assert_cleanup_order_full(streams);

	/* Immutable terminal snapshot under a status query command ID. */
	dispatch_status();
	{
		char needle[64];

		snprintf(needle, sizeof(needle), "\"sub\":%u", (unsigned int)total);
		zassert_true(cap_find(needle), "terminal submitted");
		snprintf(needle, sizeof(needle), "\"sc\":%u", (unsigned int)target);
		zassert_true(cap_find(needle), "terminal scored");
		zassert_true(cap_find("\"verdict\":\"pass\""), "terminal verdict");
		zassert_true(cap_find("\"sf\":0"), "no send failures");
		zassert_true(cap_find("\"connected\":false"), "conn closed in snapshot");
	}

	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle after run failed");
}

static void *suite_setup(void)
{
	/* Capture every submitted line; the output queue is drained by the
	 * test helpers, so no writer thread runs in the test build. */
	hil_source_output_test_set_capture(capture_fn, NULL);
	return NULL;
}

ZTEST_SUITE(hil_source_app, NULL, suite_setup, NULL, NULL, NULL);

/* ── 1. boot init order and fatal-stop ───────────────────────────── */

ZTEST(hil_source_app, test_boot_init_order_and_fatal)
{
	/* No backend injected yet: app init fails cleanly. */
	zassert_equal(hil_source_app_init(), -EINVAL, "init without backend");

	/* Backend injected but output not initialized: order guard. */
	hil_source_app_test_set_backend(fake_backend_ops());
	zassert_equal(hil_source_app_init(), -EIO, "init without output");

	/* Output first, then the app initializes. */
	zassert_equal(hil_source_output_init(), 0, "output init");
	zassert_equal(hil_source_app_init(), 0, "app init");

	/* Fatal boot failure emits one best-effort HIL1 status record. */
	hil_source_app_fatal_status("bap_init", -EIO);
	drain();
	zassert_true(cap_find("\"command_id\":\"boot\""), "boot record command id");
	zassert_true(cap_find("\"phase\":\"bap_init\""), "boot record phase");
	zassert_true(cap_find("\"errno\":-5"), "boot record errno");

	/* Coordinator is idle and responsive after init. */
	cap_reset();
	hil_source_app_fatal_status("app_init", -ENOMEM);
	drain();
	zassert_true(cap_find("\"phase\":\"app_init\""), "app_init phase");
}

/* ── 2. hello exact constants ────────────────────────────────────── */

ZTEST(hil_source_app, test_hello_constants)
{
	char json[256];

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_identity(PEER_ADDR, 1U);
	fake_set_bond_count(3);

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_HELLO, RUN_ID);
	dispatch_json(json);

	zassert_true(cap_find("\"firmware_id\":\"le-audio-hil-source-rh1\""), "firmware id");
	zassert_true(cap_find("\"protocol_version\":1"), "protocol version");
	zassert_true(cap_find("\"identity\":\"AA:BB:CC:DD:EE:FF\""), "identity");
	zassert_true(cap_find("\"identity_type\":\"random\""), "identity type");
	zassert_true(cap_find("\"active\":false"), "not active");
	zassert_true(cap_find("\"state\":\"idle\""), "idle state");
	zassert_true(cap_find("\"bond_count\":3"), "bond count");
	zassert_true(cap_find("\"sample_rate\":48000"), "sample rate");
	zassert_true(cap_find("\"frame_samples_48_3_1\":360"), "48_3_1 samples");
	zassert_true(cap_find("\"octets_48_3_1\":90"), "48_3_1 octets");
	zassert_true(cap_find("\"sdu_mono_48_3_1\":90"), "48_3_1 mono sdu");
	zassert_true(cap_find("\"sdu_modeb_48_3_1\":180"), "48_3_1 modeb sdu");
	zassert_true(cap_find("\"frame_samples_48_4_1\":480"), "48_4_1 samples");
	zassert_true(cap_find("\"octets_48_4_1\":120"), "48_4_1 octets");
	zassert_true(cap_find("\"sdu_mono_48_4_1\":120"), "48_4_1 mono sdu");
	zassert_true(cap_find("\"sdu_modeb_48_4_1\":240"), "48_4_1 modeb sdu");
	zassert_true(cap_find("\"preamble_samples\":69120"), "preamble samples");
	zassert_true(cap_find("\"tail_frames_48_3_1\":667"), "tail 48_3_1");
	zassert_true(cap_find("\"tail_frames_48_4_1\":500"), "tail 48_4_1");
	zassert_true(cap_find("\"left_carrier_hz\":997"), "left carrier");
	zassert_true(cap_find("\"right_carrier_hz\":1601"), "right carrier");
	zassert_true(cap_find("\"default_seed\":1218649181"), "default seed");
}

/* ── 3. configure/start/status success + ID correlation ──────────── */

ZTEST(hil_source_app, test_configure_start_status_correlation)
{
	char json[256];

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	zassert_true(cap_find("\"command\":\"configure\",\"ok\":true"), "configure ok");

	/* Status before start: snapshot under the query command ID. */
	dispatch_status();
	zassert_true(cap_find("\"command\":\"status\",\"ok\":true"), "status ok");
	zassert_true(cap_find("\"command_id\":\"cmd-status\""), "query command id");
	zassert_true(cap_find("\"mode\":\"mono\""), "mode in status");
	zassert_true(cap_find("\"scored_target\":10"), "scored target");

	/* Start with a mismatched run ID is rejected. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"start\",\"command_id\":\"%s\","
		 "\"run_id\":\"run-other\"}",
		 CMD_START);
	dispatch_json(json);
	zassert_true(cap_find("\"error\":\"invalid_request\""), "start wrong run rejected");

	/* Status with a mismatched run ID is rejected. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"status\",\"command_id\":\"%s\","
		 "\"run_id\":\"run-other\"}",
		 CMD_STAT);
	dispatch_json(json);
	zassert_true(cap_find("\"error\":\"invalid_request\""), "status wrong run rejected");

	dispatch_start();
	zassert_true(cap_find("\"kind\":\"ack\""), "start ack");
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	/* Status after terminal still reports the immutable snapshot. */
	dispatch_status();
	zassert_true(cap_find("\"verdict\":\"pass\""), "terminal snapshot");
}

/* ── 4. invalid parse reserved diagnostic IDs ────────────────────── */

ZTEST(hil_source_app, test_parse_error_reserved_ids)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Malformed JSON. */
	dispatch_json("{not-json");
	zassert_true(cap_find("\"command_id\":\"parse-error\""), "parse-error command id");
	zassert_true(cap_find("\"run_id\":\"unbound\""), "parse-error run id");
	zassert_true(cap_find("\"error\":\"parse_error\""), "parse error name");
	zassert_true(cap_find("\"parse_result\":\"syntax\""), "syntax result");

	/* Unknown key. */
	dispatch_json("{\"protocol_version\":1,\"command\":\"hello\",\"command_id\":\"x\","
		      "\"run_id\":\"y\",\"bogus\":1}");
	zassert_true(cap_find("\"parse_result\":\"unknown_key\""), "unknown key result");
}

/* ── 5. busy/mismatched run/missing config rejection ─────────────── */

ZTEST(hil_source_app, test_rejections)
{
	char json[256];

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Start before configure. */
	dispatch_start();
	zassert_true(cap_find("\"error\":\"not_configured\""), "start unconfigured");

	/* Status before configure. */
	dispatch_status();
	zassert_true(cap_find("\"error\":\"invalid_request\""), "status unconfigured");

	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(cap_find("\"kind\":\"ack\""), "ack");

	/* Start while active is busy. */
	dispatch_start();
	zassert_true(cap_find("\"error\":\"busy\""), "start busy");

	/* Configure while active is busy. */
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	zassert_true(cap_find("\"error\":\"busy\""), "configure busy");

	/* Unpair while active is busy. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"unpair\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\",\"peer_address\":\"%s\",\"peer_address_type\":\"random\"}",
		 CMD_UNP, RUN_ID, PEER_ADDR);
	dispatch_json(json);
	zassert_true(cap_find("\"error\":\"busy\""), "unpair busy");

	/* Stop and let the run abort, then idle for the next test. */
	dispatch_stop();
	zassert_true(pump_until_terminal(), "stop terminal");
	zassert_true(cap_terminal("fail"), "stop fail");
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle");
}

/* ── 6. start handler returns before worker completes ────────────── */

ZTEST(hil_source_app, test_start_returns_before_worker)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Dispatch returned; the ack exists but the worker has not yet
	 * reached the radio (no connect kick, no terminal). */
	zassert_true(cap_find("\"kind\":\"ack\""), "ack emitted");
	zassert_equal(fake_kick_count(FAKE_OP_CONNECT), 0U, "no connect yet");
	zassert_false(cap_terminal("pass") || cap_terminal("fail"), "no terminal yet");

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");
	dispatch_idle();
}

/* ── 7. full mono / Mode A / Mode B lifecycle, both profiles ─────── */

ZTEST(hil_source_app, test_lifecycle_mono_48_4_1)
{
	run_full_lifecycle(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U);
}

ZTEST(hil_source_app, test_lifecycle_mono_48_3_1)
{
	run_full_lifecycle(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_3_1, 10U);
}

ZTEST(hil_source_app, test_lifecycle_modea_48_4_1)
{
	run_full_lifecycle(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U);
}

ZTEST(hil_source_app, test_lifecycle_modea_48_3_1)
{
	run_full_lifecycle(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_3_1, 10U);
}

ZTEST(hil_source_app, test_modea_enable_serialized)
{
	const struct fake_record *ledger;
	uint32_t enable_records = 0U;
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Hold completion for the first enable.  The worker may submit stream 0,
	 * but it must remain blocked before submitting stream 1. */
	pump_skip = 1ULL << FAKE_OP_ENABLE;
	dispatch_start();
	while (fake_kick_count(FAKE_OP_ENABLE) == 0U) {
		pump_once();
	}
	zassert_equal(fake_kick_count(FAKE_OP_ENABLE), 1U, "first enable only");
	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op == FAKE_OP_ENABLE) {
			zassert_equal(ledger[i].stream_idx, 0U, "first enable stream");
			break;
		}
	}

	for (i = 0U; i < 10U; i++) {
		pump_once();
	}
	zassert_equal(fake_kick_count(FAKE_OP_ENABLE), 1U,
		      "second enable waits for first completion");

	/* Release exactly one completion token per enable kick. */
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "serialized enable terminal");
	zassert_true(cap_terminal("pass"), "serialized enable pass");

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_ENABLE) {
			continue;
		}
		zassert_equal(ledger[i].stream_idx, enable_records, "enable stream order");
		enable_records++;
	}
	zassert_equal(enable_records, 2U, "one enable per Mode A stream");
	dispatch_idle();
}

ZTEST(hil_source_app, test_modea_enable_busy_retry)
{
	static const uint8_t expected_streams[] = {0U, 1U, 1U};
	const struct fake_record *ledger;
	uint32_t enable_records = 0U;
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_enable_busy_attempts(1U, 1U);
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "busy retry terminal");
	zassert_true(cap_terminal("pass"), "busy retry pass");

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_ENABLE) {
			continue;
		}
		zassert_true(enable_records < ARRAY_SIZE(expected_streams),
			     "too many enable attempts");
		zassert_equal(ledger[i].stream_idx, expected_streams[enable_records],
			      "enable stream order/retry");
		enable_records++;
	}
	zassert_equal(enable_records, ARRAY_SIZE(expected_streams), "enable attempt count");
	zassert_equal(fake_enable_busy_rejection_count(), 1U, "one busy rejection");
	zassert_equal(fake_enable_accepted_count(), 2U, "accepted enable count");
	zassert_equal(fake_enable_completion_count(), fake_enable_accepted_count(),
		      "one completion per accepted enable");
	dispatch_idle();
}

ZTEST(hil_source_app, test_modea_stream_connect_serialized)
{
	const struct fake_record *ledger;
	uint32_t connect_records = 0U;
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Hold the real stream-connected completion.  The coordinator may kick
	 * stream 0, but the public .connected gate must block stream 1. */
	pump_skip = 1ULL << FAKE_OP_STREAM_CONNECT;
	dispatch_start();
	while (fake_kick_count(FAKE_OP_STREAM_CONNECT) == 0U) {
		pump_once();
	}
	zassert_equal(fake_stream_connect_accepted_count(), 1U, "stream 0 accepted");
	for (i = 0U; i < 10U; i++) {
		pump_once();
	}
	zassert_equal(fake_kick_count(FAKE_OP_STREAM_CONNECT), 1U,
		      "stream 1 waits for stream 0 completion");
	zassert_equal(fake_stream_connect_completion_count(), 0U,
		      "no synthetic completion while pending");

	/* Deliver stream 0's actual completion, then allow the second request. */
	fake_signal(FAKE_OP_STREAM_CONNECT, 1U);
	pump_skip = 0U;
	while (fake_kick_count(FAKE_OP_STREAM_CONNECT) < 2U) {
		pump_once();
	}

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_STREAM_CONNECT) {
			continue;
		}
		zassert_equal(ledger[i].stream_idx, connect_records, "CIS stream order");
		connect_records++;
	}
	zassert_equal(connect_records, 2U, "two stream-connect kicks");
	zassert_equal(fake_stream_connect_busy_rejection_count(), 0U, "no pending-CIS rejection");
	zassert_equal(fake_stream_connect_accepted_count(), 2U, "two accepted connections");

	zassert_true(pump_until_terminal(), "serialized stream-connect terminal");
	zassert_true(cap_terminal("pass"), "serialized stream-connect pass");
	zassert_equal(fake_stream_connect_completion_count(), 2U,
		      "one completion per accepted connection");
	dispatch_idle();
}

ZTEST(hil_source_app, test_modea_stream_connect_transient_failure_retry)
{
	const struct fake_record *ledger;
	uint32_t connect_records = 0U;
	uint32_t total = profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
			 profile_tail(HIL_SOURCE_PROFILE_48_4_1);
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Hold stream-connect completions so the test can inject one failed
	 * second-CIS establishment before allowing its one retry to complete. */
	pump_skip = 1ULL << FAKE_OP_STREAM_CONNECT;
	dispatch_start();
	while (fake_stream_connect_accepted_count() < 1U) {
		pump_once();
	}
	fake_signal(FAKE_OP_STREAM_CONNECT, 1U);
	while (fake_stream_connect_accepted_count() < 2U) {
		pump_once();
	}
	fake_signal_stream_connect_failure(1U);
	while (fake_stream_connect_accepted_count() < 3U) {
		pump_once();
	}
	pump_skip = 0U;

	zassert_true(pump_until_terminal(), "transient retry terminal");
	zassert_true(cap_terminal("pass"), "transient retry pass");
	zassert_equal(fake_send_count(0), total + 1U,
		      "stream 0 transmitted expected data plus anchor");
	zassert_equal(fake_send_count(1), total, "stream 1 transmitted expected data");
	zassert_equal(fake_stream_connect_accepted_count(), 3U, "three accepted connections");
	zassert_equal(fake_stream_connect_completion_count(), 2U, "two successful completions");
	zassert_equal(fake_stream_connect_failure_count(), 1U, "one failed-CIS event");
	zassert_equal(fake_stream_connect_busy_rejection_count(), 0U, "no pending-CIS rejection");

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_STREAM_CONNECT) {
			continue;
		}
		zassert_true(connect_records < 3U, "too many stream-connect attempts");
		zassert_equal(ledger[i].stream_idx, (connect_records == 0U) ? 0U : 1U,
			      "same-stream retry order");
		connect_records++;
	}
	zassert_equal(connect_records, 3U, "three stream-connect attempts");
	dispatch_idle();
}

ZTEST(hil_source_app, test_modea_stream_connect_persistent_failure)
{
	const struct fake_record *ledger;
	uint32_t connect_records = 0U;
	uint32_t t0;
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	pump_skip = 1ULL << FAKE_OP_STREAM_CONNECT;
	dispatch_start();
	while (fake_stream_connect_accepted_count() < 1U) {
		pump_once();
	}
	fake_signal(FAKE_OP_STREAM_CONNECT, 1U);
	while (fake_stream_connect_accepted_count() < 2U) {
		pump_once();
	}
	fake_signal_stream_connect_failure(1U);
	while (fake_stream_connect_accepted_count() < 3U) {
		pump_once();
	}
	t0 = k_uptime_get_32();
	fake_signal_stream_connect_failure(1U);
	pump_skip = 0U;

	zassert_true(pump_until_terminal(), "persistent failure terminal");
	zassert_true(cap_terminal("fail"), "persistent failure fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""),
		     "persistent failure error teardown");
	zassert_true(k_uptime_get_32() - t0 < HIL_SOURCE_OP_TIMEOUT_STREAM_MS,
		     "persistent failure did not wait for stream timeout");
	zassert_equal(fake_stream_connect_accepted_count(), 3U, "three accepted connections");
	zassert_equal(fake_stream_connect_completion_count(), 1U,
		      "stream 0 only successful completion");
	zassert_equal(fake_stream_connect_failure_count(), 2U, "two failed-CIS events");
	zassert_equal(fake_stream_connect_busy_rejection_count(), 0U, "no pending-CIS rejection");
	zassert_equal(fake_kick_count(FAKE_OP_RESET_SEGMENT), 1U, "cleanup completed");

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_STREAM_CONNECT) {
			continue;
		}
		zassert_true(connect_records < 3U, "fourth stream-connect attempt");
		zassert_equal(ledger[i].stream_idx, (connect_records == 0U) ? 0U : 1U,
			      "persistent failure attempt order");
		connect_records++;
	}
	zassert_equal(connect_records, 3U, "no fourth stream-connect attempt");

	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "persistent failure first errno");
	zassert_false(cap_find("\"cause\":\"timeout\""), "not ordinary timeout teardown");
	assert_cleanup_order_full(2U);
	dispatch_idle();
}

ZTEST(hil_source_app, test_lifecycle_modeb_48_4_1)
{
	run_full_lifecycle(HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_4_1, 10U);
}

ZTEST(hil_source_app, test_lifecycle_modeb_48_3_1)
{
	run_full_lifecycle(HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_3_1, 10U);
}

/* ── 8. reconnect: segment 1 starts at connecting, fresh seq ─────── */

ZTEST(hil_source_app, test_reconnect_segment)
{
	uint32_t total = profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
			 profile_tail(HIL_SOURCE_PROFILE_48_4_1);
	const struct fake_record *ledger;
	uint32_t n;
	uint32_t i;
	bool saw_second_connect = false;
	bool first_send_after_reconnect_seen = false;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "once");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "once");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	collect_state_pairs();
	{
		const char *seg0[] = {"idle",      "configured",      "connecting",
				      "secured",   "discovered",      "qos",
				      "streaming", "scored_complete", "teardown"};
		const char *seg1[] = {"connecting", "secured",         "discovered", "qos",
				      "streaming",  "scored_complete", "teardown"};

		zassert_true(state_pairs_match(seg0, ARRAY_SIZE(seg0), 0U), "segment 0 order");
		zassert_true(state_pairs_match(seg1, ARRAY_SIZE(seg1), 1U), "segment 1 order");
	}

	zassert_equal(fake_kick_count(FAKE_OP_CONNECT), 2U, "two connects");
	zassert_equal(fake_kick_count(FAKE_OP_RESET_SEGMENT), 2U, "two resets");
	zassert_equal(fake_send_count(0), 2U * (total + 1U),
		      "two segments of sends plus one anchor each");

	/* Explicit TX activation: exactly one activation per segment, and the
	 * TX stop that separates the two segments cleared the driver so the
	 * reconnect segment reactivates with a fresh sequence. */
	zassert_equal(fake_kick_count(FAKE_OP_TX_START), 2U, "one tx_start per segment");
	zassert_equal(fake_kick_count(FAKE_OP_TX_STOP), 2U, "one tx_stop per segment cleanup");
	ledger = fake_ledger();
	n = fake_ledger_count();
	{
		uint32_t tx_start_seen = 0U;
		bool tx_start_before_send = false;

		for (i = 0U; i < n; i++) {
			if (ledger[i].op == FAKE_OP_TX_START) {
				tx_start_seen++;
				continue;
			}
			if (tx_start_seen > 0U && ledger[i].op == FAKE_OP_TX_SEND) {
				tx_start_before_send = true;
				break;
			}
		}
		zassert_true(tx_start_before_send, "first send after activation");
	}

	/* Segment 1 starts with a fresh sequence counter: the first TX send
	 * after the second connect has seq 0. */
	saw_second_connect = false;
	for (i = 0U; i < n; i++) {
		if (ledger[i].op == FAKE_OP_CONNECT) {
			if (saw_second_connect) {
				break;
			}
			saw_second_connect = true;
			continue;
		}
		if (saw_second_connect && ledger[i].op == FAKE_OP_TX_SEND) {
			zassert_equal(ledger[i].seq, 0U, "fresh sequence in segment 1");
			first_send_after_reconnect_seen = true;
			break;
		}
	}
	zassert_true(first_send_after_reconnect_seen, "segment 1 send seen");

	dispatch_status();
	zassert_true(cap_find("\"sc\":20"), "two segments of scored");
	dispatch_idle();
}

/* ── 4. invalid parse reserved diagnostic IDs ────────────────────── */

/* ── 9. stop during each lifecycle stage ─────────────────────────── */

static void assert_abort_stop_and_cleanup(void)
{
	zassert_true(cap_terminal("fail"), "stop terminal fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"stop\""), "abort stop record");
	dispatch_status();
	zassert_true(cap_find("\"aborted\":true"), "aborted flag");
	zassert_true(cap_find("\"cause\":\"stop\""), "abort cause");
}

ZTEST(hil_source_app, test_stop_during_connect)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	pump_skip = 1ULL << FAKE_OP_CONNECT;
	while (fake_kick_count(FAKE_OP_CONNECT) == 0U) {
		pump_once();
	}
	dispatch_stop();
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "terminal");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

ZTEST(hil_source_app, test_stop_during_security)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	pump_skip = 1ULL << FAKE_OP_SECURITY;
	while (fake_kick_count(FAKE_OP_SECURITY) == 0U) {
		pump_once();
	}
	dispatch_stop();
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "terminal");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

ZTEST(hil_source_app, test_stop_during_discovery)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	pump_skip = 1ULL << FAKE_OP_DISCOVER;
	while (fake_kick_count(FAKE_OP_DISCOVER) == 0U) {
		pump_once();
	}
	dispatch_stop();
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "terminal");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

ZTEST(hil_source_app, test_stop_during_qos)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	pump_skip = 1ULL << FAKE_OP_QOS;
	while (fake_kick_count(FAKE_OP_QOS) == 0U) {
		pump_once();
	}
	dispatch_stop();
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "terminal");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

ZTEST(hil_source_app, test_stop_during_streaming)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	dispatch_stop();
	zassert_true(pump_until_terminal(), "terminal");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

ZTEST(hil_source_app, test_stop_during_tail)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Stop once the scored phase completed (tail about to start). */
	while (!cap_find("\"state\":\"scored_complete\"")) {
		pump_once();
	}
	dispatch_stop();
	zassert_true(pump_until_terminal(), "terminal");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

/* ── 10. every timeout boundary ──────────────────────────────────── */

static void run_timeout_scenario(enum fake_op op)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	pump_skip = 1ULL << op;
	zassert_true(pump_until_terminal(), "terminal for op %d", (int)op);
	pump_skip = 0U;
	zassert_true(cap_terminal("fail"), "timeout fail for op %d", (int)op);
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"timeout\""),
		     "timeout cause for op %d", (int)op);
	dispatch_status();
	zassert_true(cap_find("\"cause\":\"timeout\""), "abort cause timeout");
	dispatch_idle();
}

ZTEST(hil_source_app, test_timeout_connect)
{
	run_timeout_scenario(FAKE_OP_CONNECT);
}

ZTEST(hil_source_app, test_timeout_security)
{
	run_timeout_scenario(FAKE_OP_SECURITY);
}

ZTEST(hil_source_app, test_timeout_discover)
{
	run_timeout_scenario(FAKE_OP_DISCOVER);
}

ZTEST(hil_source_app, test_timeout_configure)
{
	run_timeout_scenario(FAKE_OP_CONFIGURE);
}

ZTEST(hil_source_app, test_timeout_qos)
{
	run_timeout_scenario(FAKE_OP_QOS);
}

ZTEST(hil_source_app, test_timeout_enable)
{
	run_timeout_scenario(FAKE_OP_ENABLE);
}

ZTEST(hil_source_app, test_timeout_stream_connect)
{
	run_timeout_scenario(FAKE_OP_STREAM_CONNECT);
}

ZTEST(hil_source_app, test_timeout_start)
{
	run_timeout_scenario(FAKE_OP_START);
}

/* ── 11. backend/send/cleanup failures ───────────────────────────── */

ZTEST(hil_source_app, test_error_connect_kick)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_kick_result(FAKE_OP_CONNECT, -ENOMEM);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""), "error abort");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-12"), "first errno preserved");
	dispatch_idle();
}

ZTEST(hil_source_app, test_error_connect_async)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_op_error(-EIO);
	fake_set_conn_present(false);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"cause\":\"error\""), "async error abort");
	dispatch_idle();
}

ZTEST(hil_source_app, test_error_tx_send)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_kick_result(FAKE_OP_TX_SEND, -EIO);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""), "send error abort");
	zassert_true(fake_send_count(0) < profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
						  profile_tail(HIL_SOURCE_PROFILE_48_4_1),
		     "run stopped by send failure");
	dispatch_status();
	zassert_true(cap_find("\"sf\":1"), "send failure counted");
	zassert_true(cap_find("\"first_errno\":-5"), "send errno recorded");
	dispatch_idle();
}

/* ── SDC timestamp-mode provisioning ─────────────────────────────── */

ZTEST(hil_source_app, test_tsmode_first_send_plain_then_pinned)
{
	uint32_t total = profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
			 profile_tail(HIL_SOURCE_PROFILE_48_4_1);
	const struct fake_record *first_plain = NULL;
	const struct fake_record *first_pinned = NULL;
	char needle[160];

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	{
		const struct fake_record *ledger = fake_ledger();
		uint32_t n = fake_ledger_count();
		uint32_t plain = 0U;
		uint32_t pinned_sends = 0U;
		uint32_t i;

		for (i = 0U; i < n; i++) {
			if (ledger[i].op == FAKE_OP_TX_SEND) {
				if (first_plain == NULL) {
					first_plain = &ledger[i];
				}
				plain++;
			} else if (ledger[i].op == FAKE_OP_TX_SEND_TS) {
				if (first_pinned == NULL) {
					first_pinned = &ledger[i];
				}
				pinned_sends++;
			}
		}
		/* Exactly one untimestamped SDU (the base-learning probe);
		 * every later SDU is pinned. */
		zassert_equal(plain, 1U, "exactly one plain first send");
		zassert_true(pinned_sends + 1U == fake_send_count(0), "all later sends pinned");
		/* CIS central shares controller timing. One completion-anchored
		 * readback establishes the full segment grid. */
		zassert_equal(fake_kick_count(FAKE_OP_TX_READ_TX_TS), 1U,
			      "one CIG anchor readback");
		zassert_true(fake_ts_time_get_count() > pinned_sends,
			     "controller time checked at gate and submission");
		zassert_not_null(first_plain, "anchor record");
		zassert_equal(first_plain->stream_idx, 0U, "anchor stream");
		zassert_equal(first_plain->seq, 0U, "anchor sequence");
		zassert_not_null(first_pinned, "first regular record");
		zassert_equal(first_pinned->seq, 1U, "stream 0 sequence follows anchor");
	}
	dispatch_status();
	snprintf(needle, sizeof(needle),
		 "\"seq\":%u,\"sub\":%u,\"sc\":10,\"sf\":0,\"cb\":%u,\"out\":0",
		 (unsigned int)(total + 1U), (unsigned int)total, (unsigned int)total);
	zassert_true(cap_find(needle), "anchor excluded from scored counters");
	zassert_true(cap_find("\"tx\":{\"anchor\":100000,\"pin\":"), "compact TX diagnostics");
	zassert_true(cap_find("\"lead\":{\"min\":[3000],\"max\":[3000],\"under\":[0]}"),
		     "controller-relative lead envelope");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_pins_advance_one_interval)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	{
		const struct fake_record *ledger = fake_ledger();
		uint32_t n = fake_ledger_count();
		uint32_t prev_ts = 0U;
		bool have_prev = false;
		uint32_t i;

		uint32_t first_pin = 0U;
		bool have_first = false;

		for (i = 0U; i < n; i++) {
			if (ledger[i].op == FAKE_OP_TX_SEND_TS) {
				if (!have_first) {
					first_pin = ledger[i].ts;
					have_first = true;
				}
				if (have_prev) {
					/* Consecutive pins differ by exactly
					 * one SDU interval (10000 us for
					 * 48_4_1): one pinned SDU per ISO
					 * event. */
					zassert_equal(ledger[i].ts - prev_ts, 10000U,
						      "pin advance");
				}
				prev_ts = ledger[i].ts;
				have_prev = true;
			}
		}
		zassert_true(have_prev, "pinned sends exist");
		zassert_equal(first_pin, 110000U, "first pin follows assigned event");
	}
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_stale_pin_catches_up_one_event)
{
	const struct fake_record *ledger;
	uint32_t i;
	bool found = false;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	/* Controller-now is one interval later than the normal gate-open time.
	 * Catch-up must skip one event and submit with 3000 us lead. */
	fake_ts_set_initial_time_offset(10000);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op == FAKE_OP_TX_SEND_TS) {
			zassert_equal(ledger[i].ts, 120000U, "stale first pin advanced once");
			found = true;
			break;
		}
	}
	zassert_true(found, "timestamped send");
	dispatch_status();
	zassert_true(cap_find("\"skip\":1"), "one skipped event reported");
	zassert_true(cap_find("\"lead\":{\"min\":[3000],\"max\":[3000],\"under\":[0]}"),
		     "catch-up restored target lead");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_wrap_uses_modulo_controller_time)
{
	const uint32_t assigned = UINT32_MAX - 5000U;
	const uint32_t first_expected = assigned + 10000U;
	const struct fake_record *ledger;
	uint32_t previous = 0U;
	uint32_t i;
	bool have_previous = false;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_ts_set_readback_base(assigned);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_TX_SEND_TS) {
			continue;
		}
		if (!have_previous) {
			zassert_equal(ledger[i].ts, first_expected, "first pin wraps");
			have_previous = true;
		} else {
			zassert_equal(ledger[i].ts - previous, 10000U,
				      "wrapped pins retain interval");
		}
		previous = ledger[i].ts;
	}
	zassert_true(have_previous, "timestamped sends");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_readback_error_fails_closed)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_ts_set_readback_result(-EIO);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_equal(fake_send_count(0), 1U, "only anchor submitted");
	zassert_equal(fake_ts_send_count(0), 0U, "no unanchored regular send");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "readback errno preserved");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_controller_time_error_fails_closed)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_ts_set_time_result(-EIO);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_equal(fake_send_count(0), 1U, "only anchor submitted");
	zassert_equal(fake_ts_send_count(0), 0U, "no send without controller time");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "clock errno preserved");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_frozen_controller_time_times_out)
{
	bool terminal;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_ts_set_time_frozen(true);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	terminal = pump_until(terminal_seen, HIL_SOURCE_TX_PROGRESS_TIMEOUT_MS + 1000U);
	if (!terminal) {
		/* Leave the fixture recoverable when running this regression against
		 * the pre-fix scheduler, which otherwise waits forever. */
		fake_ts_set_time_frozen(false);
		zassert_true(pump_until_terminal(), "pre-fix cleanup");
	}
	zassert_true(terminal, "frozen controller clock terminates within progress budget");
	zassert_true(cap_terminal("fail"), "frozen controller clock fails closed");
	zassert_equal(fake_send_count(0), 2U, "anchor and one regular SDU submitted");
	zassert_equal(fake_ts_send_count(0), 1U, "no further sends while controller is frozen");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-110"), "progress timeout preserved");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tsmode_modea_late_peer_fails_closed)
{
	char needle[48];

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_ts_set_peer_submit_offset(1500);
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_equal(fake_ts_send_count(0), 1U, "stream 0 committed shared pin");
	zassert_equal(fake_ts_send_count(1), 0U, "late peer not submitted");
	dispatch_status();
	snprintf(needle, sizeof(needle), "\"first_errno\":%d", -ETIME);
	zassert_true(cap_find(needle), "late-peer errno preserved");
	zassert_true(cap_find("\"under\":[0,1]"), "late peer counted");
	dispatch_idle();
}

ZTEST(hil_source_app, test_error_cleanup_continues)
{
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_kick_result(FAKE_OP_DISABLE, -EIO);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");

	/* Every cleanup operation was still attempted in order. */
	{
		const struct fake_record *ledger = fake_ledger();
		uint32_t n = fake_ledger_count();
		bool saw_disable = false;
		bool saw_release = false;
		bool saw_disconnect = false;
		bool saw_group = false;
		bool saw_unref = false;
		bool saw_reset = false;

		for (i = 0U; i < n; i++) {
			switch (ledger[i].op) {
			case FAKE_OP_DISABLE:
				saw_disable = true;
				break;
			case FAKE_OP_RELEASE:
				saw_release = true;
				break;
			case FAKE_OP_DISCONNECT:
				saw_disconnect = true;
				break;
			case FAKE_OP_GROUP_DELETE:
				saw_group = true;
				break;
			case FAKE_OP_CONN_UNREF:
				saw_unref = true;
				break;
			case FAKE_OP_RESET_SEGMENT:
				saw_reset = true;
				break;
			default:
				break;
			}
		}
		zassert_true(saw_disable, "disable attempted");
		zassert_true(saw_release, "release attempted");
		zassert_true(saw_disconnect, "disconnect attempted");
		zassert_true(saw_group, "group delete attempted");
		zassert_true(saw_unref, "conn unref attempted");
		zassert_true(saw_reset, "reset attempted");
	}

	/* The cleanup failure is preserved as the first error. */
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "cleanup errno preserved");
	dispatch_idle();
}

/* ── 12. status during active run does not mutate ────────────────── */

ZTEST(hil_source_app, test_status_no_mutate)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"discovered\"")) {
		pump_once();
	}
	{
		/* Snapshot the record count; a status query must not add state
		 * records or change the segment. */
		uint32_t states_before = cap_state_record_count();
		uint32_t seg = cap_segment_of_state("discovered");

		cap_reset();
		dispatch_status();
		zassert_true(cap_find("\"command\":\"status\",\"ok\":true"), "status during run");
		zassert_true(cap_find("\"command_id\":\"cmd-status\""), "query command id");
		zassert_true(cap_find("\"active\":true"), "active");
		zassert_true(cap_find("\"verdict\":\"none\""), "active status no terminal verdict");
		zassert_true(cap_find("\"state\":\"discovered\""), "state in status unchanged");
		zassert_true(cap_find("\"segment\":0"), "segment unchanged");
		zassert_equal(cap_state_record_count(), 0U, "no state records from status");
		zassert_equal(states_before, 5U, "states before: idle..discovered");
		zassert_equal(seg, 0U, "discovered in segment 0");
	}

	/* The run continues normally after the status query. */
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");
	dispatch_idle();
}

static bool modea_outstanding_target_reached(void);
static bool send_count_one_reached(void);

ZTEST(hil_source_app, test_status_during_streaming_uses_cached_tx_sync)
{
	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	pump_sents_enabled = false;
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Release only the bootstrap. Regular sends then fill both outstanding
	 * windows and leave worker blocked in a stable streaming state. */
	zassert_true(pump_until(send_count_one_reached, 10000U), "anchor send");
	fake_signal_sent(0);
	zassert_true(pump_until(modea_outstanding_target_reached, 10000U), "outstanding windows");
	zassert_equal(fake_kick_count(FAKE_OP_TX_READ_SYNC), 0U, "no TX-sync capture before drain");

	cap_reset();
	dispatch_status();
	zassert_true(cap_find("\"command\":\"status\",\"ok\":true"), "status during streaming");
	zassert_true(cap_find("\"state\":\"streaming\""), "streaming snapshot");
	zassert_equal(fake_kick_count(FAKE_OP_TX_READ_SYNC), 0U,
		      "status uses cached telemetry only");

	pump_sents_enabled = true;
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "run survives active status");
	zassert_equal(fake_kick_count(FAKE_OP_TX_READ_SYNC), 2U,
		      "worker captures one final sync sample per stream");
	dispatch_idle();
}

/* ── 13. output queue validation/overflow, formatter truncation ──── */

ZTEST(hil_source_app, test_output_validation)
{
	/* Non-HIL1 prefix rejected. */
	zassert_not_equal(hil_source_output_submit("hello world", 11U, 10U), 0, "prefix");
	/* Embedded CR/LF/NUL rejected. */
	zassert_not_equal(hil_source_output_submit("HIL1 a\nb", 8U, 10U), 0, "newline");
	zassert_not_equal(hil_source_output_submit("HIL1 a\rb", 8U, 10U), 0, "cr");
	zassert_not_equal(hil_source_output_submit("HIL1 a", 7U, 10U), 0, "nul");
	/* Overlong line rejected. */
	{
		char big[HIL_SOURCE_OUTPUT_LINE_SIZE + 16U];

		memset(big, 'x', sizeof(big));
		memcpy(big, "HIL1 ", 5U);
		zassert_not_equal(hil_source_output_submit(big, sizeof(big), 10U), 0, "overflow");
	}
}

ZTEST(hil_source_app, test_output_queue_overflow)
{
	char line[HIL_SOURCE_OUTPUT_LINE_SIZE];
	uint32_t i;
	int ret;

	/* Fill the queue (depth 8) without draining; the 9th submit must
	 * fail with the bounded timeout. */
	for (i = 0U; i < 8U; i++) {
		int n = snprintf(line, sizeof(line), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(line, (size_t)n, 10U), 0, "fill %u", i);
	}
	ret = hil_source_output_submit(
		line, (size_t)snprintf(line, sizeof(line), "HIL1 {\"full\":1}"), 1U);
	zassert_not_equal(ret, 0, "overflow must fail");
	/* Draining restores capacity. */
	hil_source_output_test_drain();
	ret = hil_source_output_submit(
		line, (size_t)snprintf(line, sizeof(line), "HIL1 {\"ok\":1}"), 10U);
	zassert_equal(ret, 0, "drain restores");
}

ZTEST(hil_source_app, test_formatter_truncation_fails_closed)
{
	char buf[64];
	int n;

	/* A tiny buffer must fail with no partial output. */
	n = hil_source_app_format_status_line(buf, sizeof(buf), CMD_STAT, RUN_ID);
	zassert_not_equal(n, 0, "truncation must fail");
	zassert_equal(buf[0], '\0', "no partial output");
}

/* ── 14. idle idempotence, exact-peer unpair, terminal reset ─────── */

ZTEST(hil_source_app, test_idle_idempotent_and_terminal_reset)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Idle with nothing configured: success. */
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle empty");

	/* Idempotent: another idle also succeeds. */
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle again");

	/* Full run to terminal pass (the preceding idles consumed the
	 * scripted connection state). */
	fake_set_conn_present(true);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	/* Configure is rejected while the immutable terminal snapshot is
	 * present. */
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	zassert_true(cap_find("\"error\":\"busy\""), "configure after terminal busy");

	/* Idle resets the terminal snapshot and configured IDs. */
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle after run");

	/* Configure works again after idle. */
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	zassert_true(cap_find("\"command\":\"configure\",\"ok\":true"), "reconfigure ok");
	dispatch_idle();
}

ZTEST(hil_source_app, test_unpair_exact_peer)
{
	char json[320];

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_bond_count(2);

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"unpair\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\",\"peer_address\":\"%s\",\"peer_address_type\":\"random\"}",
		 CMD_UNP, RUN_ID, PEER_ADDR);
	dispatch_json(json);
	zassert_true(cap_find("\"command\":\"unpair\",\"ok\":true"), "unpair ok");
	zassert_true(cap_find("\"bond_count\":2"), "resulting bond count");

	/* Exact on-air peer: display AA:BB:CC:DD:EE:FF -> little endian. */
	zassert_equal(fake_kick_count(FAKE_OP_UNPAIR), 1U, "unpair once");
	{
		const uint8_t want[6] = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA};
		const struct fake_record *ledger = fake_ledger();
		uint32_t n = fake_ledger_count();
		uint32_t i;
		bool found = false;

		for (i = 0U; i < n; i++) {
			if (ledger[i].op == FAKE_OP_UNPAIR) {
				zassert_mem_equal(ledger[i].addr, want, 6, "on-air address");
				zassert_equal(ledger[i].addr_type, 1U, "random type");
				found = true;
				break;
			}
		}
		zassert_true(found, "unpair ledger entry");
	}

	/* No-key is success: -ENOENT from the backend still reports ok. */
	fake_set_kick_result(FAKE_OP_UNPAIR, -ENOENT);
	dispatch_json(json);
	zassert_true(cap_find("\"command\":\"unpair\",\"ok\":true"), "no-key success");
}

/* ── 15. Mode A lockstep and exact stage caps ────────────────────── */

ZTEST(hil_source_app, test_modea_lockstep_and_caps)
{
	const struct fake_record *ledger;
	uint32_t n;
	uint32_t i;
	uint32_t sends_seen = 0U;
	uint32_t total = profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
			 profile_tail(HIL_SOURCE_PROFILE_48_4_1);

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	/* One plain stream-0 anchor precedes timestamped semantic pairs. */
	ledger = fake_ledger();
	n = fake_ledger_count();
	for (i = 0U; i < n; i++) {
		if (ledger[i].op == FAKE_OP_TX_SEND) {
			zassert_equal(ledger[i].stream_idx, 0U, "anchor stream");
			continue;
		}
		if (ledger[i].op != FAKE_OP_TX_SEND_TS) {
			continue;
		}
		zassert_equal(ledger[i].stream_idx, sends_seen % 2U, "lockstep stream");
		sends_seen++;
	}
	zassert_equal(sends_seen, 2U * total, "exact timestamped pair sends");
	zassert_equal(fake_send_count(0), total + 1U, "stream 0 cap plus anchor");
	zassert_equal(fake_send_count(1), total, "stream 1 cap");

	/* SDU lengths match the mono preset per stream. */
	for (i = 0U; i < n; i++) {
		if (ledger[i].op == FAKE_OP_TX_SEND) {
			zassert_equal(ledger[i].len, 120U, "mode A sdu len");
			break;
		}
	}
	dispatch_idle();
}

static bool modea_outstanding_target_reached(void)
{
	return fake_send_count(0) - fake_sent_count(0) >= HIL_SOURCE_TX_OUTSTANDING_TARGET &&
	       fake_send_count(1) - fake_sent_count(1) >= HIL_SOURCE_TX_OUTSTANDING_TARGET;
}

/* True once stream 0 has submitted its first (plain) SDU. */
static bool send_count_one_reached(void)
{
	return fake_send_count(0) >= 1U;
}

static bool mono_outstanding_target_reached(void)
{
	return fake_send_count(0) - fake_sent_count(0) >= HIL_SOURCE_TX_OUTSTANDING_TARGET;
}

ZTEST(hil_source_app, test_modea_three_outstanding_lockstep_backpressure)
{
	static const uint8_t expected_streams[] = {0U, 1U, 0U, 1U, 0U, 1U};
	const struct fake_record *ledger;
	uint32_t sends_seen = 0U;
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Hold callbacks after stream 0's one CIG-anchor completion. */
	pump_sents_enabled = false;
	dispatch_start();
	zassert_equal(HIL_SOURCE_TX_OUTSTANDING_TARGET, 3U, "Mode A target");
	zassert_true(pump_until(send_count_one_reached, 10000U), "anchor send");
	fake_signal_sent(0);
	zassert_true(pump_until(modea_outstanding_target_reached, 10000U),
		     "three outstanding sends per stream");

	/* The coordinator must block at the pair target, with no further send
	 * on either stream before another completion callback arrives: after
	 * settling, each stream sits at exactly three outstanding sends and
	 * exactly the one base completion. */
	for (i = 0U; i < 8U; i++) {
		pump_once();
	}
	zassert_equal(fake_send_count(0), fake_sent_count(0) + 3U,
		      "stream 0 exactly three outstanding");
	zassert_equal(fake_send_count(1), fake_sent_count(1) + 3U,
		      "stream 1 exactly three outstanding");

	/* Timestamped semantic sends strictly alternate streams. */
	ledger = fake_ledger();
	for (i = 0U; i < fake_ledger_count(); i++) {
		if (ledger[i].op != FAKE_OP_TX_SEND_TS) {
			continue;
		}
		zassert_equal(ledger[i].stream_idx, (uint8_t)(sends_seen % 2U),
			      "Mode A send lockstep");
		sends_seen++;
	}
	zassert_equal(sends_seen, ARRAY_SIZE(expected_streams), "exact target ledger");

	/* Completions release the pair backpressure and the normal run must pass. */
	pump_sents_enabled = true;
	zassert_true(pump_until_terminal(), "terminal after callbacks");
	zassert_true(cap_terminal("pass"), "pass after callbacks");
	dispatch_idle();
}

/* ── 16. stale callbacks counted not applied; zero-outstanding error ─ */

ZTEST(hil_source_app, test_stale_callback_counted)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Stream until TX is flowing, then stop pumping sents: the TX
	 * progress timeout fires (2 s with no send/sent activity) and the
	 * run aborts with cause timeout.  There is no host stop flag, so
	 * the universal cleanup waits are not short-circuited: stalling the
	 * cleanup at the disable wait leaves the run in teardown with the
	 * logical outstanding already cleared, and late sent callbacks are
	 * stale but still counted (never applied). */
	while (fake_send_count(0) < 4U) {
		pump_once();
	}
	pump_sents_enabled = false;
	pump_skip = (1ULL << FAKE_OP_DISABLE) | (1ULL << FAKE_OP_RELEASE) |
		    (1ULL << FAKE_OP_DISCONNECT);
	while (!cap_find("\"state\":\"teardown\",\"cause\":\"timeout\"")) {
		pump_once();
	}
	fake_signal_sent(0);
	fake_signal_sent(0);
	fake_signal_sent(0);

	pump_skip = 0U;
	pump_sents_enabled = true;
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"cause\":\"timeout\""), "timeout cause, not error");

	/* The stale callbacks delivered during teardown were counted (not
	 * applied): the terminal snapshot reports at least the three stale
	 * callbacks among the sent-callback total. */
	dispatch_status();
	{
		int sc = cap_extract_int("\"cb\":");

		zassert_true(sc >= 3, "stale callbacks counted");
	}
	dispatch_idle();
}

ZTEST(hil_source_app, test_zero_outstanding_callback_error)
{
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Suppress sent callbacks until TX reaches its outstanding target, then
	 * deliver one callback more than that depth: the surplus hits zero
	 * outstanding while streaming and aborts the run with cause error. First
	 * deliver the one bootstrap completion so anchor readback can finish. */
	pump_sents_enabled = false;
	zassert_true(pump_until(send_count_one_reached, 10000U), "anchor send");
	fake_signal_sent(0);
	zassert_true(pump_until(mono_outstanding_target_reached, 10000U), "outstanding target");
	for (i = 0U; i <= HIL_SOURCE_TX_OUTSTANDING_TARGET; i++) {
		fake_signal_sent(0);
	}
	pump_sents_enabled = true;

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""),
		     "zero-outstanding error abort");
	dispatch_idle();
}

ZTEST(hil_source_app, test_sent_completion_wakes_tx_backpressure)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	/* Keep completions disabled in the normal pump.  The fake asynchronously
	 * notifies a lower-priority completion thread exactly when the source
	 * reaches its outstanding target, so the callback occurs only after
	 * backpressure is established. */
	pump_sents_enabled = false;
	start_tx_completion();
	fake_set_send_signal(0, HIL_SOURCE_TX_OUTSTANDING_TARGET + 2U, &sem_tx_next_send);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Deliver bootstrap completion, then completion thread owns callback at
	 * exact regular outstanding target. */
	zassert_true(pump_until(send_count_one_reached, 10000U), "anchor send");
	fake_signal_sent(0);

	zassert_true(pump_until(tx_completion_done, 10000U), "completion choreography");
	zassert_true(fake_send_count(0) >= HIL_SOURCE_TX_OUTSTANDING_TARGET,
		     "completion at outstanding target");
	/* Under SDC timestamp-mode provisioning, the next submission
	 * is additionally paced by its ISO-event pin gate: at most one pin
	 * interval plus the lead window can separate the completion from the
	 * next send even when the outstanding slot freed instantly.  The
	 * bound below covers one 10 ms interval + 4 ms lead + slack; the old
	 * 5 ms completion-only bound applied to the pre-pin polling regime. */
	zassert_equal(k_sem_take(&sem_tx_next_send, K_MSEC(20)), 0,
		      "sent completion did not promptly permit next TX submission");

	pump_sents_enabled = true;
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");
	dispatch_idle();
}

ZTEST(hil_source_app, test_stop_breaks_tx_backpressure_wait)
{
	uint32_t t0;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	pump_sents_enabled = false;
	zassert_true(pump_until(mono_outstanding_target_reached, 10000U), "outstanding target");

	t0 = k_uptime_get_32();
	dispatch_stop();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(k_uptime_get_32() - t0 < 1000U, "stop waited for TX deadline");
	assert_abort_stop_and_cleanup();
	pump_sents_enabled = true;
	dispatch_idle();
}

ZTEST(hil_source_app, test_idle_breaks_tx_backpressure_wait)
{
	uint32_t t0;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	pump_sents_enabled = false;
	zassert_true(pump_until(mono_outstanding_target_reached, 10000U), "outstanding target");

	/* Run active idle in a peer thread while this test pumps lifecycle
	 * completions.  This proves idle's own stop request wakes TX instead of
	 * waiting for the two-second progress deadline. */
	t0 = k_uptime_get_32();
	start_idle_dispatch();
	zassert_true(pump_until(idle_dispatch_done, 1000U), "idle did not complete");
	zassert_equal(idle_dispatch_result, 0, "idle result");
	zassert_true(k_uptime_get_32() - t0 < 1000U, "idle waited for TX deadline");
	drain();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle success");
	pump_sents_enabled = true;
}

/* ── 17. explicit TX activation ──────────────────────────────────── */

ZTEST(hil_source_app, test_tx_start_activation_once_before_send)
{
	uint32_t total = profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
			 profile_tail(HIL_SOURCE_PROFILE_48_4_1);
	const struct fake_record *ledger;
	uint32_t n;
	uint32_t i;
	bool saw_tx_start = false;
	bool saw_send_before_start = false;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	zassert_equal(fake_kick_count(FAKE_OP_TX_START), 1U, "tx_start once per segment");
	ledger = fake_ledger();
	n = fake_ledger_count();
	for (i = 0U; i < n; i++) {
		if (ledger[i].op == FAKE_OP_TX_START) {
			saw_tx_start = true;
			zassert_equal(ledger[i].count, 1U, "mono stream count");
			continue;
		}
		if (ledger[i].op == FAKE_OP_TX_SEND && !saw_tx_start) {
			saw_send_before_start = true;
		}
	}
	zassert_true(saw_tx_start, "tx_start recorded");
	zassert_false(saw_send_before_start, "no send before activation");
	zassert_equal(fake_send_count(0), total + 1U, "full mono run plus anchor sent");
	dispatch_idle();
}

ZTEST(hil_source_app, test_tx_start_failure_aborts)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_kick_result(FAKE_OP_TX_START, -EIO);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""), "error abort");
	zassert_equal(fake_send_count(0), 0U, "no sends after failed activation");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "first errno preserved");
	dispatch_idle();
}

/* ── 18. Mode A asymmetric stall: per-stream progress ────────────── */

ZTEST(hil_source_app, test_modea_asymmetric_stall_timeout)
{
	uint32_t total = profile_preamble(HIL_SOURCE_PROFILE_48_4_1) + 10U +
			 profile_tail(HIL_SOURCE_PROFILE_48_4_1);

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Let both streams flow until each has outstanding frames, then stop
	 * stream 0 sent callbacks entirely while stream 1 keeps completing.
	 * The pair cannot send; stream 1 activity must not mask stream 0's
	 * stall, so the run times out instead of looping forever. */
	while (fake_send_count(0) < 4U) {
		pump_once();
	}
	fake_set_suppress_sent(0, true);

	zassert_true(pump_until_terminal(), "bounded terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"timeout\""),
		     "per-stream timeout, other CIS progress must not mask it");
	zassert_equal(fake_send_count(0), fake_send_count(1) + 1U,
		      "one anchor plus lockstep pairs preserved");
	zassert_true(fake_send_count(0) < total + 1U, "no unbounded sends");
	zassert_true(fake_sent_count(0) < fake_send_count(0), "stream 0 callbacks suppressed");
	dispatch_status();
	zassert_true(cap_find("\"cause\":\"timeout\""), "timeout cause in snapshot");
	dispatch_idle();
}

/* ── 19. stop cleanup waits for completions ──────────────────────── */

ZTEST(hil_source_app, test_stop_cleanup_waits_completion)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	dispatch_stop();

	/* Hold the cleanup at the disable wait: with no disable token
	 * delivered, the worker must NOT short-circuit on the stop flag. */
	pump_skip = (1ULL << FAKE_OP_DISABLE) | (1ULL << FAKE_OP_RELEASE) |
		    (1ULL << FAKE_OP_DISCONNECT);
	while (fake_kick_count(FAKE_OP_DISABLE) == 0U) {
		pump_once();
	}
	zassert_false(cap_terminal("pass") || cap_terminal("fail"),
		      "no terminal while disable pending");
	zassert_equal(fake_kick_count(FAKE_OP_RELEASE), 0U, "no release before disable completes");
	zassert_equal(fake_kick_count(FAKE_OP_GROUP_DELETE), 0U,
		      "no group delete before disable completes");

	/* Deliver the disable completion; cleanup proceeds and finishes. */
	fake_signal(FAKE_OP_DISABLE, 1);
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "terminal after completions");
	zassert_true(cap_terminal("fail"), "fail");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

/* ── 20. exactly one completion token per stream ─────────────────── */

ZTEST(hil_source_app, test_per_stream_completion_token_contract)
{
	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Configure: deliver exactly one of two completion tokens.  The
	 * worker must not proceed to qos until the second stream's token
	 * arrives. */
	pump_skip = 1ULL << FAKE_OP_CONFIGURE;
	while (fake_kick_count(FAKE_OP_CONFIGURE) == 0U) {
		pump_once();
	}
	fake_signal(FAKE_OP_CONFIGURE, 1);
	k_sleep(K_MSEC(50));
	pump_once();
	zassert_equal(fake_kick_count(FAKE_OP_QOS), 0U, "no qos after first configure token");
	fake_signal(FAKE_OP_CONFIGURE, 1);
	pump_skip = 0U;

	/* Run to streaming, then stop: cleanup disables each attached
	 * stream and needs one completion token per stream. */
	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	dispatch_stop();
	pump_skip = (1ULL << FAKE_OP_DISABLE) | (1ULL << FAKE_OP_RELEASE);
	while (fake_kick_count(FAKE_OP_DISABLE) == 0U) {
		pump_once();
	}
	fake_signal(FAKE_OP_DISABLE, 1);
	k_sleep(K_MSEC(50));
	pump_once();
	zassert_equal(fake_kick_count(FAKE_OP_RELEASE), 0U, "no release after first disable token");
	fake_signal(FAKE_OP_DISABLE, 1);
	pump_skip = 0U;

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	assert_abort_stop_and_cleanup();
	dispatch_idle();
}

/* ── 21. idle completion race and timeout ownership ──────────────── */

ZTEST(hil_source_app, test_idle_completion_not_lost)
{
	uint32_t t0;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Let the worker reach the connect wait, then stop.  Later lifecycle
	 * kicks are blocked so the worker cannot advance past the connect
	 * wait on pump timing alone; it completes on its own (no streams
	 * attached, so its cleanup needs no test signals).  The idle that
	 * follows must catch the completion token and return promptly, never
	 * reset it away and stall. */
	pump_skip = 1ULL << FAKE_OP_CONNECT;
	while (fake_kick_count(FAKE_OP_CONNECT) == 0U) {
		pump_once();
	}
	dispatch_stop();
	{
		char json[256];

		snprintf(json, sizeof(json),
			 "{\"protocol_version\":1,\"command\":\"idle\",\"command_id\":\"%s\","
			 "\"run_id\":\"%s\"}",
			 CMD_IDLE, RUN_ID);
		t0 = k_uptime_get_32();
		zassert_equal(hil_source_app_dispatch(json, strlen(json)), 0, "idle ok");
		drain();
	}
	pump_skip = 0U;
	zassert_true(k_uptime_get_32() - t0 < 5000U, "idle did not stall");
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle success record");
	zassert_true(cap_find("\"verdict\":\"fail\""), "run ended in fail after stop");
}

ZTEST(hil_source_app, test_idle_timeout_ownership)
{
	char json[256];
	int ret;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_auto_detach(false);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Run to streaming, then stop: the worker aborts and its cleanup
	 * blocks on the disable wait (no token) and then the release wait
	 * (auto_detach off, no token), so it cannot reach terminal within
	 * the idle budget. */
	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	dispatch_stop();
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"idle\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_IDLE, RUN_ID);
	ret = hil_source_app_dispatch(json, strlen(json));
	drain();
	zassert_equal(ret, -ETIMEDOUT, "idle times out");
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":false"), "failed idle record");

	/* The timed-out idle must not have reset the run or started a second
	 * cleanup: the run is still active and the worker's cleanup has run
	 * exactly once. */
	dispatch_status();
	zassert_true(cap_find("\"active\":true"), "run still active after idle timeout");
	zassert_equal(fake_kick_count(FAKE_OP_DISABLE), 1U, "no second cleanup from idle");

	/* Let the worker's blocked waits expire and finish the run. */
	zassert_true(pump_until_terminal(), "worker terminal after waits expire");
	zassert_true(cap_terminal("fail"), "fail");

	/* Clean the fixture up for the next test. */
	fake_set_attached(0, false);
	fake_set_group_present(false);
	fake_set_conn_present(false);
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle after worker done");
}

/* ── 22. maximum IDs/state produce bounded two-stream statuses ───── */

ZTEST(hil_source_app, test_max_ids_two_stream_status)
{
	char cmd_id[64];
	char run_id[65];
	char wrong_run_id[65];
	char json[512];

	memset(cmd_id, 'C', sizeof(cmd_id) - 1U);
	cmd_id[sizeof(cmd_id) - 1U] = '\0';
	memset(run_id, 'R', sizeof(run_id) - 1U);
	run_id[sizeof(run_id) - 1U] = '\0';
	memset(wrong_run_id, 'W', sizeof(wrong_run_id) - 1U);
	wrong_run_id[sizeof(wrong_run_id) - 1U] = '\0';

	scenario_setup(HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, HIL_SOURCE_MAX_SCORED_SDUS,
		       "once");
	/* Keep every controller-clock diagnostic at its widest realistic shape:
	 * ten-digit timestamps and a two-digit stale-event skip count. */
	fake_ts_set_readback_base(4000000000U);
	fake_ts_set_initial_time_offset(100000);

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"configure\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\",\"peer_address\":\"%s\",\"peer_address_type\":\"random\","
		 "\"mode\":\"mode_a\",\"profile\":\"48_4_1\",\"scored_sdu_count\":%u,"
		 "\"signal_seed\":4294967295,\"reconnect_policy\":\"once\"}",
		 cmd_id, run_id, PEER_ADDR, (unsigned int)HIL_SOURCE_MAX_SCORED_SDUS);
	dispatch_json(json);
	zassert_true(cap_find("\"command\":\"configure\",\"ok\":true"), "max-id configure");

	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"start\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 cmd_id, run_id);
	dispatch_json(json);
	zassert_true(cap_find("\"kind\":\"ack\""), "max-id start ack");
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("pass"), "pass");

	/* Maximum legal command ID (63) and run ID (64) in a two-stream
	 * status must format and queue without truncation under the 1024
	 * byte line bound. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"status\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 cmd_id, run_id);
	dispatch_json(json);
	{
		char needle[80];
		bool found_full = false;
		uint32_t c;

		snprintf(needle, sizeof(needle), "\"command_id\":\"%s\"", cmd_id);
		for (c = 0U; c < cap_count; c++) {
			if (strstr(captured[c], needle) == NULL) {
				continue;
			}
			zassert_true(strlen(captured[c]) <= HIL_SOURCE_OUTPUT_LINE_SIZE,
				     "line within capacity");
			zassert_not_null(strstr(captured[c], "\"stream_count\":2"),
					 "two-stream status intact");
			zassert_not_null(strstr(captured[c], "\"bond_count\":"),
					 "status tail intact");
			zassert_not_null(strstr(captured[c], run_id), "full run id present");
			found_full = true;
			break;
		}
		zassert_true(found_full, "complete max-id status line captured");
	}

	/* Rejected commands use a compact status snapshot. With maximum legal
	 * IDs, rendering the full successful TX diagnostics plus the longer
	 * false/invalid_request fields would exceed the fixed 1024-byte line. */
	cap_reset();
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"status\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 cmd_id, wrong_run_id);
	zassert_equal(hil_source_app_dispatch(json, strlen(json)), -EINVAL,
		      "mismatched maximum run id rejected");
	drain();
	{
		char command_needle[80];
		char run_needle[82];
		bool found_rejection = false;
		uint32_t c;

		snprintf(command_needle, sizeof(command_needle), "\"command_id\":\"%s\"", cmd_id);
		snprintf(run_needle, sizeof(run_needle), "\"run_id\":\"%s\"", wrong_run_id);
		for (c = 0U; c < cap_count; c++) {
			if (strstr(captured[c], command_needle) == NULL ||
			    strstr(captured[c], run_needle) == NULL) {
				continue;
			}
			zassert_true(strlen(captured[c]) < HIL_SOURCE_OUTPUT_LINE_SIZE,
				     "rejection within fixed line capacity");
			zassert_not_null(strstr(captured[c], "\"ok\":false"),
					 "rejection result intact");
			zassert_not_null(strstr(captured[c], "\"error\":\"invalid_request\""),
					 "rejection error intact");
			zassert_not_null(strstr(captured[c], "\"tx\":null"),
					 "rejection omits optional TX diagnostics");
			zassert_not_null(strstr(captured[c], "\"bond_count\":"),
					 "rejection tail intact");
			found_rejection = true;
			break;
		}
		zassert_true(found_rejection, "complete max-id rejection captured");
	}
	dispatch_idle();
}

/* ── 23. worker record submission failure ────────────────────────── */

ZTEST(hil_source_app, test_record_submission_failure)
{
	char fill[64];
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_op_error(-EIO);
	fake_set_conn_present(true);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Let the worker reach the connect wait without completing it (records
	 * drained so far). */
	pump_skip = 1ULL << FAKE_OP_CONNECT;
	while (fake_kick_count(FAKE_OP_CONNECT) == 0U) {
		pump_once();
	}
	drain();
	/* Fill the output queue: the worker's next record cannot be submitted
	 * and its failure must abort with -EIO preserved as the first error. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	/* Complete the connect: the worker sees the scripted async error,
	 * aborts, and its abort record submit fails on the full queue. */
	fake_signal(FAKE_OP_CONNECT, 1);
	k_sleep(K_MSEC(100));
	/* Free the queue; the worker's records emit once output can resume. */
	drain();
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""), "error abort");
	zassert_equal(fake_kick_count(FAKE_OP_DISCONNECT), 1U, "cleanup attempted");
	zassert_equal(fake_kick_count(FAKE_OP_CONN_UNREF), 0U,
		      "no owned ref: connect never completed");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "first errno -EIO preserved");
	dispatch_idle();
}

/* ── 24. exact-peer security ─────────────────────────────────────── */

ZTEST(hil_source_app, test_security_missing_bond)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_peer_bonded(false);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""),
		     "missing bond aborts before discovery");
	zassert_true(fake_kick_count(FAKE_OP_PEER_BONDED) >= 1U, "exact bond consulted");
	zassert_equal(fake_kick_count(FAKE_OP_DISCOVER), 0U, "no discovery without bond");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-13"), "first errno -EACCES");
	dispatch_idle();
}

ZTEST(hil_source_app, test_security_auto_complete_no_callback)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	/* Already-secure bonded reconnect shape: kick_security completes the
	 * wait itself, no security_changed callback fires, and the status
	 * gate starts at zero.  The fake must update the security level to
	 * L2 exactly as production does; giving only the semaphore without
	 * the level update must fail the run. */
	fake_set_security_level(0U);
	fake_set_security_auto_complete(true);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	pump_skip = 1ULL << FAKE_OP_SECURITY;
	zassert_true(pump_until_terminal(), "terminal");
	pump_skip = 0U;
	zassert_true(cap_terminal("pass"), "pass without security callback");
	zassert_equal(fake_kick_count(FAKE_OP_SECURITY), 1U, "security kicked once");
	dispatch_idle();
}

/* ── 25. cleanup ASCS error ownership ────────────────────────────── */

ZTEST(hil_source_app, test_cleanup_ascs_rejection_first_error)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	dispatch_stop();
	/* Hold the cleanup at the disable wait, then deliver a disable
	 * completion that carries an ASCS rejection error. */
	pump_skip = (1ULL << FAKE_OP_DISABLE) | (1ULL << FAKE_OP_RELEASE) |
		    (1ULL << FAKE_OP_DISCONNECT);
	while (fake_kick_count(FAKE_OP_DISABLE) == 0U) {
		pump_once();
	}
	fake_set_op_error(-EBADMSG);
	fake_signal(FAKE_OP_DISABLE, 1);
	pump_skip = 0U;

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"stop\""), "stop abort");

	/* The ASCS rejection became the first cleanup error while later
	 * cleanup operations were still attempted in order. */
	dispatch_status();
	{
		char needle[32];

		snprintf(needle, sizeof(needle), "\"first_errno\":%d", -EBADMSG);
		zassert_true(cap_find(needle), "first errno -EBADMSG");
	}
	zassert_true(fake_kick_count(FAKE_OP_RELEASE) >= 1U, "release still attempted");
	zassert_true(fake_kick_count(FAKE_OP_DISCONNECT) >= 1U, "disconnect still attempted");
	zassert_true(fake_kick_count(FAKE_OP_GROUP_DELETE) >= 1U, "group delete still attempted");
	zassert_true(fake_kick_count(FAKE_OP_CONN_UNREF) >= 1U, "conn unref still attempted");
	zassert_true(fake_kick_count(FAKE_OP_RESET_SEGMENT) >= 1U, "reset still attempted");
	dispatch_idle();
}

/* ── 26. retryable group delete ──────────────────────────────────── */

ZTEST(hil_source_app, test_group_delete_retry)
{
	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_kick_result(FAKE_OP_GROUP_DELETE, -EIO);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	zassert_true(pump_until_terminal(), "terminal");
	zassert_true(cap_terminal("fail"), "fail");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "group delete failure first errno");
	/* A failed delete must not falsely report the group absent. */
	zassert_not_null(strstr(captured[cap_count - 1U], "\"group\":true"),
			 "group still reported present before retry");

	/* Recover and retry through idle; the retried delete succeeds. */
	fake_set_kick_result(FAKE_OP_GROUP_DELETE, 0);
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle retry ok");
	dispatch_status();
	zassert_not_null(strstr(captured[cap_count - 1U], "\"group\":false"),
			 "group absent after successful retry");
}

/* ── 27. command response propagation ────────────────────────────── */

ZTEST(hil_source_app, test_command_response_propagation)
{
	char json[256];
	char fill[64];
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Semantic rejection with a successful emission returns the
	 * semantic command error, never zero. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"status\",\"command_id\":\"%s\","
		 "\"run_id\":\"run-other\"}",
		 CMD_STAT);
	zassert_equal(hil_source_app_dispatch(json, strlen(json)), -EINVAL,
		      "semantic rejection return");
	drain();

	/* A queue-full accepted command response returns -EIO, never
	 * success. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_HELLO, RUN_ID);
	zassert_equal(hil_source_app_dispatch(json, strlen(json)), -EIO,
		      "queue-full response return");
	drain();
	dispatch_idle();
}

/* ── 28. terminal publication failure ────────────────────────────── */

ZTEST(hil_source_app, test_terminal_pass_submission_failure)
{
	char fill[64];
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Run to teardown and hold the worker at the cleanup disable wait. */
	pump_skip = 1ULL << FAKE_OP_DISABLE;
	while (!cap_find("\"state\":\"teardown\"")) {
		pump_once();
	}
	while (fake_kick_count(FAKE_OP_DISABLE) == 0U) {
		pump_once();
	}
	drain();
	/* Fill the output queue: the PASS terminal record cannot be
	 * submitted, so the run must publish fail, never pass. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	/* Release the disable wait; both terminal emits block 1 s each on
	 * the full queue.  No draining during the wait. */
	fake_signal(FAKE_OP_DISABLE, 1);
	pump_skip = 0U;
	k_sleep(K_MSEC(2500));
	drain();

	zassert_false(cap_terminal("pass"), "no PASS terminal captured");
	dispatch_status();
	zassert_true(cap_find("\"verdict\":\"fail\""), "status reports fail");
	zassert_true(cap_find("\"first_errno\":-5"), "first errno -EIO");
	dispatch_idle();
}

/* ── 29. worker record submission failure is first errno ─────────── */

ZTEST(hil_source_app, test_worker_record_failure_first_errno)
{
	char fill[64];
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* The worker reaches the connect wait without completing it. */
	pump_skip = 1ULL << FAKE_OP_CONNECT;
	while (fake_kick_count(FAKE_OP_CONNECT) == 0U) {
		pump_once();
	}
	drain();
	/* Fill the output queue with no scripted backend error: the worker's
	 * next state record (secured) submit fails and output failure itself
	 * must become the first errno. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	/* Complete connect + security: the secured record submit blocks 1 s,
	 * the abort record submit blocks 1 s, then cleanup reaches its
	 * disable wait. */
	fake_signal(FAKE_OP_CONNECT, 1);
	fake_signal(FAKE_OP_SECURITY, 1);
	k_sleep(K_MSEC(2300));
	/* Free the queue and let cleanup finish; the terminal fail record
	 * emits once output can resume. */
	drain();
	fake_signal(FAKE_OP_DISABLE, 1);
	k_sleep(K_MSEC(200));
	drain();

	zassert_true(cap_terminal("fail"), "terminal fail once output resumed");
	/* The abort happened at the secured state-record emit, before any
	 * stream was configured, so no streams are attached: cleanup still
	 * runs its acquired-resource steps (disconnect, group, unref,
	 * reset). */
	zassert_equal(fake_kick_count(FAKE_OP_DISABLE), 0U, "no attached streams to disable");
	zassert_true(fake_kick_count(FAKE_OP_DISCONNECT) >= 1U, "cleanup disconnect attempted");
	zassert_true(fake_kick_count(FAKE_OP_CONN_UNREF) >= 1U, "cleanup conn unref attempted");
	zassert_true(fake_kick_count(FAKE_OP_RESET_SEGMENT) >= 1U, "cleanup reset attempted");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "output failure is first errno");
	zassert_true(cap_find("\"verdict\":\"fail\""), "fail");
	dispatch_idle();
}

/* ── 30. start ACK failure: no deadlock, no silent worker ────────── */

ZTEST(hil_source_app, test_start_ack_failure_no_deadlock)
{
	char json[256];
	char fill[64];
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");

	/* Fill the output queue: the start ACK cannot be submitted.  The
	 * dispatch must return -EIO within the bounded submit timeout, not
	 * deadlock on a nested app-mutex acquisition. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"start\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_START, RUN_ID);
	zassert_equal(hil_source_app_dispatch(json, strlen(json)), -EIO, "start ack failure");
	drain();

	/* No worker was woken: no connect kick, no TX. */
	zassert_equal(fake_kick_count(FAKE_OP_CONNECT), 0U, "no worker connect");
	zassert_equal(fake_kick_count(FAKE_OP_TX_START), 0U, "no TX activation");
	zassert_equal(fake_send_count(0), 0U, "no TX sends");

	/* The run is an immutable terminal FAIL with first errno -EIO. */
	dispatch_status();
	zassert_true(cap_find("\"aborted\":true"), "aborted flag");
	zassert_true(cap_find("\"cause\":\"error\""), "error cause");
	zassert_true(cap_find("\"verdict\":\"fail\""), "verdict fail");
	zassert_true(cap_find("\"first_errno\":-5"), "first errno -EIO");

	/* Idle resets successfully. */
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle reset");
}

/* ── 31. idle timeout response emission failure ──────────────────── */

ZTEST(hil_source_app, test_idle_timeout_response_emission_failure)
{
	char json[256];
	char fill[64];
	uint32_t i;
	int ret;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	fake_set_auto_detach(false);
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	while (!cap_find("\"state\":\"streaming\"")) {
		pump_once();
	}
	dispatch_stop();
	/* Fill the output queue: the failed idle response emission must be
	 * returned as the dispatch error, never silently dropped. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"idle\",\"command_id\":\"%s\","
		 "\"run_id\":\"%s\"}",
		 CMD_IDLE, RUN_ID);
	ret = hil_source_app_dispatch(json, strlen(json));
	zassert_equal(ret, -EIO, "idle response emission failure returned");
	drain();
	zassert_false(cap_find("\"command\":\"idle\""), "no idle record emitted");

	/* The run is still active and the worker owns its cleanup. Let blocked
	 * disable/release waits expire. The stop path may first expire TX progress
	 * and drain waits before cleanup, so allow the full bounded path. */
	k_sleep(K_MSEC(15000));
	drain();
	dispatch_status();
	zassert_true(cap_find("\"verdict\":\"fail\""), "worker terminalized fail");
	zassert_true(cap_find("\"active\":false"), "run inactive after worker done");

	fake_set_attached(0, false);
	fake_set_group_present(false);
	fake_set_conn_present(false);
	dispatch_idle();
	zassert_true(cap_find("\"command\":\"idle\",\"ok\":true"), "idle reset ok");
}

/* ── 32. parse-error during active run ───────────────────────────── */

ZTEST(hil_source_app, test_parse_error_active_run)
{
	char fill[64];
	uint32_t i;

	scenario_setup(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_configure(HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 10U, "none");
	dispatch_start();

	/* Let the worker reach the connect wait so the run is active. */
	pump_skip = 1ULL << FAKE_OP_CONNECT;
	while (fake_kick_count(FAKE_OP_CONNECT) == 0U) {
		pump_once();
	}

	/* Malformed command with output available: reserved diagnostic IDs,
	 * no lifecycle mutation. */
	zassert_equal(hil_source_app_dispatch("{not-json", 9), -EINVAL, "parse error return");
	drain();
	zassert_true(cap_find("\"command_id\":\"parse-error\""), "reserved command id");
	zassert_true(cap_find("\"run_id\":\"unbound\""), "reserved run id");
	dispatch_status();
	zassert_true(cap_find("\"active\":true"), "lifecycle not mutated");

	/* Malformed command with output full: -EIO, runtime error aborts the
	 * worker, terminal fail once output resumes. */
	for (i = 0U; i < HIL_SOURCE_OUTPUT_QUEUE_DEPTH; i++) {
		int n = snprintf(fill, sizeof(fill), "HIL1 {\"fill\":%u}", (unsigned int)i);

		zassert_equal(hil_source_output_submit(fill, (size_t)n, 0U), 0, "fill %u", i);
	}
	zassert_equal(hil_source_app_dispatch("{not-json", 9), -EIO, "parse error emit failure");
	pump_skip = 0U;
	zassert_true(pump_until_terminal(), "worker error-abort after runtime error");
	zassert_true(cap_terminal("fail"), "terminal fail");
	zassert_true(cap_find("\"state\":\"teardown\",\"cause\":\"error\""), "error abort");
	dispatch_status();
	zassert_true(cap_find("\"first_errno\":-5"), "first errno -EIO");
	dispatch_idle();
}
