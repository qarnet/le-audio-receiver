/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * RH1A native public-boundary tests for the strict inbound command schema,
 * the HIL1 record envelope, and the pure run-state machine.
 *
 * Tests prove public behavior only: parse results, exact formatted record
 * bytes, state-machine transitions, and byte-level atomicity of rejected
 * operations.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "hil_source_protocol.h"
#include "hil_source_record.h"
#include "hil_source_state.h"
#include "hil_source_types.h"

ZTEST_SUITE(hil_source_control, NULL, NULL, NULL, NULL, NULL);

/* ── helpers ──────────────────────────────────────────────────────── */

static enum hil_source_parse_result parse_json(const char *json, struct hil_source_command_in *out)
{
	return hil_source_parse(json, strlen(json), out);
}

#define CMD_HELLO                                                                                  \
	"{\"protocol_version\":1,\"command\":\"hello\","                                           \
	"\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\""
#define CMD_UNPAIR                                                                                 \
	"{\"protocol_version\":1,\"command\":\"unpair\","                                          \
	"\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\""
#define CMD_CONFIGURE                                                                              \
	"{\"protocol_version\":1,\"command\":\"configure\","                                       \
	"\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\""
#define CMD_ADDR "\"peer_address\":\"AA:BB:CC:DD:EE:FF\",\"peer_address_type\":\"public\""
#define CMD_CONFIGURE_EXTRA                                                                        \
	"\"mode\":\"mono\",\"profile\":\"48_3_1\","                                                \
	"\"scored_sdu_count\":100,\"signal_seed\":123456789,"                                      \
	"\"reconnect_policy\":\"once\""

static void expect_ok(const char *json, enum hil_source_command cmd)
{
	struct hil_source_command_in out;

	zassert_equal(parse_json(json, &out), HIL_SOURCE_PARSE_OK, "expected OK for %s", json);
	zassert_equal(out.command, cmd, "command enum mismatch for %s", json);
	zassert_equal(strcmp(out.command_id, "cmd-0001"), 0, "command_id mismatch for %s", json);
	zassert_equal(strcmp(out.run_id, "run-0001"), 0, "run_id mismatch for %s", json);
}

static void expect_err(const char *json, enum hil_source_parse_result want)
{
	struct hil_source_command_in out;

	memset(&out, 0, sizeof(out));
	zassert_equal(parse_json(json, &out), want, "expected %s for %s",
		      hil_source_parse_result_name(want), json);
}

static void state_config(struct hil_source_state *st, enum hil_source_mode mode,
			 enum hil_source_profile profile, uint32_t target,
			 enum hil_source_reconnect_policy policy)
{
	struct hil_source_config cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = mode;
	cfg.profile = profile;
	cfg.peer_address[0] = 0xAA;
	cfg.peer_address[1] = 0xBB;
	cfg.peer_address[2] = 0xCC;
	cfg.peer_address[3] = 0xDD;
	cfg.peer_address[4] = 0xEE;
	cfg.peer_address[5] = 0xFF;
	cfg.peer_address_type = HIL_SOURCE_ADDR_PUBLIC;
	cfg.scored_sdu_count = target;
	cfg.signal_seed = HIL_SOURCE_DEFAULT_SIGNAL_SEED;
	cfg.reconnect_policy = policy;
	zassert_equal(hil_source_state_configure(st, &cfg), 0, "configure failed");
}

static void state_to_streaming(struct hil_source_state *st)
{
	zassert_equal(hil_source_state_start(st), 0, "start failed");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_CONFIGURED), 0,
		      "advance configured");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_CONNECTING), 0,
		      "advance connecting");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_SECURED), 0,
		      "advance secured");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_DISCOVERED), 0,
		      "advance discovered");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_QOS), 0, "advance qos");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_STREAMING), 0,
		      "advance streaming");
}

static void state_submit_scored(struct hil_source_state *st, uint32_t stream, uint32_t count)
{
	uint32_t i;

	for (i = 0U; i < count; i++) {
		zassert_equal(hil_source_state_counter_submit_scored(st, stream), 0,
			      "submit scored");
	}
}

static void state_finish_segment(struct hil_source_state *st)
{
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_SCORED_COMPLETE), 0,
		      "advance scored_complete");
	zassert_equal(hil_source_state_advance(st, HIL_SOURCE_RUN_STATE_TEARDOWN), 0,
		      "advance teardown");
}

/* ── 1. valid command shapes and canonical enum mapping ───────────── */

ZTEST(hil_source_control, test_all_seven_command_shapes)
{
	expect_ok(CMD_HELLO "}", HIL_SOURCE_CMD_HELLO);
	expect_ok("{\"protocol_version\":1,\"command\":\"idle\","
		  "\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\"}",
		  HIL_SOURCE_CMD_IDLE);
	expect_ok(CMD_UNPAIR ", " CMD_ADDR "}", HIL_SOURCE_CMD_UNPAIR);
	expect_ok(CMD_CONFIGURE ", " CMD_ADDR ", " CMD_CONFIGURE_EXTRA "}",
		  HIL_SOURCE_CMD_CONFIGURE);
	expect_ok("{\"protocol_version\":1,\"command\":\"start\","
		  "\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\"}",
		  HIL_SOURCE_CMD_START);
	expect_ok("{\"protocol_version\":1,\"command\":\"stop\","
		  "\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\"}",
		  HIL_SOURCE_CMD_STOP);
	expect_ok("{\"protocol_version\":1,\"command\":\"status\","
		  "\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\"}",
		  HIL_SOURCE_CMD_STATUS);
}

ZTEST(hil_source_control, test_configure_fields_map_to_enums)
{
	struct hil_source_command_in out;
	const char *json = CMD_CONFIGURE ", " CMD_ADDR ", " CMD_CONFIGURE_EXTRA "}";

	zassert_equal(parse_json(json, &out), HIL_SOURCE_PARSE_OK, "parse");
	zassert_equal(out.mode, HIL_SOURCE_MODE_MONO, "mode");
	zassert_equal(out.profile, HIL_SOURCE_PROFILE_48_3_1, "profile");
	zassert_equal(out.scored_sdu_count, 100U, "scored_sdu_count");
	zassert_equal(out.signal_seed, 123456789U, "signal_seed");
	zassert_equal(out.reconnect_policy, HIL_SOURCE_RECONNECT_ONCE, "policy");
	zassert_equal(out.peer_address_type, HIL_SOURCE_ADDR_PUBLIC, "addr type");
	zassert_mem_equal(out.peer_address, "\xAA\xBB\xCC\xDD\xEE\xFF", 6, "peer address bytes");
}

ZTEST(hil_source_control, test_mode_b_and_other_enum_variants)
{
	struct hil_source_command_in out;
	const char *json =
		CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mode_a\",\"profile\":\"48_4_1\","
			      "\"scored_sdu_count\":1,\"signal_seed\":4294967295,"
			      "\"reconnect_policy\":\"none\"}";

	zassert_equal(parse_json(json, &out), HIL_SOURCE_PARSE_OK, "parse");
	zassert_equal(out.mode, HIL_SOURCE_MODE_A, "mode_a");
	zassert_equal(out.profile, HIL_SOURCE_PROFILE_48_4_1, "48_4_1");
	zassert_equal(out.scored_sdu_count, 1U, "count boundary");
	zassert_equal(out.signal_seed, UINT32_MAX, "seed boundary");
	zassert_equal(out.reconnect_policy, HIL_SOURCE_RECONNECT_NONE, "none");
}

