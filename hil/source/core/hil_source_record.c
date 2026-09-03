/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * HIL1 record envelope formatting (RH1A).
 *
 * One generic envelope function emits the exact key order of the RH0 wire
 * parser, then validates kind, safe IDs, and the trusted data_json span
 * before formatting.  All failures (invalid input or truncation) leave the
 * caller buffer empty when capacity is nonzero; no partial success is
 * ever reported.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hil_source_record.h"

static bool hil_record_kind_ok(const char *kind)
{
	return strcmp(kind, "ack") == 0 || strcmp(kind, "state") == 0 ||
	       strcmp(kind, "terminal") == 0 || strcmp(kind, "status") == 0;
}

/* Safe identifier: correct length bounds plus the safe character set.
 * The data_json span may itself contain command/run IDs only through the
 * validated parameters, so this check is the single gate. */
static bool hil_record_id_ok(const char *id, size_t max_len)
{
	size_t n;

	if (id == NULL || !hil_source_safe_id_chars_ok(id)) {
		return false;
	}
	n = strlen(id);
	return n >= 1U && n <= max_len;
}

/* The data_json span must be a nonempty {...} object with no CR/LF/NUL. */
static bool hil_record_data_ok(const char *data_json, size_t data_len)
{
	size_t i;

	if (data_json == NULL || data_len < 2U) {
		return false;
	}
	if (data_json[0] != '{' || data_json[data_len - 1U] != '}') {
		return false;
	}
	for (i = 0U; i < data_len; i++) {
		char c = data_json[i];

		if (c == '\r' || c == '\n' || c == '\0') {
			return false;
		}
	}
	return true;
}

static bool hil_record_error_name_ok(const char *name)
{
	static const char *const names[] = {
		HIL_SOURCE_ERROR_OK,
		HIL_SOURCE_ERROR_UNKNOWN_COMMAND,
		HIL_SOURCE_ERROR_INVALID_REQUEST,
		HIL_SOURCE_ERROR_BUSY,
		HIL_SOURCE_ERROR_NOT_CONFIGURED,
		HIL_SOURCE_ERROR_PARSE,
		HIL_SOURCE_ERROR_FAILED,
	};
	size_t i;

	if (name == NULL) {
		return false;
	}
	for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
		if (strcmp(name, names[i]) == 0) {
			return true;
		}
	}
	return false;
}

int hil_source_record_format_line(char *buf, size_t cap, uint32_t monotonic_ms,
				  const char *command_id, const char *run_id, uint32_t segment,
				  const char *kind, const char *data_json, size_t data_len)
{
	int n;

	if (buf == NULL) {
		return -EINVAL;
	}
	if (cap > 0U) {
		buf[0] = '\0';
	}
	if (kind == NULL || !hil_record_kind_ok(kind)) {
		return -EINVAL;
	}
	if (!hil_record_id_ok(command_id, HIL_SOURCE_MAX_COMMAND_ID_LEN) ||
	    !hil_record_id_ok(run_id, HIL_SOURCE_MAX_RUN_ID_LEN)) {
		return -EINVAL;
	}
	if (!hil_record_data_ok(data_json, data_len)) {
		return -EINVAL;
	}

	n = snprintf(buf, cap,
		     "{\"protocol_version\":%u,\"kind\":\"%s\",\"firmware_id\":\"%s\","
		     "\"monotonic_ms\":%u,\"command_id\":\"%s\",\"run_id\":\"%s\","
		     "\"segment\":%u,\"data\":%.*s}",
		     HIL_SOURCE_PROTOCOL_VERSION, kind, HIL_SOURCE_FIRMWARE_ID, monotonic_ms,
		     command_id, run_id, segment, (int)data_len, data_json);
	if (n < 0 || (size_t)n >= cap) {
		if (cap > 0U) {
			buf[0] = '\0';
		}
		return -ENOBUFS;
	}
	return n;
}

/* Build one data_json span locally and delegate to the generic envelope.
 * The spans are compile-generated constants plus canonical enum strings,
 * so no caller-supplied string ever reaches data_json. */
static int hil_record_format_with_data(char *buf, size_t cap, uint32_t monotonic_ms,
				       const char *command_id, const char *run_id, uint32_t segment,
				       const char *kind, const char *data, size_t data_len)
{
	return hil_source_record_format_line(buf, cap, monotonic_ms, command_id, run_id, segment,
					     kind, data, data_len);
}

int hil_source_record_format_ack_start(char *buf, size_t cap, uint32_t monotonic_ms,
				       const char *command_id, const char *run_id, uint32_t segment)
{
	static const char data[] = "{\"command\":\"start\",\"accepted\":true}";

	return hil_source_record_format_line(buf, cap, monotonic_ms, command_id, run_id, segment,
					     "ack", data, sizeof(data) - 1U);
}

int hil_source_record_format_state(char *buf, size_t cap, uint32_t monotonic_ms,
				   const char *command_id, const char *run_id, uint32_t segment,
				   enum hil_source_run_state state)
{
	const char *name = hil_source_state_name(state);
	char data[64];
	int n;

	if (name == NULL) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	n = snprintf(data, sizeof(data), "{\"state\":\"%s\"}", name);
	if (n < 0 || (size_t)n >= sizeof(data)) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	return hil_source_record_format_line(buf, cap, monotonic_ms, command_id, run_id, segment,
					     "state", data, (size_t)n);
}

int hil_source_record_format_terminal(char *buf, size_t cap, uint32_t monotonic_ms,
				      const char *command_id, const char *run_id, uint32_t segment,
				      enum hil_source_verdict verdict)
{
	const char *name = hil_source_verdict_name(verdict);
	char data[32];
	int n;

	if (name == NULL) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	n = snprintf(data, sizeof(data), "{\"verdict\":\"%s\"}", name);
	if (n < 0 || (size_t)n >= sizeof(data)) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	return hil_source_record_format_line(buf, cap, monotonic_ms, command_id, run_id, segment,
					     "terminal", data, (size_t)n);
}

int hil_source_record_format_abort_teardown(char *buf, size_t cap,
					    uint32_t monotonic_ms,
					    const char *command_id,
					    const char *run_id, uint32_t segment,
					    enum hil_source_abort_cause cause)
{
	const char *name = hil_source_abort_cause_name(cause);
	char data[48];
	int n;

	if (name == NULL) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	n = snprintf(data, sizeof(data), "{\"state\":\"teardown\",\"cause\":\"%s\"}",
		     name);
	if (n < 0 || (size_t)n >= sizeof(data)) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	return hil_source_record_format_line(buf, cap, monotonic_ms, command_id,
					     run_id, segment, "state", data,
					     (size_t)n);
}

int hil_source_record_format_status(char *buf, size_t cap, uint32_t monotonic_ms,
				    const char *command_id, const char *run_id, uint32_t segment,
				    enum hil_source_command command, bool ok,
				    const char *error_name)
{
	const char *cmd_name = hil_source_command_name(command);
	char data[96];
	int n;

	if (cmd_name == NULL || !hil_record_error_name_ok(error_name)) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	n = snprintf(data, sizeof(data),
		     "{\"command\":\"%s\",\"ok\":%s,"
		     "\"error\":\"%s\"}",
		     cmd_name, ok ? "true" : "false", error_name);
	if (n < 0 || (size_t)n >= sizeof(data)) {
		if (buf != NULL && cap > 0U) {
			buf[0] = '\0';
		}
		return -EINVAL;
	}
	return hil_record_format_with_data(buf, cap, monotonic_ms, command_id, run_id, segment,
					   "status", data, (size_t)n);
}
