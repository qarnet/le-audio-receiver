/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Strict bounded `hil <raw JSON>` inbound command parser (RH1A).
 *
 * Implements a bounded flat-object parser supporting JSON whitespace,
 * string values, and unsigned canonical decimal integers.  No dynamic
 * allocation, no input mutation, and on failure the caller output struct
 * stays byte-for-byte unchanged: every check runs against a zero-initialized
 * local candidate and the output is assigned exactly once at the end.
 *
 * The parser context is stack-bounded (a compile-time assertion keeps it
 * under the documented 2 KiB budget) so the same module is safe to call
 * from the target shell context.
 *
 * Error class ordering (documented for the native suite):
 *   1. line length > HIL_SOURCE_MAX_COMMAND_LINE_BYTES      -> TOO_LONG
 *   2. lexical scan: malformed/truncated input, bad escapes,
 *      raw control characters, duplicate known keys, non-canonical
 *      integer syntax, overlong strings, >64-bit integer
 *      accumulation, malformed nested JSON, nesting-depth overflow
 *      -> SYNTAX / DUPLICATE_KEY / RANGE
 *   3. trailing non-whitespace after the object               -> SYNTAX
 *   4. command missing / wrong type / not canonical           -> MISSING_KEY /
 *      WRONG_TYPE / UNSUPPORTED_VALUE
 *   5. unknown vocabulary key or key not allowed for the
 *      selected command (extra key)                           -> UNKNOWN_KEY
 *   6. missing common or command-specific key                 -> MISSING_KEY
 *   7. per-key value checks (type, length, canonical value,
 *      configured integer range)                              -> WRONG_TYPE /
 *      RANGE / UNSUPPORTED_VALUE
 */

#include <stdint.h>
#include <string.h>

#include "hil_source_protocol.h"

/* Stack budget for the whole parser context (documented, not exposed as a
 * public implementation-size API). */
#define HIL_SOURCE_PARSER_STACK_BUDGET 2048U

/* Maximum semantic decoded string length in bytes (run id: 64). */
#define HIL_SOURCE_MAX_STRING_BYTES 64U
#define HIL_SOURCE_STRING_BUFFER    (HIL_SOURCE_MAX_STRING_BYTES + 2U)

/* Maximum key length in bytes ("scored_sdu_count": 16). */
#define HIL_SOURCE_MAX_KEY_BYTES 16U
#define HIL_SOURCE_KEY_BUFFER    (HIL_SOURCE_MAX_KEY_BYTES + 2U)

/* Maximum nesting depth accepted inside rejected nested object/array
 * values; deeper input fails as SYNTAX. */
#define HIL_JSON_MAX_DEPTH 32U

/* ── keys ─────────────────────────────────────────────────────────── */

enum hil_source_key_id {
	KEY_PROTOCOL_VERSION,
	KEY_COMMAND,
	KEY_COMMAND_ID,
	KEY_RUN_ID,
	KEY_PEER_ADDRESS,
	KEY_PEER_ADDRESS_TYPE,
	KEY_MODE,
	KEY_PROFILE,
	KEY_SCORED_SDU_COUNT,
	KEY_SIGNAL_SEED,
	KEY_RECONNECT_POLICY,
	KEY__COUNT,
};

#define KEY_PROTOCOL_VERSION_BIT  (1UL << KEY_PROTOCOL_VERSION)
#define KEY_COMMAND_BIT           (1UL << KEY_COMMAND)
#define KEY_COMMAND_ID_BIT        (1UL << KEY_COMMAND_ID)
#define KEY_RUN_ID_BIT            (1UL << KEY_RUN_ID)
#define KEY_PEER_ADDRESS_BIT      (1UL << KEY_PEER_ADDRESS)
#define KEY_PEER_ADDRESS_TYPE_BIT (1UL << KEY_PEER_ADDRESS_TYPE)
#define KEY_MODE_BIT              (1UL << KEY_MODE)
#define KEY_PROFILE_BIT           (1UL << KEY_PROFILE)
#define KEY_SCORED_SDU_COUNT_BIT  (1UL << KEY_SCORED_SDU_COUNT)
#define KEY_SIGNAL_SEED_BIT       (1UL << KEY_SIGNAL_SEED)
#define KEY_RECONNECT_POLICY_BIT  (1UL << KEY_RECONNECT_POLICY)