ZTEST(hil_source_control, test_canonical_name_helpers)
{
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_HELLO), "hello"), 0);
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_IDLE), "idle"), 0);
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_UNPAIR), "unpair"), 0);
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_CONFIGURE), "configure"), 0);
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_START), "start"), 0);
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_STOP), "stop"), 0);
	zassert_equal(strcmp(hil_source_command_name(HIL_SOURCE_CMD_STATUS), "status"), 0);
	zassert_equal(strcmp(hil_source_mode_name(HIL_SOURCE_MODE_MONO), "mono"), 0);
	zassert_equal(strcmp(hil_source_mode_name(HIL_SOURCE_MODE_A), "mode_a"), 0);
	zassert_equal(strcmp(hil_source_mode_name(HIL_SOURCE_MODE_B), "mode_b"), 0);
	zassert_equal(strcmp(hil_source_profile_name(HIL_SOURCE_PROFILE_48_3_1), "48_3_1"), 0);
	zassert_equal(strcmp(hil_source_profile_name(HIL_SOURCE_PROFILE_48_4_1), "48_4_1"), 0);
	zassert_equal(strcmp(hil_source_reconnect_policy_name(HIL_SOURCE_RECONNECT_NONE), "none"),
		      0);
	zassert_equal(strcmp(hil_source_reconnect_policy_name(HIL_SOURCE_RECONNECT_ONCE), "once"),
		      0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_IDLE), "idle"), 0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_CONFIGURED), "configured"),
		      0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_CONNECTING), "connecting"),
		      0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_SECURED), "secured"), 0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_DISCOVERED), "discovered"),
		      0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_QOS), "qos"), 0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_STREAMING), "streaming"),
		      0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_SCORED_COMPLETE),
			     "scored_complete"),
		      0);
	zassert_equal(strcmp(hil_source_state_name(HIL_SOURCE_RUN_STATE_TEARDOWN), "teardown"), 0);
	zassert_equal(strcmp(hil_source_verdict_name(HIL_SOURCE_VERDICT_PASS), "pass"), 0);
	zassert_equal(strcmp(hil_source_verdict_name(HIL_SOURCE_VERDICT_FAIL), "fail"), 0);

	/* Unknown enum values return NULL, never a fallback. */
	zassert_is_null(hil_source_command_name((enum hil_source_command)99), "cmd");
	zassert_is_null(hil_source_mode_name((enum hil_source_mode)99), "mode");
	zassert_is_null(hil_source_profile_name((enum hil_source_profile)99), "profile");
	zassert_is_null(hil_source_reconnect_policy_name((enum hil_source_reconnect_policy)99),
			"policy");
	zassert_is_null(hil_source_state_name((enum hil_source_run_state)99), "state");
	zassert_is_null(hil_source_verdict_name(HIL_SOURCE_VERDICT_NONE), "verdict none");
	zassert_is_null(hil_source_verdict_name((enum hil_source_verdict)99), "verdict");

	zassert_equal(strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_OK), "ok"), 0);
	zassert_equal(strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_TOO_LONG), "too_long"),
		      0);
	zassert_equal(strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_SYNTAX), "syntax"), 0);
	zassert_equal(
		strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_UNKNOWN_KEY), "unknown_key"),
		0);
	zassert_equal(strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_DUPLICATE_KEY),
			     "duplicate_key"),
		      0);
	zassert_equal(
		strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_MISSING_KEY), "missing_key"),
		0);
	zassert_equal(
		strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_WRONG_TYPE), "wrong_type"), 0);
	zassert_equal(strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_RANGE), "range"), 0);
	zassert_equal(strcmp(hil_source_parse_result_name(HIL_SOURCE_PARSE_UNSUPPORTED_VALUE),
			     "unsupported_value"),
		      0);
	zassert_is_null(hil_source_parse_result_name((enum hil_source_parse_result)99),
			"parse result");
}

/* ── 2. whitespace and allowed JSON escapes ───────────────────────── */

ZTEST(hil_source_control, test_whitespace_is_accepted)
{
	expect_ok("{\n\t\"protocol_version\" : 1 ,\n"
		  " \"command\" : \"hello\" ,\n"
		  " \"command_id\" : \"cmd-0001\" ,\n"
		  " \"run_id\" : \"run-0001\" \n}",
		  HIL_SOURCE_CMD_HELLO);
}

ZTEST(hil_source_control, test_allowed_escapes_lex_then_semantics_reject)
{
	/* \\\\ decodes to a backslash: lexically fine, then the decoded
	 * value is outside the canonical grammar. */
	expect_err("{\"protocol_version\":1,\"command\":\"he\\\\llo\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	/* \\/ decodes to '/', also outside the safe identifier set. */
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\\/1\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	/* \\t decodes to a control character: rejected semantically. */
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\\t1\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
}

/* ── 3. malformed inputs ──────────────────────────────────────────── */

ZTEST(hil_source_control, test_non_object_and_malformed_roots)
{
	expect_err("[]", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("\"hello\"", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("42", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("null", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("true", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,}", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,", HIL_SOURCE_PARSE_SYNTAX);
}

ZTEST(hil_source_control, test_trailing_content_rejected)
{
	expect_err(CMD_HELLO "x", HIL_SOURCE_PARSE_SYNTAX);
	expect_err(CMD_HELLO " {}", HIL_SOURCE_PARSE_SYNTAX);
	expect_err("x" CMD_HELLO, HIL_SOURCE_PARSE_SYNTAX);
}

ZTEST(hil_source_control, test_overlong_line_rejected)
{
	char buf[600];
	struct hil_source_command_in out;

	memset(buf, ' ', sizeof(buf));
	memcpy(buf, CMD_HELLO "}", strlen(CMD_HELLO "}"));
	zassert_equal(hil_source_parse(buf, 512U, &out), HIL_SOURCE_PARSE_TOO_LONG, "512 bytes");
	zassert_equal(hil_source_parse(buf, 511U, &out), HIL_SOURCE_PARSE_OK, "511 bytes accepted");
}

ZTEST(hil_source_control, test_string_overflow_rejected)
{
	char run_id[220];
	char json[600];

	memset(run_id, 'a', sizeof(run_id) - 1U);
	run_id[65] = '\0'; /* one past the 64-char run-id maximum */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\","
		 "\"command_id\":\"c\",\"run_id\":\"%s\"}",
		 run_id);
	expect_err(json, HIL_SOURCE_PARSE_RANGE);

	/* A lexically overlong value (beyond the bounded decode buffer)
	 * is consumed safely and reported as range. */
	memset(run_id, 'a', 200U);
	run_id[200] = '\0';
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\","
		 "\"command_id\":\"c\",\"run_id\":\"%s\"}",
		 run_id);
	expect_err(json, HIL_SOURCE_PARSE_RANGE);
}

ZTEST(hil_source_control, test_invalid_escape_unicode_control_rejected)
{
	expect_err("{\"protocol_version\":1,\"command\":\"he\\qllo\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"he\\ullo\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	/* \\n is an allowed escape: lexed fine, decoded control character
	 * rejected by the semantic grammar. */
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\\n1\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\",\n}",
		   HIL_SOURCE_PARSE_SYNTAX);
}

/* ── 4. unknown / duplicate / missing / extra / wrong-type ────────── */

ZTEST(hil_source_control, test_unknown_duplicate_missing_extra)
{
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\",\"bogus\":1}",
		   HIL_SOURCE_PARSE_UNKNOWN_KEY);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\",\"command_id\":\"d\"}",
		   HIL_SOURCE_PARSE_DUPLICATE_KEY);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\",\"command_id\":\"c\"}",
		   HIL_SOURCE_PARSE_MISSING_KEY);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_MISSING_KEY);
	expect_err("{\"command\":\"hello\",\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_MISSING_KEY);
	expect_err(CMD_HELLO ", \"mode\":\"mono\"}", HIL_SOURCE_PARSE_UNKNOWN_KEY);
	expect_err(CMD_HELLO ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\"}",
		   HIL_SOURCE_PARSE_UNKNOWN_KEY);
	expect_err("{\"protocol_version\":1,\"command\":\"configure\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"," CMD_ADDR
		   ",\"mode\":\"mono\",\"profile\":\"48_3_1\","
		   "\"scored_sdu_count\":1,\"signal_seed\":2,\"reconnect_policy\":\"once\""
		   ",\"extra\":true}",
		   HIL_SOURCE_PARSE_UNKNOWN_KEY);
}

