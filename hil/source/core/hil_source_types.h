/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared source-core types, constants, and canonical wire strings (RH1A).
 *
 * Plain C99/C11 with no Zephyr dependency: only standard fixed-width types
 * are used.  Every enum below has a companion helper returning the exact
 * canonical wire string used by the HIL1 protocol; unknown enum values
 * return NULL, never an invented fallback.
 */

#ifndef HIL_SOURCE_TYPES_H
#define HIL_SOURCE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── frozen enums ─────────────────────────────────────────────────── */

enum hil_source_command {
	HIL_SOURCE_CMD_HELLO,
	HIL_SOURCE_CMD_IDLE,
	HIL_SOURCE_CMD_UNPAIR,
	HIL_SOURCE_CMD_CONFIGURE,
	HIL_SOURCE_CMD_START,
	HIL_SOURCE_CMD_STOP,
	HIL_SOURCE_CMD_STATUS,
};

enum hil_source_address_type {
	HIL_SOURCE_ADDR_PUBLIC,
	HIL_SOURCE_ADDR_RANDOM,
};

enum hil_source_mode {
	HIL_SOURCE_MODE_MONO,
	HIL_SOURCE_MODE_A,
	HIL_SOURCE_MODE_B,
};

enum hil_source_profile {
	HIL_SOURCE_PROFILE_48_3_1,
	HIL_SOURCE_PROFILE_48_4_1,
};

enum hil_source_reconnect_policy {
	HIL_SOURCE_RECONNECT_NONE,
	HIL_SOURCE_RECONNECT_ONCE,
};

/* Bounded abort-to-teardown causes (RH1A abort edge). */
enum hil_source_abort_cause {
	HIL_SOURCE_ABORT_STOP,
	HIL_SOURCE_ABORT_TIMEOUT,
	HIL_SOURCE_ABORT_ERROR,
};

enum hil_source_semantic_channel {
	HIL_SOURCE_CHANNEL_LEFT,
	HIL_SOURCE_CHANNEL_RIGHT,
};

enum hil_source_signal_stage {
	HIL_SOURCE_SIGNAL_PREAMBLE,
	HIL_SOURCE_SIGNAL_SCORED,
	HIL_SOURCE_SIGNAL_TAIL,
};

/* Run lifecycle states, frozen in RH0 wire order (see
 * scripts/hil/protocol.py STATES). */
enum hil_source_run_state {
	HIL_SOURCE_RUN_STATE_IDLE,
	HIL_SOURCE_RUN_STATE_CONFIGURED,
	HIL_SOURCE_RUN_STATE_CONNECTING,
	HIL_SOURCE_RUN_STATE_SECURED,
	HIL_SOURCE_RUN_STATE_DISCOVERED,
	HIL_SOURCE_RUN_STATE_QOS,
	HIL_SOURCE_RUN_STATE_STREAMING,
	HIL_SOURCE_RUN_STATE_SCORED_COMPLETE,
	HIL_SOURCE_RUN_STATE_TEARDOWN,
};

/* Terminal verdict; NONE means no terminal has been accepted yet. */
enum hil_source_verdict {
	HIL_SOURCE_VERDICT_NONE,
	HIL_SOURCE_VERDICT_PASS,
	HIL_SOURCE_VERDICT_FAIL,
};

/* ── frozen constants ─────────────────────────────────────────────── */

#define HIL_SOURCE_PROTOCOL_VERSION 1U
#define HIL_SOURCE_FIRMWARE_ID      "le-audio-hil-source-rh1"
#define HIL_SOURCE_SAMPLE_RATE_HZ   48000U

#define HIL_SOURCE_LEFT_CARRIER_HZ  997U
#define HIL_SOURCE_LEFT_PHASE_STEP  0x05513CC2UL
#define HIL_SOURCE_RIGHT_CARRIER_HZ 1601U
#define HIL_SOURCE_RIGHT_PHASE_STEP 0x0889E60FUL

#define HIL_SOURCE_AMPLITUDE_FULL 8192
#define HIL_SOURCE_AMPLITUDE_LOW  4096

#define HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES   5760U  /* 120 ms at 48 kHz */
#define HIL_SOURCE_TRANSITION_RAMP_SAMPLES  240U   /* 5 ms at 48 kHz */
#define HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES 11520U /* 240 ms at 48 kHz */
#define HIL_SOURCE_PREAMBLE_SEGMENTS        6U
#define HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES   69120U /* 1.44 s at 48 kHz */

#define HIL_SOURCE_TAIL_MIN_US         5000000U
#define HIL_SOURCE_DEFAULT_SIGNAL_SEED 0x48A31C5DUL
#define HIL_SOURCE_MAX_SCORED_SDUS     20000U

#define HIL_SOURCE_MAX_COMMAND_LINE_BYTES 511U
#define HIL_SOURCE_MAX_COMMAND_ID_LEN     63U
#define HIL_SOURCE_MAX_RUN_ID_LEN         64U