#define KEY_COMMON_MASK                                                                            \
	(KEY_PROTOCOL_VERSION_BIT | KEY_COMMAND_BIT | KEY_COMMAND_ID_BIT | KEY_RUN_ID_BIT)
#define KEY_UNPAIR_MASK (KEY_COMMON_MASK | KEY_PEER_ADDRESS_BIT | KEY_PEER_ADDRESS_TYPE_BIT)
#define KEY_CONFIGURE_MASK                                                                         \
	(KEY_UNPAIR_MASK | KEY_MODE_BIT | KEY_PROFILE_BIT | KEY_SCORED_SDU_COUNT_BIT |             \
	 KEY_SIGNAL_SEED_BIT | KEY_RECONNECT_POLICY_BIT)

static const char *const hil_key_names[KEY__COUNT] = {
	"protocol_version",  "command", "command_id", "run_id",           "peer_address",
	"peer_address_type", "mode",    "profile",    "scored_sdu_count", "signal_seed",
	"reconnect_policy",
};

/* ── value kinds ──────────────────────────────────────────────────── */

enum hil_value_kind {
	VAL_STRING,
	VAL_UINT,
	VAL_OBJECT,
	VAL_ARRAY,
	VAL_BOOL,
	VAL_NULL,
};

struct hil_value {
	enum hil_value_kind kind;
	char str[HIL_SOURCE_STRING_BUFFER]; /* decoded top-level string */
	size_t str_len;
	uint64_t uval; /* VAL_UINT only */
	bool present;
};

struct hil_parse_ctx {
	const char *buf;
	size_t len;
	size_t pos;
	enum hil_source_parse_result result;
	bool failed;
	struct hil_value pending[KEY__COUNT];
	uint32_t seen;
	bool unknown_key_seen;
};

_Static_assert(sizeof(struct hil_parse_ctx) <= HIL_SOURCE_PARSER_STACK_BUDGET,
	       "hil parser context exceeds the documented stack budget");

/* ── lexer primitives ─────────────────────────────────────────────── */