ZTEST(hil_source_control, test_wrong_type_rejected)
{
	expect_err("{\"protocol_version\":\"1\",\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":5,\"command_id\":\"c\","
		   "\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":5,\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1]}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":{\"a\":1}}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"configure\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"," CMD_ADDR
		   ",\"mode\":\"mono\",\"profile\":\"48_3_1\","
		   "\"scored_sdu_count\":\"100\",\"signal_seed\":2,"
		   "\"reconnect_policy\":\"once\"}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\",\"bogus\":5}",
		   HIL_SOURCE_PARSE_UNKNOWN_KEY);
}

/* ── 5. bool/null/array/object/negative/fraction/exponent/plus/leading-zero ── */

ZTEST(hil_source_control, test_scalar_type_rejections)
{
	expect_err("{\"protocol_version\":true,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":null,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err(CMD_HELLO "}", HIL_SOURCE_PARSE_OK); /* sanity */
	expect_err("{\"protocol_version\":-1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":+1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1.0,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1e2,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":01,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\",\"scored_sdu_count\":007}",
		   HIL_SOURCE_PARSE_SYNTAX);
}

/* ── 6. integer overflow and semantic ranges ──────────────────────── */

ZTEST(hil_source_control, test_integer_overflow_and_ranges)
{
	expect_err("{\"protocol_version\":2,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_RANGE);
	expect_err("{\"protocol_version\":0,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_RANGE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mono\","
				 "\"profile\":\"48_3_1\",\"scored_sdu_count\":0,"
				 "\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		   HIL_SOURCE_PARSE_RANGE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mono\","
				 "\"profile\":\"48_3_1\",\"scored_sdu_count\":20001,"
				 "\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		   HIL_SOURCE_PARSE_RANGE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mono\","
				 "\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
				 "\"signal_seed\":0,\"reconnect_policy\":\"none\"}",
		   HIL_SOURCE_PARSE_RANGE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mono\","
				 "\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
				 "\"signal_seed\":4294967296,\"reconnect_policy\":\"none\"}",
		   HIL_SOURCE_PARSE_RANGE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_RANGE);
}

ZTEST(hil_source_control, test_unsupported_canonical_values)
{
	expect_err("{\"protocol_version\":1,\"command\":\"foobar\","
		   "\"command_id\":\"c\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"stereo\","
				 "\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
				 "\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mono\","
				 "\"profile\":\"48_5_1\",\"scored_sdu_count\":1,"
				 "\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_CONFIGURE ", " CMD_ADDR ", \"mode\":\"mono\","
				 "\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
				 "\"signal_seed\":2,\"reconnect_policy\":\"always\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_UNPAIR ", \"peer_address\":\"aa:bb:cc:dd:ee:ff\", "
			      "\"peer_address_type\":\"public\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_UNPAIR ", \"peer_address\":\"AA:BB:CC:DD:EE\", "
			      "\"peer_address_type\":\"public\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_UNPAIR ", \"peer_address\":\"AA:BB:CC:DD:EE:FG\", "
			      "\"peer_address_type\":\"public\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err(CMD_UNPAIR ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\", "
			      "\"peer_address_type\":\"private\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"bad id\",\"run_id\":\"r\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"-run\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":\"r!\"}",
		   HIL_SOURCE_PARSE_UNSUPPORTED_VALUE);
}

/* ── 7. parser failure leaves output bytes unchanged ──────────────── */

ZTEST(hil_source_control, test_failure_leaves_output_unchanged)
{
	static const char *const bad[] = {
		"[]",
		"{\"protocol_version\":1",
		CMD_HELLO ", \"bogus\":1}",
		CMD_HELLO ", \"mode\":\"mono\"}",
		"{\"protocol_version\":1,\"command\":\"hello\",\"command_id\":\"c\"}",
		"{\"protocol_version\":\"1\",\"command\":\"hello\",\"command_id\":\"c\","
		"\"run_id\":\"r\"}",
		CMD_HELLO ", \"command_id\":\"cmd-0001\"}",
		"{\"protocol_version\":1,\"command\":\"hello\",\"command_id\":\"c\","
		"\"run_id\":\"r\",\"scored_sdu_count\":99999999999999999999}",
	};
	size_t i;

	for (i = 0U; i < sizeof(bad) / sizeof(bad[0]); i++) {
		struct hil_source_command_in out;
		struct hil_source_command_in snapshot;

		memset(&out, 0xA5, sizeof(out));
		snapshot = out;
		(void)hil_source_parse(bad[i], strlen(bad[i]), &out);
		zassert_mem_equal(&out, &snapshot, sizeof(out), "output mutated for %s", bad[i]);
	}
}

/* ── 8. exact HIL1 envelope and convenience records ───────────────── */

ZTEST(hil_source_control, test_exact_envelope_bytes)
{
	char buf[512];
	const char *expected = "{\"protocol_version\":1,\"kind\":\"state\",\"firmware_id\":"
			       "\"le-audio-hil-source-rh1\",\"monotonic_ms\":123,"
			       "\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\","
			       "\"segment\":0,\"data\":{\"state\":\"idle\"}}";
	int n = hil_source_record_format_line(buf, sizeof(buf), 123U, "cmd-0001", "run-0001", 0U,
					      "state", "{\"state\":\"idle\"}",
					      strlen("{\"state\":\"idle\"}"));

	zassert_true(n > 0, "format failed: %d", n);
	zassert_equal(strcmp(buf, expected), 0, "envelope mismatch:\n%s\n%s", buf, expected);
	zassert_equal(n, (int)strlen(expected), "returned length");
}

ZTEST(hil_source_control, test_convenience_records_exact)
{
	char buf[512];
	const char *expected_ack =
		"{\"protocol_version\":1,\"kind\":\"ack\",\"firmware_id\":"
		"\"le-audio-hil-source-rh1\",\"monotonic_ms\":10,"
		"\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\","
		"\"segment\":0,\"data\":{\"command\":\"start\",\"accepted\":true}}";
	const char *expected_terminal =
		"{\"protocol_version\":1,\"kind\":\"terminal\",\"firmware_id\":"
		"\"le-audio-hil-source-rh1\",\"monotonic_ms\":99,"
		"\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\","
		"\"segment\":0,\"data\":{\"verdict\":\"pass\"}}";
	const char *expected_status = "{\"protocol_version\":1,\"kind\":\"status\",\"firmware_id\":"
				      "\"le-audio-hil-source-rh1\",\"monotonic_ms\":7,"
				      "\"command_id\":\"cmd-0009\",\"run_id\":\"run-0001\","
				      "\"segment\":1,\"data\":{\"command\":\"stop\",\"ok\":false,"
				      "\"error\":\"busy\"}}";
	int n;

	n = hil_source_record_format_ack_start(buf, sizeof(buf), 10U, "cmd-0001", "run-0001", 0U);
	zassert_equal(strcmp(buf, expected_ack), 0, "ack:\n%s\n%s", buf, expected_ack);

	n = hil_source_record_format_terminal(buf, sizeof(buf), 99U, "cmd-0001", "run-0001", 0U,
					      HIL_SOURCE_VERDICT_PASS);
	zassert_true(n > 0, "terminal format: %d", n);
	zassert_equal(strcmp(buf, expected_terminal), 0, "terminal:\n%s\n%s", buf,
		      expected_terminal);

	n = hil_source_record_format_status(buf, sizeof(buf), 7U, "cmd-0009", "run-0001", 1U,
					    HIL_SOURCE_CMD_STOP, false, HIL_SOURCE_ERROR_BUSY);
	zassert_true(n > 0, "status format: %d", n);
	zassert_equal(strcmp(buf, expected_status), 0, "status:\n%s\n%s", buf, expected_status);

	n = hil_source_record_format_state(buf, sizeof(buf), 5U, "cmd-0001", "run-0001", 0U,
					   HIL_SOURCE_RUN_STATE_STREAMING);
	zassert_true(n > 0, "state format: %d", n);
	zassert_not_null(strstr(buf, "\"data\":{\"state\":\"streaming\"}"), "state data: %s", buf);
}

ZTEST(hil_source_control, test_record_truncation_and_rejection)
{
	char buf[8];

	/* Truncation: capacity below the needed length. */
	zassert_equal(hil_source_record_format_state(buf, sizeof(buf), 5U, "cmd-0001", "run-0001",
						     0U, HIL_SOURCE_RUN_STATE_IDLE),
		      -ENOBUFS, "truncation must return -ENOBUFS");
	zassert_equal(buf[0], '\0', "buffer must be empty on failure");
	zassert_equal(hil_source_record_format_state(buf, 0U, 5U, "cmd-0001", "run-0001", 0U,
						     HIL_SOURCE_RUN_STATE_IDLE),
		      -ENOBUFS, "zero capacity");
	zassert_equal(hil_source_record_format_state(NULL, 8U, 5U, "cmd-0001", "run-0001", 0U,
						     HIL_SOURCE_RUN_STATE_IDLE),
		      -EINVAL, "null buffer");
	/* Invalid kind. */
	zassert_equal(hil_source_record_format_line(buf, sizeof(buf), 1U, "c", "r", 0U, "bogus",
						    "{}", 2U),
		      -EINVAL, "bad kind");
	zassert_equal(buf[0], '\0', "empty after bad kind");

	/* Unsafe IDs. */
	zassert_equal(hil_source_record_format_state(buf, sizeof(buf), 1U, "bad id", "r", 0U,
						     HIL_SOURCE_RUN_STATE_IDLE),
		      -EINVAL, "bad command id");
	zassert_equal(buf[0], '\0', "empty after bad command id");
	zassert_equal(hil_source_record_format_state(buf, sizeof(buf), 1U, "c", "-run", 0U,
						     HIL_SOURCE_RUN_STATE_IDLE),
		      -EINVAL, "bad run id");

	/* Unsafe data_json span: no object delimiters, CR/LF/NUL inside. */
	zassert_equal(
		hil_source_record_format_line(buf, sizeof(buf), 1U, "c", "r", 0U, "state", "x", 1U),
		-EINVAL, "not an object");
	zassert_equal(hil_source_record_format_line(buf, sizeof(buf), 1U, "c", "r", 0U, "state",
						    "{\"a\":1\n}", 8U),
		      -EINVAL, "LF inside data");
	zassert_equal(hil_source_record_format_line(buf, sizeof(buf), 1U, "c", "r", 0U, "state",
						    "{\"a\":1\r}", 8U),
		      -EINVAL, "CR inside data");
	zassert_equal(hil_source_record_format_line(buf, sizeof(buf), 1U, "c", "r", 0U, "state",
						    "{\"a\0b\":1}", 9U),
		      -EINVAL, "NUL inside data");

	/* Bad convenience inputs. */
	zassert_equal(hil_source_record_format_terminal(buf, sizeof(buf), 1U, "c", "r", 0U,
							HIL_SOURCE_VERDICT_NONE),
		      -EINVAL, "terminal without verdict");
	zassert_equal(buf[0], '\0', "empty after bad verdict");
	zassert_equal(hil_source_record_format_status(buf, sizeof(buf), 1U, "c", "r", 0U,
						      (enum hil_source_command)99, true,
						      HIL_SOURCE_ERROR_OK),
		      -EINVAL, "bad command in status");
	zassert_equal(hil_source_record_format_status(buf, sizeof(buf), 1U, "c", "r", 0U,
						      HIL_SOURCE_CMD_START, true,
						      "not-a-stable-error"),
		      -EINVAL, "bad error name");
}

/* ── 9. state single-segment lifecycle and one reconnect segment ──── */

ZTEST(hil_source_control, test_single_segment_lifecycle_pass)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 3U,
		     HIL_SOURCE_RECONNECT_NONE);
	zassert_true(st.has_config, "has_config after configure");
	state_to_streaming(&st);
	zassert_equal(st.segment, 0U, "segment 0");
	zassert_true(st.active, "active while streaming");
	zassert_equal(st.current_state, HIL_SOURCE_RUN_STATE_STREAMING, "streaming");

	state_submit_scored(&st, 0U, 3U);
	zassert_equal(st.counters[0].submitted_scored_sdus, 3U, "scored count");
	state_finish_segment(&st);
	zassert_equal(st.current_state, HIL_SOURCE_RUN_STATE_TEARDOWN, "teardown");
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), 0, "terminal pass");
	zassert_false(st.active, "terminal clears active");
	zassert_equal(st.terminal, HIL_SOURCE_VERDICT_PASS, "verdict stored");
}

