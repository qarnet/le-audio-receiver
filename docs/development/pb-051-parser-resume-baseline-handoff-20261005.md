# PB-051: parser resume baseline handoff (2026-10-05)

## Resumption and boundary

Owner lifted the 2026-10-04 PB-051 pause after PB-053 completion. Resume the
original encoded ASCS rejection and lifecycle scope, with historical pause and
original acceptance criteria retained. This phase records only the native public
parser baseline. Do not change test bytes, production sources, SDK, wrapper,
encoded wire lane, or hardware. No native result establishes physical ASCS or RF
acceptance. PB-041 remains blocked pending ADC identification and safe wiring;
PB-042 remains blocked pending Windows access, tooling and separate rights review.

## Previous immutable physical diagnostic, read back before this run

`/tmp/opencode/pb051-ltv-physical-baseline/verdict.json`, `source-hashes.json`
and `uart.log` remain present. The standalone NCS v3.4.1 CPUAPP diagnostic
reported 2 pass, 1 fail, 0 skip, total 3, with the exact-end missing value
case returning `ret=0 exposed=1 value=aa` for a padded, logically short input.
Image SHA-256 `14105caa24b1088e37187701fb59471ab13ab661389061b66f8c1ddff6f94496`;
UART SHA-256 `d34754a6a67b36e78c31a4678367bf757a0a17b47ba8b7985a04e676696a677c`.
`source-hashes.json` pins SDK audio.c SHA-256
`07b0b016ba518dbdb7f324c9948dbfd19619569f62f216aa75cde2e88e574d1d`.
These are historical pure-parser observations, not current device identity,
new flash, or encoded ASCS execution. No hardware action in this phase.

## Focused run contract

Active installed Zephyr revision:
`33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`. `audio.c:34-81`
delivers each valid entry to its callback, stops immediately on callback
false with `-ECANCELED`, and checks each subsequent entry independently.
At line 53 it checks `i + len > size`, missing the length octet in the bound.
Whole-buffer preflight would change valid-prefix/invalid-suffix callback
visibility. Preserve per-entry delivery and early cancellation in any later
repair, including `test_final_entry_length_validation`'s delivered prefix.
The prior preflight suggestion in the 2026-10-04 refinement is superseded on
this point; a repair and actual encoded-wire diagnosis are next-phase work.
The GNU `--wrap` seam cannot intercept calls from the same SDK object; do not
claim SDK-wide protection. Keep SDK read-only and canonical 17/26 unchanged.

After verifying `/tmp/opencode` exists and exclusive
`/tmp/opencode/pb051-ltv-native-resume-r1` does not, retain full command
stdout/stderr at `build-and-run.log` under that root. Run from repo root:

```sh
env NIX_HARDENING_ENABLE="" west build --no-sysbuild -b native_sim/native/64 -d /tmp/opencode/pb051-ltv-native-resume-r1/build tests/unit/ltv_bounds -p -t run -- -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y
```

Expected: seven focused tests, one `test_metadata_length_validation` failure;
capture actual exit code, exact test summary, exposed callback value, and log
SHA-256. Do not retry or alter tests/source. Retain the known native-only
unsupported-SoC CMake product notice in raw output; stop and report any other
failure, new compiler or Kconfig warning, missing tool, or provider notification.

## Actual focused result

Executed command above once with unchanged test bytes; exit code 1, expected
assertion only. Raw stdout/stderr:
`/tmp/opencode/pb051-ltv-native-resume-r1/build-and-run.log`, SHA-256
`fc23ad805a4d492149a99dcd2c3293deab7a965349603f33b8d4f43328e66b1b`.
Exact suite line:

```text
SUITE FAIL -  85.71% [ltv_bounds]: pass = 6, fail = 1, skip = 0, total = 7 duration = 0.000 seconds
```

`test_metadata_length_validation` printed
`METADATA_VALIDATION ret=0 delivered_entries=1 value=aa`, then failed the
`ret == -EINVAL` assertion at `test_ltv.c:33`. The six other tests passed,
including valid-prefix/invalid-suffix delivery and callback cancellation.
Log ends `PROJECT EXECUTION FAILED` and `FATAL ERROR: command exited with
status 1`. Only CMake warning is the documented native SoC product-support
notice at `nrf/cmake/device_support.cmake:34`. The fake-entropy test banner is
present. No compiler or Kconfig warning observed; no source repair, wire test,
hardware action, canonical gate, or acceptance claim in this phase.
