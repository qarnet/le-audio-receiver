/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Strict bounded `hil <raw JSON>` inbound command parser (RH1A).
 *
 * Parses the supplied byte span without dynamic allocation or input
 * mutation into a caller-owned `struct hil_source_command`.  On failure
 * the output struct remains byte-for-byte unchanged.  Zephyr JSON and
 * cJSON are intentionally not used: this parser is strict about unknown,
 * duplicate, missing, and extra keys, scalar types, canonical values,
 * integer syntax, and length bounds.
 */

#ifndef HIL_SOURCE_PROTOCOL_H
#define HIL_SOURCE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "hil_source_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── parse result ─────────────────────────────────────────────────── */

enum hil_source_parse_result {
	HIL_SOURCE_PARSE_OK,
	HIL_SOURCE_PARSE_TOO_LONG,    /* line exceeds 511 bytes */
	HIL_SOURCE_PARSE_SYNTAX,      /* malformed/truncated JSON, bad
				       * escapes, control chars, trailing
				       * content, non-canonical integer */
	HIL_SOURCE_PARSE_UNKNOWN_KEY, /* key outside selected command */
	HIL_SOURCE_PARSE_DUPLICATE_KEY,
	HIL_SOURCE_PARSE_MISSING_KEY,
	HIL_SOURCE_PARSE_WRONG_TYPE,        /* bool/null/array/object or
					     * string/int mismatch */
	HIL_SOURCE_PARSE_RANGE,             /* overflow, length, or configured
					     * value range violation */
	HIL_SOURCE_PARSE_UNSUPPORTED_VALUE, /* decoded value outside the
					     * canonical semantic grammar */
};

/* Stable parse-result wire names; NULL for an unknown enum value. */
const char *hil_source_parse_result_name(enum hil_source_parse_result result);

/* ── parsed command ───────────────────────────────────────────────── */

struct hil_source_command_in {
	enum hil_source_command command;
	char command_id[HIL_SOURCE_MAX_COMMAND_ID_LEN + 1U]; /* NUL-terminated */
	char run_id[HIL_SOURCE_MAX_RUN_ID_LEN + 1U];         /* NUL-terminated */
	uint8_t peer_address[6];
	enum hil_source_address_type peer_address_type;
	enum hil_source_mode mode;
	enum hil_source_profile profile;
	uint32_t scored_sdu_count;
	uint32_t signal_seed;
	enum hil_source_reconnect_policy reconnect_policy;
};

/* Parse one command line body (the JSON object span, exactly as the shell
 * supplies it after `hil `).  `len` is the byte length; a length above
 * HIL_SOURCE_MAX_COMMAND_LINE_BYTES fails with HIL_SOURCE_PARSE_TOO_LONG.
 * Returns HIL_SOURCE_PARSE_OK and fills `*out` only when every check
 * passes; otherwise `*out` is untouched. */
enum hil_source_parse_result hil_source_parse(const char *buf, size_t len,
					      struct hil_source_command_in *out);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_PROTOCOL_H */