ZTEST(hil_source_control, test_reconnect_segment_lifecycle_pass)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_3_1, 2U,
		     HIL_SOURCE_RECONNECT_ONCE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 2U);
	state_finish_segment(&st);

	zassert_equal(hil_source_state_next_segment(&st), 0, "next segment");
	zassert_equal(st.segment, 1U, "segment increments");
	zassert_equal(st.current_state, HIL_SOURCE_RUN_STATE_CONNECTING,
		      "reconnect starts at connecting");
	zassert_equal(st.scored_baseline[0], 2U, "baseline recorded");

	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_SECURED), 0,
		      "reconnect secured");
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_DISCOVERED), 0,
		      "reconnect discovered");
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_QOS), 0, "reconnect qos");
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_STREAMING), 0,
		      "reconnect streaming");
	state_submit_scored(&st, 0U, 2U);
	zassert_equal(st.counters[0].submitted_scored_sdus, 4U, "two segments scored");
	state_finish_segment(&st);
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), 0,
		      "pass after both segments");
}

/* ── 10. rejected transitions leave state bytes unchanged ─────────── */

ZTEST(hil_source_control, test_rejected_operations_are_atomic)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 2U,
		     HIL_SOURCE_RECONNECT_ONCE);
	state_to_streaming(&st);

	/* Repeat. */
	snap = st;
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_STREAMING), -EINVAL,
		      "repeat state");
	zassert_mem_equal(&st, &snap, sizeof(st), "repeat mutated state");

	/* Skip. */
	snap = st;
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_SCORED_COMPLETE), -EINVAL,
		      "skip without target");
	zassert_mem_equal(&st, &snap, sizeof(st), "skip mutated state");

	/* Legal next step: submit the exact target, advance to
	 * scored_complete, then prove regression is rejected atomically. */
	state_submit_scored(&st, 0U, 2U);
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_SCORED_COMPLETE), 0,
		      "advance scored_complete");
	snap = st;
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_STREAMING), -EINVAL,
		      "regression");
	zassert_mem_equal(&st, &snap, sizeof(st), "regression mutated state");

	/* Out-of-range enum. */
	snap = st;
	zassert_equal(hil_source_state_advance(&st, (enum hil_source_run_state)99), -EINVAL,
		      "invalid enum");
	zassert_mem_equal(&st, &snap, sizeof(st), "invalid enum mutated state");

	/* Early terminal before teardown. */
	snap = st;
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), -EINVAL,
		      "early terminal");
	zassert_mem_equal(&st, &snap, sizeof(st), "early terminal mutated state");

	/* NONE verdict. */
	snap = st;
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_NONE), -EINVAL,
		      "none verdict");
	zassert_mem_equal(&st, &snap, sizeof(st), "none verdict mutated state");

	/* Second reconnect after the first is used. */
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_TEARDOWN), 0,
		      "advance teardown");
	zassert_equal(hil_source_state_next_segment(&st), 0, "first reconnect");
	snap = st;
	zassert_equal(hil_source_state_next_segment(&st), -EINVAL, "second reconnect");
	zassert_mem_equal(&st, &snap, sizeof(st), "second reconnect mutated state");

	/* Next segment before teardown. */
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_SECURED), 0,
		      "reconnect secured");
	snap = st;
	zassert_equal(hil_source_state_next_segment(&st), -EINVAL, "next segment before teardown");
	zassert_mem_equal(&st, &snap, sizeof(st), "early next segment mutated state");
}

