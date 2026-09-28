#!/usr/bin/env python3
"""Generate audited NCS v3.4.1 UARTE bounce and boundary compatibility source."""

import argparse
import hashlib
from pathlib import Path

AUDITED_SHA256 = "d68f45fbef9da8077efe6c9f94c609393fc3485bd1d486e4f710288f6d808bd3"
START = b"static void prepare_bounce_buf(const struct device *dev, uint8_t *buf,\n"
END = b"\n/* This function is responsible for swapping the bounce buffer"
OLD_BODY = (
    b"\tbuf[0] = UARTE_MAGIC_BYTE;\n"
    b"\tfor (size_t i = swap_len; i < len; i++) {\n"
    b"\t\tbuf[i] = UARTE_MAGIC_BYTE;\n"
    b"\t}\n\n"
    b"\tif (IS_ENABLED(UARTE_ANY_CACHE) && (cfg->flags & UARTE_CFG_FLAG_CACHEABLE)) {\n"
    b"\t\tsys_cache_data_flush_range(buf, 1);\n"
    b"\t\tsys_cache_data_flush_range(&buf[swap_len], len);\n"
    b"\t}\n"
)
NEW_BODY = (
    b"\tARG_UNUSED(swap_len);\n"
    b"\tmemset(buf, UARTE_MAGIC_BYTE, len);\n\n"
    b"\tif (IS_ENABLED(UARTE_ANY_CACHE) && (cfg->flags & UARTE_CFG_FLAG_CACHEABLE)) {\n"
    b"\t\tsys_cache_data_flush_range(buf, len);\n"
    b"\t}\n"
)

