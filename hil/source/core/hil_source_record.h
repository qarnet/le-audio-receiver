/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * HIL1 record envelope formatting (RH1A).
 *
 * Formats one complete HIL1 line (no line terminator) into a caller
 * buffer with the exact key order consumed by the RH0 wire parser
 * (scripts/hil/protocol.py parse_hil1_line()).  All output is emitted
 * through one generic envelope function that takes a trusted,
 * compile-generated `data_json` object span; host input is never passed
 * as `data_json`.
 */

#ifndef HIL_SOURCE_RECORD_H
#define HIL_SOURCE_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hil_source_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stable error names for status/error responses. */
#define HIL_SOURCE_ERROR_OK              "ok"
#define HIL_SOURCE_ERROR_UNKNOWN_COMMAND "unknown_command"
#define HIL_SOURCE_ERROR_INVALID_REQUEST "invalid_request"
#define HIL_SOURCE_ERROR_BUSY            "busy"
#define HIL_SOURCE_ERROR_NOT_CONFIGURED  "not_configured"
#define HIL_SOURCE_ERROR_PARSE           "parse_error"
#define HIL_SOURCE_ERROR_FAILED          "failed"

/* Generic envelope.  `kind` must be exactly "ack", "state", "terminal",
 * or "status"; command/run IDs must be safe identifiers; `data_json` must
 * be a nonempty `{...}` span with no CR/LF/NUL inside.  On success
 * returns the written length (excluding the terminating NUL); on invalid
 * input or truncation returns a negative errno and leaves `buf` an empty
 * C string when `cap` is nonzero.  Never emits partial success. */
int hil_source_record_format_line(char *buf, size_t cap, uint32_t monotonic_ms,
				  const char *command_id, const char *run_id, uint32_t segment,
				  const char *kind, const char *data_json, size_t data_len);

/* Start acknowledgment: data contains command "start" and accepted:true. */
int hil_source_record_format_ack_start(char *buf, size_t cap, uint32_t monotonic_ms,
				       const char *command_id, const char *run_id,
				       uint32_t segment);

/* State record: data contains the canonical state name. */
int hil_source_record_format_state(char *buf, size_t cap, uint32_t monotonic_ms,
				   const char *command_id, const char *run_id, uint32_t segment,
				   enum hil_source_run_state state);

/* Terminal record: data contains verdict "pass" or "fail". */
int hil_source_record_format_terminal(char *buf, size_t cap, uint32_t monotonic_ms,
				      const char *command_id, const char *run_id, uint32_t segment,
				      enum hil_source_verdict verdict);

/* Bounded abort record: a state record whose data is exactly
 * {"state":"teardown","cause":"stop|timeout|error"}. */
int hil_source_record_format_abort_teardown(char *buf, size_t cap, uint32_t monotonic_ms,
					    const char *command_id, const char *run_id,
					    uint32_t segment, enum hil_source_abort_cause cause);

/* Status/error response: data contains the canonical command name, `ok`,
 * and a stable error name.  `error_name` must be one of the
 * HIL_SOURCE_ERROR_* names. */
int hil_source_record_format_status(char *buf, size_t cap, uint32_t monotonic_ms,
				    const char *command_id, const char *run_id, uint32_t segment,
				    enum hil_source_command command, bool ok,
				    const char *error_name);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_RECORD_H */