ZTEST(hil_source_control, test_scored_overrun_blocks_scored_complete)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 2U,
		     HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 3U); /* overrun the target of 2 */
	snap = st;
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_SCORED_COMPLETE), -EINVAL,
		      "overrun blocks scored_complete");
	zassert_mem_equal(&st, &snap, sizeof(st), "overrun mutated state");
}

ZTEST(hil_source_control, test_pass_fails_on_error_and_send_failure)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 1U,
		     HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 1U);
	zassert_equal(hil_source_state_counter_send_failure(&st, 0U), 0, "send failure");
	state_finish_segment(&st);
	snap = st;
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), -EINVAL,
		      "pass with send failure");
	zassert_mem_equal(&st, &snap, sizeof(st), "send-failure pass mutated state");

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 1U,
		     HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 1U);
	zassert_equal(hil_source_state_error_report(&st, -EIO), 0, "error report");
	state_finish_segment(&st);
	snap = st;
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), -EINVAL,
		      "pass with first error");
	zassert_mem_equal(&st, &snap, sizeof(st), "first-error pass mutated state");
}

/* ── 11. configure/start/reset/stop idempotence and busy/missing-config ── */

ZTEST(hil_source_control, test_start_requires_configuration)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	zassert_equal(hil_source_state_start(&st), -EINVAL, "start without config");
	zassert_equal(st.active, false, "still inactive");
}

ZTEST(hil_source_control, test_busy_and_idempotence)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 1U,
		     HIL_SOURCE_RECONNECT_NONE);
	zassert_equal(hil_source_state_start(&st), 0, "start");
	zassert_equal(hil_source_state_start(&st), -EBUSY, "start while active");
	zassert_equal(hil_source_state_configure(&st, &st.config), -EBUSY,
		      "configure while active");
	zassert_equal(hil_source_state_stop_request(&st), 0, "stop 1");
	zassert_equal(hil_source_state_stop_request(&st), 0, "stop idempotent");
	zassert_true(st.stop_requested, "stop flag set");
	zassert_equal(hil_source_state_stop_request(&st), 0, "stop again");

	hil_source_state_reset(&st);
	hil_source_state_reset(&st);
	zassert_false(st.has_config, "reset clears config");
	zassert_equal(hil_source_state_start(&st), -EINVAL, "start after reset");
}

ZTEST(hil_source_control, test_configure_validation)
{
	struct hil_source_state st;
	struct hil_source_config cfg;

	hil_source_state_reset(&st);
	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = HIL_SOURCE_MODE_MONO;
	cfg.profile = HIL_SOURCE_PROFILE_48_4_1;
	cfg.peer_address_type = HIL_SOURCE_ADDR_PUBLIC;
	cfg.scored_sdu_count = 1U;
	cfg.signal_seed = 1U;
	cfg.reconnect_policy = HIL_SOURCE_RECONNECT_NONE;

	cfg.mode = (enum hil_source_mode)99;
	zassert_equal(hil_source_state_configure(&st, &cfg), -EINVAL, "bad mode");
	cfg.mode = HIL_SOURCE_MODE_MONO;

	cfg.profile = (enum hil_source_profile)99;
	zassert_equal(hil_source_state_configure(&st, &cfg), -EINVAL, "bad profile");
	cfg.profile = HIL_SOURCE_PROFILE_48_4_1;

	cfg.peer_address_type = (enum hil_source_address_type)99;
	zassert_equal(hil_source_state_configure(&st, &cfg), -EINVAL, "bad addr type");
	cfg.peer_address_type = HIL_SOURCE_ADDR_PUBLIC;

	cfg.scored_sdu_count = 0U;
	zassert_equal(hil_source_state_configure(&st, &cfg), -EINVAL, "count 0");
	cfg.scored_sdu_count = HIL_SOURCE_MAX_SCORED_SDUS + 1U;
	zassert_equal(hil_source_state_configure(&st, &cfg), -EINVAL, "count max+1");
	cfg.scored_sdu_count = 1U;

	cfg.signal_seed = 0U;
	zassert_equal(hil_source_state_configure(&st, &cfg), -EINVAL, "seed 0");
	cfg.signal_seed = 1U;

	zassert_equal(hil_source_state_configure(&st, &cfg), 0, "valid config");
	zassert_true(st.has_config, "config stored");
	zassert_equal(hil_source_state_configure(&st, &cfg), 0, "re-configure");

	zassert_equal(hil_source_state_configure(&st, NULL), -EINVAL, "null config");
}

/* ── 12. stream-count derivation, counter bounds, first-error-wins ── */

ZTEST(hil_source_control, test_mode_stream_count_derivation)
{
	zassert_equal(hil_source_mode_stream_count(HIL_SOURCE_MODE_MONO), 1U, "mono");
	zassert_equal(hil_source_mode_stream_count(HIL_SOURCE_MODE_A), 2U, "mode_a");
	zassert_equal(hil_source_mode_stream_count(HIL_SOURCE_MODE_B), 1U, "mode_b");
	zassert_equal(hil_source_mode_stream_count((enum hil_source_mode)99), 0U, "invalid mode");
}

