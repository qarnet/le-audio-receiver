# PB-051: native compiler probe ABI and warning classification

## Preserved failed run and exact source diagnosis

First full-matrix attempt `/tmp/opencode/pb051-owned-full-matrix-r1` stopped
after receiver build with `accepted: false`, before client build or any
family cohort. Its retained `receiver/ninja.log:108` prints an object path
ending `mbedx509.dir/error.c.obj`; a bare-word warning matcher wrongly
classified that filename as an error. The same immutable receiver
`CMakeFiles/CMakeConfigureLog.yaml:4372-4383,7377-7385` separately records
real `skipping incompatible` linker diagnostics, although CMake and Ninja
returned 0. C compiler ID and `C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS`
probes used host gcc without `-m32`, while the provided verified linker
search paths prefer 32-bit libraries. This is a build-probe ABI mismatch,
not evidence of a firmware or RF outcome. No retry or historical evidence
modification is authorized here.

## Bounded remedy and evidence boundary

In four native BSim app CMakeLists, set initial C, CXX and ASM flags to
`-m32` before `find_package(Zephyr ...)`, allowing first compiler and
configuration probes to use the same intended encrypted peer ABI.
This does not change the 64-bit separately built PHY, physical receiver,
SDK source, client C, policy or frozen 17-scenario/26-run recipe. Replace
bare-word warning detection with diagnostic syntax while retaining exact
three experimental warnings per image and one known native-SoC CMake notice
from its SDK source. Retain and inspect actual CMake configure YAML (bounded
16 MiB) on successful builds too. Unexpected real diagnostic remains fatal;
do not grant an all-probes waiver.

One compile-only receiver smoke is separately authorized in a new external
root. It uses the runner's exact CMake/Ninja shape and pinned shell/native
linker environment, but no ASCS cohort, checker, hardware or second run.
Verify compiler-ID ELF32, CMake pointer width 4, raw YAML and Ninja
diagnostics. If initial flags do not reach probes, stop with source/cache
evidence instead of guessing another option. Preserve all prior run roots.

## Single receiver compile-only smoke (2026-10-05)

One exclusive run was performed in
`/tmp/opencode/pb051-native-probe-abi-r1`, using the existing pinned
`bsim-env.sh`, nrfutil toolchain `8285d8ad56`, verified ELF32 linker
search-path helper and the same receiver CMake argv as the full runner
(saved verbatim in `smoke-record.json`). Owned `cmake` and `ninja`
returned 0; owner descendant scope closed cleanly. Approved experimental
warnings occurred once each, and the native-only CMake notice came from
`nrf/cmake/device_support.cmake:34`. Bounded configure YAML snapshot is
612466 bytes, SHA-256
`6128709a90f67bc7f6f7fa409d6b04ca84343d7bdcffe56c486d702102708c04`;
raw configure YAML and Ninja output contained no architecture-mismatch,
compiler or unlisted warning diagnostic under the corrected checker.
Compiler ID YAML says `Build flags: -m32` at line 4374 and the
`C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS` test uses `-m32` at lines
7356,7367-7368. `file -L` identifies compiler ID and outer receiver
executable as ELF32 i386. One-shot smoke-record SHA-256:
`42b3c9ca5aa6f7cc9f18791828c71e14364af0190682072917994dcd126d42f3`.

**Unmet mandatory check:** Generated
`build/receiver/receiver/CMakeFiles/4.2.1/CMakeCCompiler.cmake:42,55`
sets `CMAKE_C_ABI_COMPILED` and `CMAKE_C_SIZEOF_DATA_PTR` to empty values,
not pointer width `4`. The generated CXX compiler file also leaves its
data pointer width empty. This does not negate the observed ELF32 outputs
or successful `-m32` probes, but it cannot be reported as the required
`CMakeCCompilerABI pointer4` proof. Treat the smoke as partial and stop:
no new CMake option, SDK alteration, smoke retry or full matrix was
attempted. Owner decision needed: preserve pointer-4 acceptance and
investigate why the pinned Zephyr/CMake toolchain leaves ABI size blank,
or explicitly approve a different source-grounded ABI proof. Recommend
preserving the required pointer-4 check until that decision.

## Orchestrator resolution before full-matrix r2

The earlier requirement that generated `CMAKE_C_SIZEOF_DATA_PTR` equal `4`
was an unsupported technical assumption, not a product criterion. Installed
NCS v3.4.1 Zephyr `cmake/modules/FindHostTools.cmake:84-86` and
`cmake/modules/FindTargetTools.cmake:30-32` explicitly state "Prevent CMake
from testing the toolchain" and set `CMAKE_C_COMPILER_FORCED` and
`CMAKE_CXX_COMPILER_FORCED` to 1. Consequently the blank generated
`CMAKE_C_SIZEOF_DATA_PTR` and `CMAKE_C_ABI_COMPILED` observed in the
preserved smoke are expected, not a reason to alter SDK, force CMake ABI
testing or fill its cache. Supersede the temporary pointer-4 demand above
without rewriting the r1/smoke evidence. The current ABI proof is the
independently read ELF32 little-endian i386 compiler-ID and actual receiver
image, plus observed compiler-ID/probe `-m32` flags and warning-free raw
configure/Ninja logs. Keep strict actual binary-image ABI checking for
both encrypted ELF32 peers and the independent ELF64 x86_64 PHY.

Owner authorized exactly one full six-family r2 with the current repaired
source, fresh external matrix and command-owner roots, no retry or edits
during/after that run. No physical, codec-conformance, analog, canonical
17/26, commit or release claim follows from simulator acceptance.