# A no-collision swap provisionally assumes one more byte in the old buffer.
# Captured RAM showed that the byte can instead land at the start of the new
# buffer. Defer the decision until a stable byte counter and DMA pointer pair
# supplies the actual cut: count - (PTR - new_buffer - 1). Keep the AA sentinel
# and the collision critical window unchanged; fail closed after 128 attempts.
BOUNDARY_CHANGES = (
    (
        "cbwt_fields",
        b"\tuint32_t last_cnt;\n\tuint32_t cc_usr;",
        b"\tuint32_t last_cnt;\n\tuint32_t boundary_old_start;\n"
        b"\tuint32_t boundary_known_end;\n\tbool boundary_pending;\n\tuint32_t cc_usr;",
    ),
    (
        "helper_before_update",
        b"static bool update_usr_buf(const struct device *dev, uint32_t len, bool notify_any, bool buf_req)\n",
        b"""/* Deferred post-swap boundary resolution, outside the collision critical window. */
static bool resolve_bounce_boundary(const struct device *dev, uint32_t len)
{
\tconst struct uarte_nrfx_config *cfg = dev->config;
\tstruct uarte_async_rx_cbwt *cbwt_data = cfg->cbwt_data;
\tuint32_t cnt, ptr, diff, cut, limit, known_remaining;

\tif (!cbwt_data->boundary_pending) {
\t\treturn true;
\t}
\tif ((cbwt_data->bounce_idx > 1U) ||
\t    (cbwt_data->curr_bounce_buf != cfg->bounce_buf[1U - cbwt_data->bounce_idx]) ||
\t    (cbwt_data->bounce_off > cfg->bounce_buf_len)) {
\t\treturn false;
\t}
\tfor (uint32_t attempt = 0U; attempt < 128U; attempt++) {
\t\tcnt = get_byte_cnt(cfg->timer_regs);
\t\tptr = cfg->uarte_regs->DMA.RX.PTR;
\t\tif ((cnt != get_byte_cnt(cfg->timer_regs)) ||
\t\t    (ptr != cfg->uarte_regs->DMA.RX.PTR)) {
\t\t\tcontinue;
\t\t}
\t\tdiff = ptr - (uint32_t)cbwt_data->curr_bounce_buf;
\t\tif (diff == 0U) {
\t\t\tknown_remaining = cbwt_data->boundary_known_end - cbwt_data->last_cnt;
\t\t\tif ((known_remaining <= cfg->bounce_buf_len) &&
\t\t\t    (len <= known_remaining)) {
\t\t\t\treturn true;
\t\t\t}
\t\t\tcontinue;
\t\t}
\t\tif (diff > cfg->bounce_buf_len) {
\t\t\treturn false;
\t\t}
\t\tcut = cnt - (diff - 1U);
\t\tlimit = cut - cbwt_data->boundary_old_start;
\t\tif ((limit >= cfg->bounce_buf_len) ||
\t\t    (limit < cbwt_data->bounce_off)) {
\t\t\treturn false;
\t\t}
\t\tcbwt_data->bounce_limit = limit;
\t\tcbwt_data->anomaly_byte_addr =
\t\t\t&cfg->bounce_buf[cbwt_data->bounce_idx][limit];
\t\tcbwt_data->boundary_pending = false;
\t\treturn true;
\t}
\treturn false;
}

static bool update_usr_buf(const struct device *dev, uint32_t len, bool notify_any, bool buf_req)
""",
    ),
    (
        "update_entry",
        b"\tanomaly_byte_handle(dev);\n\n\tdo {\n",
        b"\tif (!resolve_bounce_boundary(dev, len)) {\n\t\treturn false;\n\t}\n"
        b"\tanomaly_byte_handle(dev);\n\n\tdo {\n",
    ),
    (
        "post_unlock_record",
        b"\tirq_unlock(key);\n\n\treturn prev_buf_cnt;\n",
        b"""\tirq_unlock(key);
\tcbwt_data->boundary_pending = (prev_buf_inc != 0U);
\tif (cbwt_data->boundary_pending) {
\t\tcbwt_data->boundary_old_start = cbwt_data->last_cnt - cbwt_data->bounce_off;
\t\tcbwt_data->boundary_known_end = cnt;
\t}

\treturn prev_buf_cnt;
""",
    ),
    (
        "rx_restart",
        b"\tcbwt_data->last_cnt = 0;\n\tcbwt_data->bounce_off = 0;",
        b"\tcbwt_data->boundary_pending = false;\n\tcbwt_data->last_cnt = 0;\n"
        b"\tcbwt_data->bounce_off = 0;",
    ),
)