ZTEST(hil_source_control, test_mode_a_both_streams_required)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_4_1, 2U,
		     HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 2U);
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_SCORED_COMPLETE), -EINVAL,
		      "stream 1 underrun blocks scored_complete");
	state_submit_scored(&st, 1U, 2U);
	state_finish_segment(&st);
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), 0, "mode A pass");
}

ZTEST(hil_source_control, test_counter_bounds_overflow_first_error)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_B, HIL_SOURCE_PROFILE_48_3_1, 1U,
		     HIL_SOURCE_RECONNECT_NONE);

	/* Inactive counter ops are rejected. */
	zassert_equal(hil_source_state_counter_submit(&st, 0U), -EINVAL, "submit before start");

	zassert_equal(hil_source_state_start(&st), 0, "start");
	zassert_equal(hil_source_state_counter_submit(&st, 1U), -EINVAL, "Mode B stream 1 invalid");
	zassert_equal(hil_source_state_counter_send_failure(&st, 1U), -EINVAL,
		      "send failure stream 1 invalid");
	zassert_equal(hil_source_state_counter_sent_callback(&st, 1U), -EINVAL,
		      "callback stream 1 invalid");

	/* Overflow is rejected without mutation. */
	st.counters[0].submitted_sdus = UINT32_MAX;
	zassert_equal(hil_source_state_counter_submit(&st, 0U), -EOVERFLOW, "submit overflow");
	zassert_equal(st.counters[0].submitted_sdus, UINT32_MAX, "unchanged on overflow");
	st.counters[0].submitted_sdus = 0U;

	/* First error wins and is never overwritten. */
	zassert_equal(hil_source_state_error_report(&st, -EIO), 0, "first error");
	zassert_equal(st.first_error, -EIO, "stored");
	zassert_equal(hil_source_state_error_report(&st, -ENOMEM), 0, "later error");
	zassert_equal(st.first_error, -EIO, "first error preserved");
	zassert_equal(hil_source_state_error_report(&st, 0), -EINVAL, "zero error");
	zassert_equal(hil_source_state_error_report(&st, -EINVAL), 0, "third error");
	zassert_equal(st.first_error, -EIO, "still first error");
}

/* ── 13. immutable post-terminal snapshot ─────────────────────────── */

ZTEST(hil_source_control, test_post_terminal_snapshot_immutable)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1, 1U,
		     HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 1U);
	state_finish_segment(&st);
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), 0, "terminal pass");

	snap = st;
	zassert_equal(hil_source_state_counter_submit(&st, 0U), -EINVAL, "counter");
	zassert_mem_equal(&st, &snap, sizeof(st), "counter mutated snapshot");
	zassert_equal(hil_source_state_advance(&st, HIL_SOURCE_RUN_STATE_CONFIGURED), -EINVAL,
		      "advance");
	zassert_mem_equal(&st, &snap, sizeof(st), "advance mutated snapshot");
	zassert_equal(hil_source_state_error_report(&st, -EIO), -EINVAL, "error");
	zassert_mem_equal(&st, &snap, sizeof(st), "error mutated snapshot");
	zassert_equal(hil_source_state_stop_request(&st), -EINVAL, "stop");
	zassert_mem_equal(&st, &snap, sizeof(st), "stop mutated snapshot");
	zassert_equal(hil_source_state_next_segment(&st), -EINVAL, "next segment");
	zassert_mem_equal(&st, &snap, sizeof(st), "next segment mutated snapshot");
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_FAIL), -EINVAL,
		      "duplicate terminal");
	zassert_mem_equal(&st, &snap, sizeof(st), "duplicate terminal mutated snapshot");

	/* A fresh start clears the snapshot and re-runs. */
	zassert_equal(hil_source_state_start(&st), 0, "restart");
	zassert_equal(st.terminal, HIL_SOURCE_VERDICT_NONE, "terminal cleared");
	zassert_equal(st.first_error, 0, "error cleared");
	zassert_equal(st.counters[0].submitted_sdus, 0U, "counters cleared");
	zassert_true(st.active, "active again");
}

/* ── parser: byte-atomic failure with a valid common header ───────── */

ZTEST(hil_source_control, test_valid_common_header_failure_is_atomic)
{
	static const char *const bad[] = {
		/* valid unpair header, invalid peer address/type values */
		CMD_UNPAIR ", \"peer_address\":\"aa:bb:cc:dd:ee:ff\","
		"\"peer_address_type\":\"public\"}",
		CMD_UNPAIR ", \"peer_address\":\"AA:BB:CC:DD:EE\","
		"\"peer_address_type\":\"public\"}",
		CMD_UNPAIR ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\","
		"\"peer_address_type\":\"private\"}",
		/* valid configure header, invalid configure values */
		CMD_CONFIGURE ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\","
		"\"peer_address_type\":\"public\",\"mode\":\"stereo\","
		"\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
		"\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		CMD_CONFIGURE ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\","
		"\"peer_address_type\":\"public\",\"mode\":\"mono\","
		"\"profile\":\"48_5_1\",\"scored_sdu_count\":1,"
		"\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		CMD_CONFIGURE ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\","
		"\"peer_address_type\":\"public\",\"mode\":\"mono\","
		"\"profile\":\"48_3_1\",\"scored_sdu_count\":0,"
		"\"signal_seed\":2,\"reconnect_policy\":\"none\"}",
		CMD_CONFIGURE ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\","
		"\"peer_address_type\":\"public\",\"mode\":\"mono\","
		"\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
		"\"signal_seed\":0,\"reconnect_policy\":\"none\"}",
		CMD_CONFIGURE ", \"peer_address\":\"AA:BB:CC:DD:EE:FF\","
		"\"peer_address_type\":\"public\",\"mode\":\"mono\","
		"\"profile\":\"48_3_1\",\"scored_sdu_count\":1,"
		"\"signal_seed\":2,\"reconnect_policy\":\"always\"}",
	};
	size_t i;

	for (i = 0U; i < sizeof(bad) / sizeof(bad[0]); i++) {
		struct hil_source_command_in out;
		struct hil_source_command_in snapshot;

		memset(&out, 0xA5, sizeof(out));
		snapshot = out;
		zassert_not_equal(hil_source_parse(bad[i], strlen(bad[i]), &out),
				  HIL_SOURCE_PARSE_OK, "case %zu must fail", i);
		zassert_mem_equal(&out, &snapshot, sizeof(out),
				  "case %zu mutated output", i);
	}
}

/* ── parser: nested JSON grammar validation ───────────────────────── */

static void build_run_id_json(char *out, size_t cap, const char *value)
{
	/* Truncation is impossible by construction (bounded value), but
	 * snprintf still warns about the %s width on small caps. */
	int n = snprintf(out, cap,
			 "{\"protocol_version\":1,\"command\":\"hello\","
			 "\"command_id\":\"c\",\"run_id\":%s}", value);

	zassert_true(n > 0 && (size_t)n < cap, "build_run_id_json truncated");
}

