# PB-051: per-entry public LTV guard, native verification (2026-10-05)

## Evidence and decision

Installed NCS v3.4.1 SDK parser in `audio.c:34-81` checks `i + len > size`
before consuming the length octet. Native public API baseline 6/1/7, previous
CPUAPP pure-parser diagnostic 2/1/3, and encoded R4 ASCS request
`0301010202f0` / CP success `0301010000` establish this logical-end case.
Public `audio.h:272-287` specifies callback delivery per entry and
`-ECANCELED` on early stop. Whole-buffer preflight would silently change
already-delivered prefix and cancellation outcomes and is rejected. Repair
each entry's bounds before forwarding just that entry to the real SDK parser.

Production receiver root, canonical BSim receiver and additive ASCS receiver
link a repository-owned GNU `--wrap=bt_audio_data_parse` with no Kconfig or
production opt-out. Native public parser tests compile the installed real
`audio.c` alongside guard, on by default with test-only CMake option to
reproduce prior unguarded baseline. Guard cannot intercept same-object SDK
calls from `bt_audio_data_get_val` or `bt_audio_valid_ltv`; no SDK-wide claim,
no SDK source change. No client, HIL source, offload, LC3, or hardware change.

## Focused verification boundary

Retain previous roots. Verify `/tmp/opencode` exists and new
`/tmp/opencode/pb051-ltv-native-guard-r1` is absent, then create exclusive
root. Run exactly once from repo root, save full stdout/stderr and exit code:

```sh
env NIX_HARDENING_ENABLE="" west build --no-sysbuild -b native_sim/native/64 -d /tmp/opencode/pb051-ltv-native-guard-r1/build tests/unit/ltv_bounds -p -t run -- -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y
```

Inspect actual test totals, dynamic inputs and callback behavior, native ELF
symbols/link map and real SDK parser plus wrapper cross-object linkage. Retain
raw log and SHA-256. Native-only CMake unsupported-SoC product notice remains
visible. New compiler/Kconfig or unrelated warnings and unexpected failure
must be reported without suppression. Do not run repaired wire, physical,
full canonical gates, stage, commit or push this phase. Passing native guard
does not prove physical or encoded ASCS recovery; validate separately later.

## Actual native result and link evidence

Focused command above ran once, exit code 0. Raw log
`/tmp/opencode/pb051-ltv-native-guard-r1/build-and-run.log` SHA-256
`784bdfa23ca5be117d307ab1be9fa916aad64d3da0cda4de2dfb4deda61ade6b`.
Exact summary:

```text
SUITE PASS - 100.00% [ltv_bounds]: pass = 12, fail = 0, skip = 0, total = 12 duration = 0.000 seconds
METADATA_VALIDATION ret=-22 delivered_entries=0 value=00
PROJECT EXECUTION SUCCESSFUL
```

All seven existing tests pass unchanged, including delivered valid prefix
before malformed suffix and early `-ECANCELED`. Five added public parser
tests cover missing type, missing value after delivered prefix, cancellation
before invalid suffix, full 255-length entry versus logically short buffer,
and null/empty/valid retry. No compiler or Kconfig warning; only retained
native-only SoC CMake product notice and test-only fake-entropy banner.

`build/build.ninja` compiles the actual installed SDK `audio.c`, repository
guard and test objects into `app/libapp.a`, and sets
`LINK_FLAGS = -gdwarf-4 -Os -Wl,--wrap=bt_audio_data_parse`. `nm -A` shows
`test_ltv.c.obj` has undefined `bt_audio_data_parse`, `audio.c.obj` defines
real `bt_audio_data_parse`, and guard object defines
`__wrap_bt_audio_data_parse` with undefined `__real_bt_audio_data_parse`.
Final executable `build/zephyr/zephyr.exe` has both actual
`bt_audio_data_parse` at `0x403bab` and `__wrap_bt_audio_data_parse` at
`0x403c4a`; disassembly includes calls into wrapper. The native executable
SHA-256 is
`90d720996bb11fe5f9d787adb4fdad781e33cc7ca0e1b42ac0239970e32196ef`;
intermediate `build/zephyr/zephyr.elf` SHA-256 is
`cad0420376afe11474318a95f2ab5fc7b4d2ef9b2fdf610695f9cd01271d3f67`.
`audio.c.obj` also has direct same-object references to
`bt_audio_data_parse` from `bt_audio_data_get_val` and
`bt_audio_valid_ltv`; they are outside this guard's reach. Root and BSim
receiver CMake linkage was edited but their image builds and ASCS wire
behavior were **not** verified this phase. No physical, hardware, canonical
gate or post-repair encoded-wire acceptance claim.