/* xorshift32 state derived from the configured signal seed; a derived
 * zero (seed ^ mask == 0) is replaced with this constant. */
#define HIL_SOURCE_LEFT_PRNG_MASK        0x4C454654UL
#define HIL_SOURCE_RIGHT_PRNG_MASK       0x52494748UL
#define HIL_SOURCE_PRNG_ZERO_REPLACEMENT 0x6D2B79F5UL

/* ── canonical wire strings ───────────────────────────────────────── */

static inline const char *hil_source_command_name(enum hil_source_command cmd)
{
	switch (cmd) {
	case HIL_SOURCE_CMD_HELLO:
		return "hello";
	case HIL_SOURCE_CMD_IDLE:
		return "idle";
	case HIL_SOURCE_CMD_UNPAIR:
		return "unpair";
	case HIL_SOURCE_CMD_CONFIGURE:
		return "configure";
	case HIL_SOURCE_CMD_START:
		return "start";
	case HIL_SOURCE_CMD_STOP:
		return "stop";
	case HIL_SOURCE_CMD_STATUS:
		return "status";
	}
	return NULL;
}

static inline const char *hil_source_mode_name(enum hil_source_mode mode)
{
	switch (mode) {
	case HIL_SOURCE_MODE_MONO:
		return "mono";
	case HIL_SOURCE_MODE_A:
		return "mode_a";
	case HIL_SOURCE_MODE_B:
		return "mode_b";
	}
	return NULL;
}

static inline const char *hil_source_profile_name(enum hil_source_profile profile)
{
	switch (profile) {
	case HIL_SOURCE_PROFILE_48_3_1:
		return "48_3_1";
	case HIL_SOURCE_PROFILE_48_4_1:
		return "48_4_1";
	}
	return NULL;
}

static inline const char *hil_source_reconnect_policy_name(enum hil_source_reconnect_policy policy)
{
	switch (policy) {
	case HIL_SOURCE_RECONNECT_NONE:
		return "none";
	case HIL_SOURCE_RECONNECT_ONCE:
		return "once";
	}
	return NULL;
}

static inline const char *hil_source_state_name(enum hil_source_run_state state)
{
	switch (state) {
	case HIL_SOURCE_RUN_STATE_IDLE:
		return "idle";
	case HIL_SOURCE_RUN_STATE_CONFIGURED:
		return "configured";
	case HIL_SOURCE_RUN_STATE_CONNECTING:
		return "connecting";
	case HIL_SOURCE_RUN_STATE_SECURED:
		return "secured";
	case HIL_SOURCE_RUN_STATE_DISCOVERED:
		return "discovered";
	case HIL_SOURCE_RUN_STATE_QOS:
		return "qos";
	case HIL_SOURCE_RUN_STATE_STREAMING:
		return "streaming";
	case HIL_SOURCE_RUN_STATE_SCORED_COMPLETE:
		return "scored_complete";
	case HIL_SOURCE_RUN_STATE_TEARDOWN:
		return "teardown";
	}
	return NULL;
}

static inline const char *hil_source_verdict_name(enum hil_source_verdict verdict)
{
	switch (verdict) {
	case HIL_SOURCE_VERDICT_PASS:
		return "pass";
	case HIL_SOURCE_VERDICT_FAIL:
		return "fail";
	case HIL_SOURCE_VERDICT_NONE:
		return NULL;
	}
	return NULL;
}

static inline const char *hil_source_abort_cause_name(enum hil_source_abort_cause cause)
{
	switch (cause) {
	case HIL_SOURCE_ABORT_STOP:
		return "stop";
	case HIL_SOURCE_ABORT_TIMEOUT:
		return "timeout";
	case HIL_SOURCE_ABORT_ERROR:
		return "error";
	}
	return NULL;
}

/* Number of logical streams for a mode: mono 1, Mode A 2, Mode B 1.
 * Returns 0 for an invalid mode. */
static inline uint8_t hil_source_mode_stream_count(enum hil_source_mode mode)
{
	switch (mode) {
	case HIL_SOURCE_MODE_MONO:
	case HIL_SOURCE_MODE_B:
		return 1U;
	case HIL_SOURCE_MODE_A:
		return 2U;
	}
	return 0U;
}

/* Safe identifier check shared by the parser and the record formatter:
 * [A-Za-z0-9][A-Za-z0-9._-]{0,max_len-1}.  Length bounds are enforced
 * separately by callers; this helper only validates the character set. */
static inline bool hil_source_safe_id_chars_ok(const char *id)
{
	size_t i;

	if (id == NULL || id[0] == '\0') {
		return false;
	}
	if (!((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= 'A' && id[0] <= 'Z') ||
	      (id[0] >= '0' && id[0] <= '9'))) {
		return false;
	}
	for (i = 1U; id[i] != '\0'; i++) {
		char c = id[i];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		      c == '.' || c == '_' || c == '-')) {
			return false;
		}
	}
	return true;
}

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_TYPES_H */