ZTEST(hil_source_control, test_nested_json_grammar)
{
	char json[320];

	/* Valid nested JSON in a scalar field stays wrong_type. */
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1,{\"a\":true}]}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":{\"a\":[1,2]}}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1,{\"b\":[true,null,\"s\"]}]}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[-1,2.5,1e3]}",
		   HIL_SOURCE_PARSE_WRONG_TYPE);

	/* Malformed nested JSON is syntax, never wrong_type. */
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1,2}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":{\"a\":}}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":{\"a\" 1}}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1 2]}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":{\"a\":1,}}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1,]}",
		   HIL_SOURCE_PARSE_SYNTAX);
	expect_err("{\"protocol_version\":1,\"command\":\"hello\","
		   "\"command_id\":\"c\",\"run_id\":[1,2",
		   HIL_SOURCE_PARSE_SYNTAX);

	/* Nesting within the fixed depth limit is valid; deeper fails. */
	{
		char value[300];
		size_t p = 0U;
		size_t i;

		for (i = 0U; i < 32U; i++) {
			value[p++] = '[';
		}
		value[p++] = '1';
		for (i = 0U; i < 32U; i++) {
			value[p++] = ']';
		}
		value[p] = '\0';
		build_run_id_json(json, sizeof(json), value);
		expect_err(json, HIL_SOURCE_PARSE_WRONG_TYPE);
	}
	{
		char value[300];
		size_t p = 0U;
		size_t i;

		for (i = 0U; i < 40U; i++) {
			value[p++] = '[';
		}
		value[p++] = '1';
		for (i = 0U; i < 40U; i++) {
			value[p++] = ']';
		}
		value[p] = '\0';
		build_run_id_json(json, sizeof(json), value);
		expect_err(json, HIL_SOURCE_PARSE_SYNTAX);
	}
}

/* ── state: memory-safe counter index rejection ───────────────────── */

ZTEST(hil_source_control, test_huge_stream_index_is_memory_safe)
{
	struct {
		uint8_t pre[8];
		struct hil_source_state st;
		uint8_t post[8];
	} guarded;
	struct hil_source_state snap;
	uint8_t i;

	memset(guarded.pre, 0x5A, sizeof(guarded.pre));
	memset(guarded.post, 0xA5, sizeof(guarded.post));
	hil_source_state_reset(&guarded.st);
	state_config(&guarded.st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_NONE);
	zassert_equal(hil_source_state_start(&guarded.st), 0, "start");
	snap = guarded.st;

	zassert_equal(hil_source_state_counter_submit(&guarded.st, UINT32_MAX),
		      -EINVAL, "huge submit index");
	zassert_equal(hil_source_state_counter_submit_scored(&guarded.st, UINT32_MAX),
		      -EINVAL, "huge scored index");
	zassert_equal(hil_source_state_counter_send_failure(&guarded.st, UINT32_MAX),
		      -EINVAL, "huge failure index");
	zassert_equal(hil_source_state_counter_sent_callback(&guarded.st, UINT32_MAX),
		      -EINVAL, "huge callback index");
	zassert_mem_equal(&guarded.st, &snap, sizeof(snap), "state changed");
	for (i = 0U; i < sizeof(guarded.pre); i++) {
		zassert_equal(guarded.pre[i], 0x5A, "pre canary %u", i);
	}
	for (i = 0U; i < sizeof(guarded.post); i++) {
		zassert_equal(guarded.post[i], 0xA5, "post canary %u", i);
	}
}

/* ── state: scored submission counts total and is overflow-atomic ─── */

ZTEST(hil_source_control, test_scored_submission_overflow_is_atomic)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_NONE);
	zassert_equal(hil_source_state_start(&st), 0, "start");

	/* Scored counter at max: neither counter may change. */
	st.counters[0].submitted_scored_sdus = UINT32_MAX;
	snap = st;
	zassert_equal(hil_source_state_counter_submit_scored(&st, 0U), -EOVERFLOW,
		      "scored overflow");
	zassert_mem_equal(&st, &snap, sizeof(st), "scored overflow mutated state");

	/* Total counter at max with a scored call: neither changes. */
	st.counters[0].submitted_scored_sdus = 0U;
	st.counters[0].submitted_sdus = UINT32_MAX;
	snap = st;
	zassert_equal(hil_source_state_counter_submit_scored(&st, 0U), -EOVERFLOW,
		      "total overflow blocks scored");
	zassert_mem_equal(&st, &snap, sizeof(st), "total overflow mutated state");
	zassert_equal(st.counters[0].submitted_scored_sdus, 0U,
		      "scored untouched after total overflow");
}

ZTEST(hil_source_control, test_pass_snapshot_total_ge_scored)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     2U, HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	zassert_equal(hil_source_state_counter_submit(&st, 0U), 0, "preamble submit");
	zassert_equal(hil_source_state_counter_submit(&st, 0U), 0, "tail submit");
	state_submit_scored(&st, 0U, 2U);
	zassert_equal(st.counters[0].submitted_sdus, 4U, "total submissions");
	zassert_equal(st.counters[0].submitted_scored_sdus, 2U, "scored submissions");
	zassert_true(st.counters[0].submitted_sdus >= st.counters[0].submitted_scored_sdus,
		      "total >= scored");
	state_finish_segment(&st);
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), 0,
		      "pass");
	zassert_true(st.counters[0].submitted_sdus >= st.counters[0].submitted_scored_sdus,
		      "pass snapshot total >= scored");
}

/* ── state: configure rejected while a terminal snapshot is present ── */

ZTEST(hil_source_control, test_post_terminal_configure_rejected)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 1U);
	state_finish_segment(&st);
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS), 0,
		      "terminal pass");
	snap = st;
	zassert_equal(hil_source_state_configure(&st, &st.config), -EBUSY,
		      "post-terminal configure");
	zassert_mem_equal(&st, &snap, sizeof(st), "post-terminal configure mutated state");

	/* Reset clears the snapshot and permits configure again. */
	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_NONE);
	zassert_true(st.has_config, "reconfigured after reset");
}

/* ── parser: long unknown-key classification ──────────────────────── */

ZTEST(hil_source_control, test_long_unknown_key_classification)
{
	char key[40];
	char value[220];
	char json[400];

	/* A key longer than any known key (16 decoded bytes) is unknown,
	 * never a range error from the bounded key buffer. */
	memset(key, 'x', 30U);
	key[30] = '\0';
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\","
		 "\"command_id\":\"c\",\"run_id\":\"r\",\"%s\":42}",
		 key);
	expect_err(json, HIL_SOURCE_PARSE_UNKNOWN_KEY);

	/* Valid nested value on a long unknown key stays unknown_key. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\","
		 "\"command_id\":\"c\",\"run_id\":\"r\",\"%s\":{\"a\":[1,2]}}",
		 key);
	expect_err(json, HIL_SOURCE_PARSE_UNKNOWN_KEY);

	/* Long scalar string value on a long unknown key: the value is
	 * discard-only after lexical validation, still unknown_key. */
	memset(value, 'v', 200U);
	value[200] = '\0';
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\","
		 "\"command_id\":\"c\",\"run_id\":\"r\",\"%s\":\"%s\"}",
		 key, value);
	expect_err(json, HIL_SOURCE_PARSE_UNKNOWN_KEY);

	/* A malformed value still fails syntax: lexical validity precedes
	 * schema class. */
	snprintf(json, sizeof(json),
		 "{\"protocol_version\":1,\"command\":\"hello\","
		 "\"command_id\":\"c\",\"run_id\":\"r\",\"%s\":{\"a\":}",
		 key);
	expect_err(json, HIL_SOURCE_PARSE_SYNTAX);
}

/* ── abort-to-teardown: causes, record bytes, transitions ──────────── */