static bool hil_is_ws(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool hil_is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static void hil_fail(struct hil_parse_ctx *ctx, enum hil_source_parse_result r)
{
	if (!ctx->failed) {
		ctx->result = r;
		ctx->failed = true;
	}
}

static void hil_skip_ws(struct hil_parse_ctx *ctx)
{
	while (ctx->pos < ctx->len && hil_is_ws(ctx->buf[ctx->pos])) {
		ctx->pos++;
	}
}

/* Decode one JSON string into `out` (cap includes room for the
 * terminator).  Overlong decoded values are still consumed and lexically
 * validated; the overlong condition is reported through `*overlong`
 * (when non-NULL) so callers can classify it (range for known values,
 * unknown-key for keys that cannot match any known name).  Returns 0 or
 * -1 after setting the ctx result. */
static int hil_lex_string_bounded(struct hil_parse_ctx *ctx, char *out, size_t cap,
				  size_t *out_len, bool *overlong)
{
	size_t n = 0U;
	bool too_long = false;

	if (ctx->pos >= ctx->len || ctx->buf[ctx->pos] != '"') {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	ctx->pos++;
	while (ctx->pos < ctx->len) {
		char c = ctx->buf[ctx->pos];

		if ((unsigned char)c < 0x20U) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		if (c == '"') {
			ctx->pos++;
			*out_len = n;
			if (overlong != NULL) {
				*overlong = too_long;
			}
			return 0;
		}
		if (c == '\\') {
			char e;

			ctx->pos++;
			if (ctx->pos >= ctx->len) {
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			}
			e = ctx->buf[ctx->pos];
			switch (e) {
			case '"':
				c = '"';
				break;
			case '\\':
				c = '\\';
				break;
			case '/':
				c = '/';
				break;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			case 'u':
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			default:
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			}
		}
		if (n + 1U >= cap) {
			too_long = true;
		} else {
			out[n++] = c;
		}
		ctx->pos++;
	}
	hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX); /* unterminated string */
	return -1;
}

/* Lexically validate and consume one JSON string with no output.  Used to
 * validate keys and values inside rejected nested objects/arrays. */
static int hil_lex_string_skip(struct hil_parse_ctx *ctx)
{
	if (ctx->pos >= ctx->len || ctx->buf[ctx->pos] != '"') {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	ctx->pos++;
	while (ctx->pos < ctx->len) {
		char c = ctx->buf[ctx->pos];

		if ((unsigned char)c < 0x20U) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		if (c == '"') {
			ctx->pos++;
			return 0;
		}
		if (c == '\\') {
			ctx->pos++;
			if (ctx->pos >= ctx->len) {
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			}
			switch (ctx->buf[ctx->pos]) {
			case '"':
			case '\\':
			case '/':
			case 'b':
			case 'f':
			case 'n':
			case 'r':
			case 't':
				break;
			case 'u':
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			default:
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			}
		}
		ctx->pos++;
	}
	hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
	return -1;
}

/* Lexically validate and consume one JSON number (full JSON number
 * grammar, including sign, fraction, and exponent).  Only used for
 * values inside rejected nested objects/arrays. */
static int hil_lex_number_skip(struct hil_parse_ctx *ctx)
{
	if (ctx->pos < ctx->len && ctx->buf[ctx->pos] == '-') {
		ctx->pos++;
	}
	if (ctx->pos >= ctx->len) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	if (ctx->buf[ctx->pos] == '0') {
		ctx->pos++;
	} else if (ctx->buf[ctx->pos] >= '1' && ctx->buf[ctx->pos] <= '9') {
		while (ctx->pos < ctx->len && hil_is_digit(ctx->buf[ctx->pos])) {
			ctx->pos++;
		}
	} else {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	if (ctx->pos < ctx->len && ctx->buf[ctx->pos] == '.') {
		ctx->pos++;
		if (ctx->pos >= ctx->len || !hil_is_digit(ctx->buf[ctx->pos])) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		while (ctx->pos < ctx->len && hil_is_digit(ctx->buf[ctx->pos])) {
			ctx->pos++;
		}
	}
	if (ctx->pos < ctx->len && (ctx->buf[ctx->pos] == 'e' || ctx->buf[ctx->pos] == 'E')) {
		ctx->pos++;
		if (ctx->pos < ctx->len &&
		    (ctx->buf[ctx->pos] == '+' || ctx->buf[ctx->pos] == '-')) {
			ctx->pos++;
		}
		if (ctx->pos >= ctx->len || !hil_is_digit(ctx->buf[ctx->pos])) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		while (ctx->pos < ctx->len && hil_is_digit(ctx->buf[ctx->pos])) {
			ctx->pos++;
		}
	}
	return 0;
}

/* ── nested JSON grammar validation ───────────────────────────────── */

static int hil_validate_value(struct hil_parse_ctx *ctx, unsigned depth);
static int hil_validate_object(struct hil_parse_ctx *ctx, unsigned depth);
static int hil_validate_array(struct hil_parse_ctx *ctx, unsigned depth);

static int hil_validate_literal(struct hil_parse_ctx *ctx, const char *lit, size_t lit_len)
{
	if (ctx->pos + lit_len > ctx->len || memcmp(ctx->buf + ctx->pos, lit, lit_len) != 0) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	ctx->pos += lit_len;
	return 0;
}

static int hil_validate_value(struct hil_parse_ctx *ctx, unsigned depth)
{
	char c;

	if (ctx->pos >= ctx->len) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	if (depth > HIL_JSON_MAX_DEPTH) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	c = ctx->buf[ctx->pos];
	if (c == '"') {
		return hil_lex_string_skip(ctx);
	}
	if (c == '{') {
		return hil_validate_object(ctx, depth + 1U);
	}
	if (c == '[') {
		return hil_validate_array(ctx, depth + 1U);
	}
	if (c == 't') {
		return hil_validate_literal(ctx, "true", 4U);
	}
	if (c == 'f') {
		return hil_validate_literal(ctx, "false", 5U);
	}
	if (c == 'n') {
		return hil_validate_literal(ctx, "null", 4U);
	}
	if (c == '-' || hil_is_digit(c)) {
		return hil_lex_number_skip(ctx);
	}
	hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
	return -1;
}

static int hil_validate_object(struct hil_parse_ctx *ctx, unsigned depth)
{
	ctx->pos++; /* '{' */
	hil_skip_ws(ctx);
	if (ctx->pos >= ctx->len) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	if (ctx->buf[ctx->pos] == '}') {
		ctx->pos++;
		return 0;
	}
	for (;;) {
		if (ctx->pos >= ctx->len) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		if (hil_lex_string_skip(ctx) != 0) {
			return -1;
		}
		hil_skip_ws(ctx);
		if (ctx->pos >= ctx->len || ctx->buf[ctx->pos] != ':') {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		ctx->pos++;
		hil_skip_ws(ctx);
		if (hil_validate_value(ctx, depth) != 0) {
			return -1;
		}
		hil_skip_ws(ctx);
		if (ctx->pos >= ctx->len) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		if (ctx->buf[ctx->pos] == ',') {
			ctx->pos++;
			hil_skip_ws(ctx);
			if (ctx->pos >= ctx->len || ctx->buf[ctx->pos] == '}') {
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1; /* trailing comma */
			}
			continue;
		}
		if (ctx->buf[ctx->pos] == '}') {
			ctx->pos++;
			return 0;
		}
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
}

static int hil_validate_array(struct hil_parse_ctx *ctx, unsigned depth)
{
	ctx->pos++; /* '[' */
	hil_skip_ws(ctx);
	if (ctx->pos >= ctx->len) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	if (ctx->buf[ctx->pos] == ']') {
		ctx->pos++;
		return 0;
	}
	for (;;) {
		if (hil_validate_value(ctx, depth) != 0) {
			return -1;
		}
		hil_skip_ws(ctx);
		if (ctx->pos >= ctx->len) {
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		if (ctx->buf[ctx->pos] == ',') {
			ctx->pos++;
			hil_skip_ws(ctx);
			if (ctx->pos >= ctx->len || ctx->buf[ctx->pos] == ']') {
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1; /* trailing comma */
			}
			continue;
		}
		if (ctx->buf[ctx->pos] == ']') {
			ctx->pos++;
			return 0;
		}
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
}

/* ── top-level value lexing ───────────────────────────────────────── */

static int hil_parse_value(struct hil_parse_ctx *ctx, struct hil_value *v)
{
	char c;

	if (ctx->pos >= ctx->len) {
		hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
		return -1;
	}
	c = ctx->buf[ctx->pos];

	if (c == '"') {
		bool overlong = false;

		if (hil_lex_string_bounded(ctx, v->str, sizeof(v->str), &v->str_len,
					   &overlong) != 0) {
			return -1;
		}
		if (overlong) {
			hil_fail(ctx, HIL_SOURCE_PARSE_RANGE);
			return -1;
		}
		v->kind = VAL_STRING;
		return 0;
	}
	if (c == '{' || c == '[') {
		/* Validate the nested JSON grammar fully; the value itself
		 * is still rejected later as WRONG_TYPE.  Depth starts at
		 * zero so HIL_JSON_MAX_DEPTH is the exact maximum number
		 * of nested containers. */
		if (hil_validate_value(ctx, 0U) != 0) {
			return -1;
		}
		v->kind = (c == '{') ? VAL_OBJECT : VAL_ARRAY;
		return 0;
	}
	if (c == 't' || c == 'f' || c == 'n') {
		if (c == 't') {
			if (hil_validate_literal(ctx, "true", 4U) != 0) {
				return -1;
			}
		} else if (c == 'f') {
			if (hil_validate_literal(ctx, "false", 5U) != 0) {
				return -1;
			}
		} else {
			if (hil_validate_literal(ctx, "null", 4U) != 0) {
				return -1;
			}
		}
		v->kind = (c == 'n') ? VAL_NULL : VAL_BOOL;
		return 0;
	}
	if (hil_is_digit(c)) {
		uint64_t acc = 0U;

		if (c == '0' && ctx->pos + 1U < ctx->len && hil_is_digit(ctx->buf[ctx->pos + 1U])) {
			/* Leading-zero integer (except the single "0"). */
			hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
			return -1;
		}
		while (ctx->pos < ctx->len && hil_is_digit(ctx->buf[ctx->pos])) {
			uint64_t digit = (uint64_t)(ctx->buf[ctx->pos] - '0');

			if (acc > (UINT64_MAX - digit) / 10U) {
				hil_fail(ctx, HIL_SOURCE_PARSE_RANGE);
				return -1;
			}
			acc = acc * 10U + digit;
			ctx->pos++;
		}
		if (ctx->pos < ctx->len) {
			char after = ctx->buf[ctx->pos];

			if (after != ',' && after != '}' && !hil_is_ws(after)) {
				hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
				return -1;
			}
		}
		v->kind = VAL_UINT;
		v->uval = acc;
		return 0;
	}
	/* Any other token (including '-', '+', '.', 'e', raw control
	 * characters) is not a canonical unsigned decimal integer. */
	hil_fail(ctx, HIL_SOURCE_PARSE_SYNTAX);
	return -1;
}

/* ── semantic helpers ─────────────────────────────────────────────── */

static bool hil_streq(const char *a, size_t a_len, const char *b)
{
	return strlen(b) == a_len && memcmp(a, b, a_len) == 0;
}

static enum hil_source_command hil_command_from_string(const char *s, size_t n)
{
	if (hil_streq(s, n, "hello")) {
		return HIL_SOURCE_CMD_HELLO;
	}
	if (hil_streq(s, n, "idle")) {
		return HIL_SOURCE_CMD_IDLE;
	}
	if (hil_streq(s, n, "unpair")) {
		return HIL_SOURCE_CMD_UNPAIR;
	}
	if (hil_streq(s, n, "configure")) {
		return HIL_SOURCE_CMD_CONFIGURE;
	}
	if (hil_streq(s, n, "start")) {
		return HIL_SOURCE_CMD_START;
	}
	if (hil_streq(s, n, "stop")) {
		return HIL_SOURCE_CMD_STOP;
	}
	if (hil_streq(s, n, "status")) {
		return HIL_SOURCE_CMD_STATUS;
	}
	return (enum hil_source_command) - 1;
}

/* Canonical uppercase XX:XX:XX:XX:XX:XX MAC parse; returns -1 on any
 * deviation, 0 on success with bytes filled. */
static int hil_parse_peer_address(const char *s, size_t n, uint8_t out[6])
{
	size_t i;

	if (n != 17U) {
		return -1;
	}
	for (i = 0U; i < 6U; i++) {
		size_t off = i * 3U;
		unsigned int byte = 0U;
		size_t j;

		for (j = 0U; j < 2U; j++) {
			char c = s[off + j];
			unsigned int nibble;

			if (c >= '0' && c <= '9') {
				nibble = (unsigned int)(c - '0');
			} else if (c >= 'A' && c <= 'F') {
				nibble = (unsigned int)(c - 'A') + 10U;
			} else {
				return -1;
			}
			byte = byte * 16U + nibble;
		}
		if (i < 5U && s[off + 2U] != ':') {
			return -1;
		}
		out[i] = (uint8_t)byte;
	}
	return 0;
}

/* ── public API ───────────────────────────────────────────────────── */

const char *hil_source_parse_result_name(enum hil_source_parse_result result)
{
	switch (result) {
	case HIL_SOURCE_PARSE_OK:
		return "ok";
	case HIL_SOURCE_PARSE_TOO_LONG:
		return "too_long";
	case HIL_SOURCE_PARSE_SYNTAX:
		return "syntax";
	case HIL_SOURCE_PARSE_UNKNOWN_KEY:
		return "unknown_key";
	case HIL_SOURCE_PARSE_DUPLICATE_KEY:
		return "duplicate_key";
	case HIL_SOURCE_PARSE_MISSING_KEY:
		return "missing_key";
	case HIL_SOURCE_PARSE_WRONG_TYPE:
		return "wrong_type";
	case HIL_SOURCE_PARSE_RANGE:
		return "range";
	case HIL_SOURCE_PARSE_UNSUPPORTED_VALUE:
		return "unsupported_value";
	}
	return NULL;
}

enum hil_source_parse_result hil_source_parse(const char *buf, size_t len,
					      struct hil_source_command_in *out)
{
	struct hil_parse_ctx ctx;
	struct hil_source_command_in candidate;
	enum hil_source_command command;
	uint32_t command_mask;
	int i;

	if (out == NULL) {
		return HIL_SOURCE_PARSE_SYNTAX;
	}
	if (buf == NULL) {
		return HIL_SOURCE_PARSE_SYNTAX;
	}
	if (len > HIL_SOURCE_MAX_COMMAND_LINE_BYTES) {
		return HIL_SOURCE_PARSE_TOO_LONG;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.buf = buf;
	ctx.len = len;

	/* ── object scan ── */
	hil_skip_ws(&ctx);
	if (ctx.pos >= ctx.len || ctx.buf[ctx.pos] != '{') {
		return HIL_SOURCE_PARSE_SYNTAX; /* non-object or empty input */
	}
	ctx.pos++;
	for (;;) {
		char key_scratch[HIL_SOURCE_KEY_BUFFER];
		size_t key_len;
		int key_id;

		hil_skip_ws(&ctx);
		if (ctx.pos >= ctx.len) {
			return HIL_SOURCE_PARSE_SYNTAX;
		}
		if (ctx.buf[ctx.pos] == '}') {
			ctx.pos++;
			break;
		}
		{
			bool key_overlong = false;

			if (hil_lex_string_bounded(&ctx, key_scratch, sizeof(key_scratch),
						   &key_len, &key_overlong) != 0) {
				return ctx.result;
			}
			/* A key longer than any known name (16 decoded
			 * bytes) cannot match, so it is unknown regardless
			 * of its decoded prefix. */
			if (key_overlong) {
				ctx.unknown_key_seen = true;
				key_id = -1;
			} else {
				key_id = -1;
				for (i = 0; i < KEY__COUNT; i++) {
					if (hil_streq(key_scratch, key_len,
						      hil_key_names[i])) {
						key_id = i;
						break;
					}
				}
			}
		}
		hil_skip_ws(&ctx);
		if (ctx.pos >= ctx.len || ctx.buf[ctx.pos] != ':') {
			return HIL_SOURCE_PARSE_SYNTAX;
		}
		ctx.pos++;
		hil_skip_ws(&ctx);

		if (key_id < 0) {
			ctx.unknown_key_seen = true;
		} else if (ctx.pending[key_id].present) {
			return HIL_SOURCE_PARSE_DUPLICATE_KEY;
		} else {
			ctx.pending[key_id].present = true;
			ctx.seen |= (1UL << key_id);
		}

		if (key_id < 0) {
			/* Unknown-key values are discard-only: fully
			 * lexically validated, never schema-classified. */
			if (hil_validate_value(&ctx, 0U) != 0) {
				return ctx.result;
			}
		} else if (hil_parse_value(&ctx, &ctx.pending[key_id]) != 0) {
			return ctx.result;
		}

		hil_skip_ws(&ctx);
		if (ctx.pos >= ctx.len) {
			return HIL_SOURCE_PARSE_SYNTAX;
		}
		if (ctx.buf[ctx.pos] == ',') {
			ctx.pos++;
			hil_skip_ws(&ctx);
			/* A trailing comma before the closing brace is not
			 * strict JSON. */
			if (ctx.pos >= ctx.len || ctx.buf[ctx.pos] == '}') {
				return HIL_SOURCE_PARSE_SYNTAX;
			}
			continue;
		}
		if (ctx.buf[ctx.pos] == '}') {
			ctx.pos++;
			break;
		}
		return HIL_SOURCE_PARSE_SYNTAX;
	}

	/* ── trailing non-whitespace ── */
	hil_skip_ws(&ctx);
	if (ctx.pos != ctx.len) {
		return HIL_SOURCE_PARSE_SYNTAX;
	}

	/* ── command selection ── */
	if (!(ctx.seen & KEY_COMMAND_BIT)) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if (ctx.pending[KEY_COMMAND].kind != VAL_STRING) {
		return HIL_SOURCE_PARSE_WRONG_TYPE;
	}
	command = hil_command_from_string(ctx.pending[KEY_COMMAND].str,
					  ctx.pending[KEY_COMMAND].str_len);
	if ((int)command < 0) {
		return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
	}

	/* ── key set for the selected command ── */
	switch (command) {
	case HIL_SOURCE_CMD_UNPAIR:
		command_mask = KEY_UNPAIR_MASK;
		break;
	case HIL_SOURCE_CMD_CONFIGURE:
		command_mask = KEY_CONFIGURE_MASK;
		break;
	default:
		command_mask = KEY_COMMON_MASK;
		break;
	}

	if (ctx.unknown_key_seen) {
		return HIL_SOURCE_PARSE_UNKNOWN_KEY;
	}
	if (ctx.seen & ~command_mask) {
		return HIL_SOURCE_PARSE_UNKNOWN_KEY; /* extra key */
	}

	/* ── missing keys ── */
	if ((ctx.seen & KEY_PROTOCOL_VERSION_BIT) == 0U || (ctx.seen & KEY_COMMAND_ID_BIT) == 0U ||
	    (ctx.seen & KEY_RUN_ID_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_PEER_ADDRESS_BIT) && (ctx.seen & KEY_PEER_ADDRESS_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_PEER_ADDRESS_TYPE_BIT) &&
	    (ctx.seen & KEY_PEER_ADDRESS_TYPE_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_MODE_BIT) && (ctx.seen & KEY_MODE_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_PROFILE_BIT) && (ctx.seen & KEY_PROFILE_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_SCORED_SDU_COUNT_BIT) &&
	    (ctx.seen & KEY_SCORED_SDU_COUNT_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_SIGNAL_SEED_BIT) && (ctx.seen & KEY_SIGNAL_SEED_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}
	if ((command_mask & KEY_RECONNECT_POLICY_BIT) &&
	    (ctx.seen & KEY_RECONNECT_POLICY_BIT) == 0U) {
		return HIL_SOURCE_PARSE_MISSING_KEY;
	}

	/* ── per-key value checks (all against local candidate) ── */
	{
		const struct hil_value *v = &ctx.pending[KEY_PROTOCOL_VERSION];

		if (v->kind != VAL_UINT) {
			return HIL_SOURCE_PARSE_WRONG_TYPE;
		}
		if (v->uval != HIL_SOURCE_PROTOCOL_VERSION) {
			return HIL_SOURCE_PARSE_RANGE;
		}
	}
	{
		struct hil_value *v = &ctx.pending[KEY_COMMAND_ID];

		if (v->kind != VAL_STRING) {
			return HIL_SOURCE_PARSE_WRONG_TYPE;
		}
		if (v->str_len == 0U || v->str_len > HIL_SOURCE_MAX_COMMAND_ID_LEN) {
			return HIL_SOURCE_PARSE_RANGE;
		}
		v->str[v->str_len] = '\0';
		if (!hil_source_safe_id_chars_ok(v->str)) {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
	}
	{
		struct hil_value *v = &ctx.pending[KEY_RUN_ID];

		if (v->kind != VAL_STRING) {
			return HIL_SOURCE_PARSE_WRONG_TYPE;
		}
		if (v->str_len == 0U || v->str_len > HIL_SOURCE_MAX_RUN_ID_LEN) {
			return HIL_SOURCE_PARSE_RANGE;
		}
		v->str[v->str_len] = '\0';
		if (!hil_source_safe_id_chars_ok(v->str)) {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
	}

	/* ── peer/config validation into the candidate ── */
	memset(&candidate, 0, sizeof(candidate));
	if (command == HIL_SOURCE_CMD_UNPAIR || command == HIL_SOURCE_CMD_CONFIGURE) {
		const struct hil_value *addr = &ctx.pending[KEY_PEER_ADDRESS];
		const struct hil_value *atype = &ctx.pending[KEY_PEER_ADDRESS_TYPE];

		if (addr->kind != VAL_STRING || atype->kind != VAL_STRING) {
			return HIL_SOURCE_PARSE_WRONG_TYPE;
		}
		if (hil_parse_peer_address(addr->str, addr->str_len, candidate.peer_address) != 0) {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
		if (hil_streq(atype->str, atype->str_len, "public")) {
			candidate.peer_address_type = HIL_SOURCE_ADDR_PUBLIC;
		} else if (hil_streq(atype->str, atype->str_len, "random")) {
			candidate.peer_address_type = HIL_SOURCE_ADDR_RANDOM;
		} else {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
	}

	if (command == HIL_SOURCE_CMD_CONFIGURE) {
		const struct hil_value *mode = &ctx.pending[KEY_MODE];
		const struct hil_value *profile = &ctx.pending[KEY_PROFILE];
		const struct hil_value *count = &ctx.pending[KEY_SCORED_SDU_COUNT];
		const struct hil_value *seed = &ctx.pending[KEY_SIGNAL_SEED];
		const struct hil_value *reconnect = &ctx.pending[KEY_RECONNECT_POLICY];

		if (mode->kind != VAL_STRING || profile->kind != VAL_STRING ||
		    count->kind != VAL_UINT || seed->kind != VAL_UINT ||
		    reconnect->kind != VAL_STRING) {
			return HIL_SOURCE_PARSE_WRONG_TYPE;
		}
		if (hil_streq(mode->str, mode->str_len, "mono")) {
			candidate.mode = HIL_SOURCE_MODE_MONO;
		} else if (hil_streq(mode->str, mode->str_len, "mode_a")) {
			candidate.mode = HIL_SOURCE_MODE_A;
		} else if (hil_streq(mode->str, mode->str_len, "mode_b")) {
			candidate.mode = HIL_SOURCE_MODE_B;
		} else {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
		if (hil_streq(profile->str, profile->str_len, "48_3_1")) {
			candidate.profile = HIL_SOURCE_PROFILE_48_3_1;
		} else if (hil_streq(profile->str, profile->str_len, "48_4_1")) {
			candidate.profile = HIL_SOURCE_PROFILE_48_4_1;
		} else {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
		if (count->uval < 1U || count->uval > HIL_SOURCE_MAX_SCORED_SDUS) {
			return HIL_SOURCE_PARSE_RANGE;
		}
		candidate.scored_sdu_count = (uint32_t)count->uval;
		if (seed->uval < 1U || seed->uval > UINT32_MAX) {
			return HIL_SOURCE_PARSE_RANGE;
		}
		candidate.signal_seed = (uint32_t)seed->uval;
		if (hil_streq(reconnect->str, reconnect->str_len, "none")) {
			candidate.reconnect_policy = HIL_SOURCE_RECONNECT_NONE;
		} else if (hil_streq(reconnect->str, reconnect->str_len, "once")) {
			candidate.reconnect_policy = HIL_SOURCE_RECONNECT_ONCE;
		} else {
			return HIL_SOURCE_PARSE_UNSUPPORTED_VALUE;
		}
	}

	/* ── commit exactly once ── */
	candidate.command = command;
	memcpy(candidate.command_id, ctx.pending[KEY_COMMAND_ID].str,
	       ctx.pending[KEY_COMMAND_ID].str_len + 1U);
	memcpy(candidate.run_id, ctx.pending[KEY_RUN_ID].str, ctx.pending[KEY_RUN_ID].str_len + 1U);
	*out = candidate;

	return HIL_SOURCE_PARSE_OK;
}
