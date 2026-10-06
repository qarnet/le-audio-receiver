# PB-051 native probe entry results (2026-10-06)

Source-grounded repair for the hosted-run bsim build-diagnostic failure;
no warning waiver, no checker-whitelist change, no SDK edit.

## Hosted run identity (37422469265)

Workflow run 37422469265 on the exact pushed PR #16 head
`d7f2e4c6ae5728cad371fe13ec101892f40c853f`
(https://github.com/qarnet/le-audio-receiver/pull/16). Result: `test-unit`
SUCCESS, `test-heavy (coverage)` SUCCESS, `firmware` SUCCESS, `release`
SKIPPED (pull_request), but `test-heavy (bsim)` FAILED (and the aggregate
`tests` job FAILED as designed on a failed child). The exact console
artifacts retained by the executor:

- Whole-run console: /tmp/opencode/pb051-pr16-hosted-run.log (SHA-256
  a038e853712f0c36f28fc54ee81b7c5d7045afcd856609a8d216dc1d68293e40).
- Failed-job raw log (gh run view --log-failed): /tmp/opencode/
  pb051-pr16-bsim-failed.log (SHA-256
  910914d4cd462a85c3d7c461a2a6938feff2d437feb42fcd7a1dea6eefdc5211).
- Delegator-downloaded hosted artifact root, read-only:
  /tmp/opencode/pb051-hosted-bsim-37422469265/ with
  ascs-le-audio-ascs.4xWrnx/ (the ASCS lane container) and scenarios/
  (the Stage 1 build tree). The exact configure YAML with the failing
  probe at lines 8875-8888 is
  /tmp/opencode/pb051-hosted-bsim-37422469265/scenarios/build/receiver/
  cmake-configure.yaml: variable `check_C__fuse_ld_bfd__nostdlib`,
  ninja steps
  `[1/2] gcc -Dcheck_C__fuse_ld_bfd__nostdlib ... -m32 -fuse-ld=bfd
  -nostdlib -o ...src.c.obj -c .../TryCompile-4HW76o/src.c` and
  `[2/2] gcc -m32 -fuse-ld=bfd -nostdlib ...src.c.obj -o cmTC_04d51`,
  followed on line 8883 by
  `/nix/store/i7mdvmliqcb5lz0nqija8rq55vws6gi8-binutils-2.44/bin/ld.bfd:
  warning: cannot find entry symbol _start; defaulting to 00001000`
  and `exitCode: 0`. The identical probe appears in the ASCS receiver
  tree under ascs-le-audio-ascs.4xWrnx/.
- Retained identity hashes from that tree: cmake-configure.yaml
  193ae1ca85843ea79d531cfd52f5fc6cd2f08c9977c4bb6e9cb9a747dd9d3875,
  cmake.out 3cc8f88dc4aa73d28fcf733f2ae6b149cd9eaa3ee5520eb916734a4064d73067,
  ninja.out 0e26454dfa70cb1b78382d11e7bb1c0d52186ddf5b0efe93a550779eacdcdf39,
  resolved.config 8f34c79176d71d9c88ddb53356ae308cd55b085d8f93091ce360cc5d4b9a4ba7.

## Source-grounded probe mechanism (installed sources, read-only)

- /home/thomas-workstation/ncs/toolchains/8285d8ad56/usr/local/share/
  cmake-4.2/Modules/Internal/CheckFlagCommonConfig.cmake (lines 13+):
  `CMAKE_CHECK_FLAG_COMMON_INIT` sets the C probe source to
  `int main(void) { return 0; }`.
- /home/thomas-workstation/ncs/v3.4.1/zephyr/cmake/linker/ld/
  linker_flags.cmake (lines 10-17): the `baremetal` linker property lists
  `-nostdlib` (among others); this reaches the probe when the property
  loop appends it to the required-flags set.
- /home/thomas-workstation/ncs/v3.4.1/zephyr/cmake/linker/ld/target.cmake
  (lines 12-13): `-fuse-ld=bfd` is appended to `CMAKE_REQUIRED_FLAGS` when
  the bfd linker is in use, matching the hosted probe's variable name
  `check_C__fuse_ld_bfd__nostdlib` (check_ + empty option + `_C_` +
  required flags).
- /home/thomas-workstation/ncs/toolchains/8285d8ad56/usr/local/share/
  cmake-4.2/Modules/Internal/CheckSourceCompiles.cmake (lines 74-76 and
  104-109): `CMAKE_REQUIRED_LINK_OPTIONS` are applied by `try_compile`
  as probe `LINK_OPTIONS` (final-target options are unaffected),
  while other required state flows into the probe's compile definitions.

## Direct probe reproduction (executor-authored, host only)

Exact authored probe source and outputs (delegator artifacts, read-only;
both links produce an ELF32 i386 object):

- /tmp/opencode/pb051-probe-entry-20261006.c - the authored probe main.
- /tmp/opencode/pb051-probe-entry-unset-20261006.log - the baseline link
  with -fuse-ld=bfd -nostdlib reproduces the exact
  `warning: cannot find entry symbol _start; defaulting to 00001000`
  line; ELF at /tmp/opencode/pb051-probe-entry-unset-20261006.elf.
- /tmp/opencode/pb051-probe-entry-main-20261006.log - the same link with
  the additional `-Wl,--entry=main` is empty (no warning); ELF at
  /tmp/opencode/pb051-probe-entry-main-20261006.elf.

## Repair: probe-only entry option in the four native fixture CMakeLists

After the existing `CMAKE_{C,CXX,ASM}_FLAGS_INIT ... -m32` lines and
before `find_package(Zephyr REQUIRED ...)` in all four
(tests/bsim/CMakeLists.txt, tests/bsim/client/CMakeLists.txt,
tests/ascs_bsim/receiver/CMakeLists.txt,
tests/ascs_bsim/client/CMakeLists.txt):

```
list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--entry=main")
```

With an accurate comment describing the probe mechanism. Scope is the
compiler-capability probe link only: CMake's
`Internal/CheckSourceCompiles.cmake` applies
`CMAKE_REQUIRED_LINK_OPTIONS` exclusively to `try_compile` LINK_OPTIONS,
never to final targets; `target_link_options`,
`CMAKE_EXE_LINKER_FLAGS`, the SDK, the warning-checker whitelist, the
production root/physical target, matrix policy and scenarios are all
untouched. The warning is not suppressed and exit 0 with a warning is not
accepted: the probe simply stops emitting a meaningless entry-symbol
warning because it now has an explicit `--entry=main`.

## Focused harness proof (installed cmake/ninja host-CMake)

New `HostCmakeProbeEntry` tests in tests/unit/ascs_runner/
test_ascs_runner.py (missing tool is a hard failure, never a skip):

- A real host-CMake configure of a standalone probe project with
  `-DCMAKE_C_FLAGS=-m32`, `CMAKE_REQUIRED_FLAGS "-fuse-ld=bfd"` and
  `CMAKE_REQUIRED_LINK_OPTIONS "-nostdlib"` reproduces the exact
  baseline `_start`-warning-with-exit-0 shape in its
  CMakeConfigureLog (known intentional negative fixture); the same
  project with the additional `-Wl,--entry=main` link option records
  `HAS_NOSTDLIB:INTERNAL=1` in CMakeCache.txt and its configure log
  contains `-Wl,--entry=main` while containing no
  `cannot find entry symbol` line. Probe binaries are never executed.
- The independent build-diagnostic checker is unchanged and still
  rejects a synthetic CMakeConfigureLog carrying this `_start` warning
  (`probe=check_C__fuse_ld_bfd__nostdlib`), so no checker exemption is
  needed and none exists.

The host harness is a cmake/compiler probe proof only; it is not a full
NCS build claim. The full real native matrix/gate over all four consumer
fixtures is the separate full canonical gate.

## Clean canonical gate verification (2026-10-06, commit 1868f4a)

After the comment trim, commit `1868f4ac52f4552fb6e1b383bde4a6fa7beba296`
(`PB-051: give native compiler probes an explicit entry`) and a fresh
clean detached worktree `/tmp/opencode/pb051-clean-candidate-r4`
(tracked/untracked clean before the gate; source bytes verified against
the commit), the full canonical gate ran once from the candidate workdir
with exclusive noclobber raw log `/tmp/opencode/pb051-canonical-r4.log`
(SHA-256 `1b71d4c7334d95d24cb6a1f23350a09bc5a00a3fbd6aec548de8554dc7cc68d3`),
`GATE_EXIT=0`, `Gate complete: 98 PASS / 0 FAIL / 98 TOTAL`, Stage 1
unchanged 17 scenarios/26 runs strict-checked, frozen-population coverage
exactly 5049/5491 lines, 2245/3036 branches, 378/378 functions (frozen
baseline unchanged, SHA-256 `5bb01f95afc12c0771086a537cb70c92d20f7d96c8b
9b4323528b6d9ed76de7a`), additive sidecar enforced at exactly 13/13
lines, 12/12 branches, 1/1 functions and direct matrix PASS. The actual
canon/ASCS configure logs retained under
`/tmp/le-audio-ascs.ig3DqI/run/build/{receiver,client}/.../CMakeConfigureLog.yaml`
contain zero `entry symbol _start` occurrences (warning grammar unchanged;
the strict checker accepted the builds because no warning exists). The
whole `pb051-canonical-r4.log` contains zero `_start` strings. The sealed
ASCS lane record at `/tmp/le-audio-ascs.ig3DqI/run` carries suite-record
SHA-256 `0380273a64880ad61f0b73bc33b2274087982068eb2da0ade47046cf120cba26`
with accepted=true, run_id `0ac278db67204b6ab4d1a1413f1333d3`, exact totals
60 cases/65 render phases/259 raw exchanges/269 response records and the
clean-commit worktree (dirty inventory digest `e3b0c442...b7852b855`).
Production sources are unchanged since `33310f0`'s receiver build
(app+FLPR ELF hash `70f01610...6a77` and `62ba69d4...4caaf` and the
contract/wrap proofs referenced by the results document remain
source-identical), so the hosted firmware job is the re-verification
path; no local production rebuild was performed in this phase. No
hardware, no release, not Done.
## Hosted follow-up on the same run lane (37432566147, 2026-10-06)

The next hosted run `37432566147` for exact head
`b6158d4e8456e2a64a632c0f9d62c6de0d579e80` still failed
`test-heavy (bsim)` (and the aggregate `tests`), with `test-unit`
SUCCESS, `test-heavy (coverage)` SUCCESS, `firmware` SUCCESS and
`release` SKIPPED as designed. The exact retained raw artifacts:

- Whole-run console: /tmp/opencode/pb051-pr16-hosted-b615-run.log
  (SHA-256 96189036f5eda268bf7c6bfd9c0da89634a786e03bb93486d60f16237916339d).
- Failed-job raw log: /tmp/opencode/pb051-pr16-b615-failed.log.
- Receiver build identities from the runner: cmake-configure.yaml
  9047777c50bc5ea0dada85218054969d947565250411b183fdcf72b662f3ce91
  (759663 bytes), cmake.out
  63997ddab0ea79bf5edfc6de1320d31a930fa1d850e2b3aa40fde4636eed6a60,
  ninja.out fbfcec21ea72f0d42aeb5862252b5ab28b2b7a4e02814d023f796c78aec16c71,
  resolved.config 8f34c79176d71d9c88ddb53356ae308cd55b085d8f93091ce360cc5d4
  b9a4ba7.

Changed failure shape (previous `_start` warning is gone; the probe
grammar is unchanged and no waiver was applied): the checker now reports

```
ValueError: receiver: CMakeConfigureLog probe=check_C__fuse_ld_bfd__static
exit=1 line=8918: FAILED: [code=1] cmTC_03a39
```

Read-only reproduction with the exact host gcc wrapper
(iwf80230xr0z8pqh1jk3z8rgw67ydagm-gcc-wrapper-14.3.0, binutils 2.44), the
same `-m32 -fuse-ld=bfd -static` probe shape on the same authored probe
source: the link fails with
`/nix/store/i7mdvmliqcb5lz0nqija8rq55vws6gi8-binutils-2.44/bin/ld.bfd:
cannot find -lc: No such file or directory` and
`have you installed the static version of the c library ?` (log
/tmp/opencode/pb051-probe-static-nostdlib-20261006.log); the
`-nostdlib -static -Wl,--entry=main` combination itself builds clean
(/tmp/opencode/pb051-probe-static-main-20261006.log, static ELF32 i386).
Local gate r4 never surfaces this probe because the local Zephyr try
compile capability cache (ToolchainCapabilityDatabase) short-circuits the
baremetal-property probes, while the hosted runner has no cache and runs
them; this retained-evidence note records the shape, the disposition is
not decided here and no source/checker change was made from it.
The ascs lane itself PASSED in the same hosted stage again
(`Gate complete: 1 PASS / 1 FAIL / 2 TOTAL`).