ZTEST(hil_source_control, test_abort_cause_names_and_record_bytes)
{
	char buf[512];
	const char *expected =
		"{\"protocol_version\":1,\"kind\":\"state\",\"firmware_id\":"
		"\"le-audio-hil-source-rh1\",\"monotonic_ms\":10,"
		"\"command_id\":\"cmd-0001\",\"run_id\":\"run-0001\","
		"\"segment\":0,\"data\":{\"state\":\"teardown\",\"cause\":\"stop\"}}";

	zassert_equal(strcmp(hil_source_abort_cause_name(HIL_SOURCE_ABORT_STOP), "stop"), 0);
	zassert_equal(strcmp(hil_source_abort_cause_name(HIL_SOURCE_ABORT_TIMEOUT), "timeout"), 0);
	zassert_equal(strcmp(hil_source_abort_cause_name(HIL_SOURCE_ABORT_ERROR), "error"), 0);
	zassert_is_null(hil_source_abort_cause_name((enum hil_source_abort_cause)99));

	zassert_equal(hil_source_record_format_abort_teardown(buf, sizeof(buf), 10U,
							     "cmd-0001", "run-0001", 0U,
							     HIL_SOURCE_ABORT_STOP) > 0,
		      true, "abort record format");
	zassert_equal(strcmp(buf, expected), 0, "abort bytes:\n%s\n%s", buf, expected);

	zassert_true(hil_source_record_format_abort_teardown(buf, sizeof(buf), 10U,
							     "cmd-0001", "run-0001", 0U,
							     HIL_SOURCE_ABORT_TIMEOUT) > 0,
		      "timeout format");
	zassert_not_null(strstr(buf, "\"cause\":\"timeout\""), "timeout cause");

	zassert_true(hil_source_record_format_abort_teardown(buf, sizeof(buf), 10U,
							     "cmd-0001", "run-0001", 0U,
							     HIL_SOURCE_ABORT_ERROR) > 0,
		      "error format");
	zassert_not_null(strstr(buf, "\"cause\":\"error\""), "error cause");

	zassert_equal(hil_source_record_format_abort_teardown(buf, sizeof(buf), 10U,
							     "cmd-0001", "run-0001", 0U,
							     (enum hil_source_abort_cause)99),
		      -EINVAL, "invalid cause");
	zassert_equal(buf[0], '\0', "empty on invalid cause");
}

ZTEST(hil_source_control, test_abort_from_each_pre_teardown_state)
{
	static const enum hil_source_run_state states[] = {
		HIL_SOURCE_RUN_STATE_IDLE,
		HIL_SOURCE_RUN_STATE_CONFIGURED,
		HIL_SOURCE_RUN_STATE_CONNECTING,
		HIL_SOURCE_RUN_STATE_SECURED,
		HIL_SOURCE_RUN_STATE_DISCOVERED,
		HIL_SOURCE_RUN_STATE_QOS,
		HIL_SOURCE_RUN_STATE_STREAMING,
		HIL_SOURCE_RUN_STATE_SCORED_COMPLETE,
	};
	size_t i;

	for (i = 0U; i < sizeof(states) / sizeof(states[0]); i++) {
		struct hil_source_state st;

		hil_source_state_reset(&st);
		state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
			     1U, HIL_SOURCE_RECONNECT_NONE);
		zassert_equal(hil_source_state_start(&st), 0, "start %zu", i);
		for (enum hil_source_run_state s = HIL_SOURCE_RUN_STATE_CONFIGURED;
		     s <= states[i]; s++) {
			if (s == HIL_SOURCE_RUN_STATE_SCORED_COMPLETE) {
				zassert_equal(hil_source_state_counter_submit_scored(&st, 0U),
					      0, "scored submit %zu", i);
			}
			zassert_equal(hil_source_state_advance(&st, s), 0,
				      "advance to state %zu (%d)", i, (int)s);
		}
		zassert_equal(hil_source_state_abort_to_teardown(&st,
								HIL_SOURCE_ABORT_STOP),
			      0, "abort from state %zu", i);
		zassert_true(st.aborted, "aborted %zu", i);
		zassert_equal(st.abort_cause, HIL_SOURCE_ABORT_STOP, "cause %zu", i);
		zassert_equal(st.current_state, HIL_SOURCE_RUN_STATE_TEARDOWN,
			      "teardown %zu", i);
		zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_FAIL),
			      0, "fail terminal after abort %zu", i);
	}
}

ZTEST(hil_source_control, test_abort_rejection_atomic)
{
	struct hil_source_state st;
	struct hil_source_state snap;

	/* Inactive. */
	hil_source_state_reset(&st);
	snap = st;
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_STOP),
		      -EINVAL, "inactive abort");
	zassert_mem_equal(&st, &snap, sizeof(st), "inactive abort mutated state");

	/* Invalid cause. */
	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_NONE);
	zassert_equal(hil_source_state_start(&st), 0, "start");
	snap = st;
	zassert_equal(hil_source_state_abort_to_teardown(&st,
							(enum hil_source_abort_cause)99),
		      -EINVAL, "invalid cause");
	zassert_mem_equal(&st, &snap, sizeof(st), "invalid cause mutated state");

	/* Repeat abort. */
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_ERROR),
		      0, "first abort");
	snap = st;
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_TIMEOUT),
		      -EINVAL, "repeat abort");
	zassert_mem_equal(&st, &snap, sizeof(st), "repeat abort mutated state");

	/* Abort after ordinary teardown. */
	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	state_submit_scored(&st, 0U, 1U);
	state_finish_segment(&st);
	snap = st;
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_STOP),
		      -EINVAL, "abort after teardown");
	zassert_mem_equal(&st, &snap, sizeof(st), "abort-after-teardown mutated state");
}

ZTEST(hil_source_control, test_abort_blocks_pass_and_reconnect)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_ONCE);
	state_to_streaming(&st);
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_TIMEOUT),
		      0, "abort");
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_PASS),
		      -EINVAL, "pass after abort");
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_FAIL), 0,
		      "fail after abort");
	zassert_false(st.active, "terminal clears active");
}

ZTEST(hil_source_control, test_abort_blocks_reconnect_segment)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_MONO, HIL_SOURCE_PROFILE_48_4_1,
		     1U, HIL_SOURCE_RECONNECT_ONCE);
	state_to_streaming(&st);
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_ERROR),
		      0, "abort");
	zassert_equal(hil_source_state_next_segment(&st), -EINVAL,
		      "reconnect after abort");
}

ZTEST(hil_source_control, test_start_after_abort_clears_snapshot_preserves_config)
{
	struct hil_source_state st;

	hil_source_state_reset(&st);
	state_config(&st, HIL_SOURCE_MODE_A, HIL_SOURCE_PROFILE_48_3_1,
		     2U, HIL_SOURCE_RECONNECT_NONE);
	state_to_streaming(&st);
	zassert_equal(hil_source_state_abort_to_teardown(&st, HIL_SOURCE_ABORT_ERROR),
		      0, "abort");
	zassert_equal(hil_source_state_terminal(&st, HIL_SOURCE_VERDICT_FAIL), 0,
		      "fail terminal");
	zassert_equal(hil_source_state_start(&st), 0, "restart");
	zassert_false(st.aborted, "abort snapshot cleared");
	zassert_true(st.has_config, "configuration preserved");
	zassert_equal(st.config.mode, HIL_SOURCE_MODE_A, "mode preserved");
	zassert_equal(st.config.profile, HIL_SOURCE_PROFILE_48_3_1, "profile preserved");
	zassert_true(st.active, "active again");
}