# At 1 Mbaud, 8N1 transmits one byte in 10 us. Sampling count and PTR one
# microsecond apart checks a quiet interval for this HCI profile only; it does
# not claim to be sufficient for other baud rates or peripherals. The bounded
# 128-attempt sampler also settles the first new byte before anomaly recovery.
# Captured new_count=7 showed that *every* successful swap needs deferred
# resolution, not just the original no-collision prev_inc=1 branch.
COHERENCY_CHANGES = (
    (
        "sampler before anomaly",
        b"static void anomaly_byte_handle(const struct device *dev)\n",
        b"""/* 1 Mbaud HCI: one quiet microsecond is shorter than an 8N1 byte. */
static bool sample_bounce_position(const struct device *dev, uint32_t *count,
\t\t\t\t   uint32_t *pointer)
{
\tconst struct uarte_nrfx_config *cfg = dev->config;
\tuint32_t cnt, ptr;

\tfor (uint32_t attempt = 0U; attempt < 128U; attempt++) {
\t\tcnt = get_byte_cnt(cfg->timer_regs);
\t\tptr = cfg->uarte_regs->DMA.RX.PTR;
\t\tk_busy_wait(1);
\t\tif ((cnt == get_byte_cnt(cfg->timer_regs)) &&
\t\t    (ptr == cfg->uarte_regs->DMA.RX.PTR)) {
\t\t\t*count = cnt;
\t\t\t*pointer = ptr;
\t\t\treturn true;
\t\t}
\t}
\treturn false;
}

static void anomaly_byte_handle(const struct device *dev)
""",
    ),
    (
        "settler after anomaly",
        b"static uint32_t fill_usr_buf(const struct device *dev, uint32_t len)\n",
        b"""static bool settle_bounce_anomaly(const struct device *dev)
{
\tconst struct uarte_nrfx_config *cfg = dev->config;
\tstruct uarte_async_rx_cbwt *cbwt_data = cfg->cbwt_data;
\tuint32_t cnt, ptr, diff;

\tif (cbwt_data->anomaly_byte_addr == NULL) {
\t\treturn true;
\t}
\tif (!sample_bounce_position(dev, &cnt, &ptr)) {
\t\treturn false;
\t}
\tdiff = ptr - (uint32_t)cbwt_data->curr_bounce_buf;
\tif (diff > cfg->bounce_buf_len) {
\t\treturn false;
\t}
\tif (diff < 2U) {
\t\treturn true;
\t}
\tanomaly_byte_handle(dev);
\treturn true;
}

static uint32_t fill_usr_buf(const struct device *dev, uint32_t len)
""",
    ),
    (
        "coherent resolver sample",
        b"""\t\tcnt = get_byte_cnt(cfg->timer_regs);
\t\tptr = cfg->uarte_regs->DMA.RX.PTR;
\t\tif ((cnt != get_byte_cnt(cfg->timer_regs)) ||
\t\t    (ptr != cfg->uarte_regs->DMA.RX.PTR)) {
\t\t\tcontinue;
\t\t}
""",
        b"""\t\tif (!sample_bounce_position(dev, &cnt, &ptr)) {
\t\t\treturn false;
\t\t}
""",
    ),
    (
        "all successful swaps provisional",
        b"""\tcbwt_data->boundary_pending = (prev_buf_inc != 0U);
\tif (cbwt_data->boundary_pending) {
\t\tcbwt_data->boundary_old_start = cbwt_data->last_cnt - cbwt_data->bounce_off;
\t\tcbwt_data->boundary_known_end = cnt;
\t}
""",
        b"""\tcbwt_data->boundary_pending = true;
\tcbwt_data->boundary_old_start = cbwt_data->last_cnt - cbwt_data->bounce_off;
\tcbwt_data->boundary_known_end = cnt;
""",
    ),
    (
        "settle before user copy",
        b"""\tif (!resolve_bounce_boundary(dev, len)) {
\t\treturn false;
\t}
\tanomaly_byte_handle(dev);
""",
        b"""\tif (!resolve_bounce_boundary(dev, len)) {
\t\treturn false;
\t}
\tif (!settle_bounce_anomaly(dev)) {
\t\treturn false;
\t}
""",
    ),
)


def transform(source: bytes) -> bytes:
    if hashlib.sha256(source).hexdigest() != AUDITED_SHA256:
        raise ValueError("UARTE source changed: review audited NCS v3.4.1 source")
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError("Missing or duplicate UARTE prepare function anchor")
    start = source.index(START)
    end = source.index(END, start)
    function = source[start:end]
    if function.count(OLD_BODY) != 1 or not function.endswith(b"}\n"):
        raise ValueError("Missing or duplicate audited bounce-prepare body")
    generated = source[:start] + function.replace(OLD_BODY, NEW_BODY) + source[end:]
    for label, old, new in BOUNDARY_CHANGES:
        if generated.count(old) != 1 or new in generated:
            raise ValueError("Missing or duplicate UARTE boundary anchor: " + label)
        generated = generated.replace(old, new, 1)
    for label, old, new in COHERENCY_CHANGES:
        if generated.count(old) != 1 or new in generated:
            raise ValueError("Missing or duplicate UARTE coherency anchor: " + label)
        generated = generated.replace(old, new, 1)
    return generated


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("source and output must differ")
    try:
        generated = transform(args.source.read_bytes())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    args.output.write_bytes(generated)


if __name__ == "__main__":
    main()
