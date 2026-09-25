#!/usr/bin/env python3
"""Generate the audited NCS v3.4.1 UARTE bounce-prepare compatibility source."""

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
    return source[:start] + function.replace(OLD_BODY, NEW_BODY) + source[end:]


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
