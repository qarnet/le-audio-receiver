#!/usr/bin/env python3
"""Authored global-coordinate expected PCM; never execute the DUT."""

import importlib.util
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location(
    "asrc_reference", Path(__file__).parent.parent / "asrc_oracle/test_asrc_oracle.py"
)
assert spec is not None and spec.loader is not None
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)

lines = [
    "/* Generated from the independent rational reference, not DUT output. */",
    "struct oracle_block { unsigned frames, produced; int ppm;",
    "const int16_t *input, *output; };",
    "#define ORACLE_BLOCKS 6",
]
for size in (360, 480):
    source = model.signal("random", size * 6)
    reference = model.Reference(source, (48000, 47619))
    controls = (0, -2000, 2000, -1, 0, 500)
    rows = []
    for block, ppm in enumerate(controls):
        pcm = [
            sample
            for frame in source[block * size : (block + 1) * size]
            for sample in frame
        ]
        output = reference.block(size, ppm)
        name = f"v{size}_{block}"
        for suffix, values in (("input", pcm), ("output", output)):
            lines.append(
                f"static const int16_t {name}_{suffix}[] = {{"
                + ",".join(map(str, values))
                + "};"
            )
        rows.append(f"{{{size},{len(output) // 2},{ppm},{name}_input,{name}_output}}")
    lines.append(
        f"static const struct oracle_block v{size}[] = {{" + ",".join(rows) + "};"
    )
Path(sys.argv[1]).write_text("\n".join(lines) + "\n")
