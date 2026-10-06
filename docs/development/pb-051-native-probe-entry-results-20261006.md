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
## Portable probe module (2026-10-06, exact grammar)

`sdk_root` is a required explicit parameter (no home literal/SDK_ROOT
default). `verify_sdk_sources` does one bounded regular non-symlink read
per support file via `os.open(..., O_NOFOLLOW|O_NONBLOCK)` and `fstat`,
hashed once per inspector call and reused across recognized records; no
cached-`/home` dependency, and the test fixture copies the pinned SDK
support files from the actual `ascs_bsim_run.sdk_root()` (pinned
`ZEPHYR_BASE`). Exact required singular fields use
`re.findall` count-1 semantics (kind/variable/exitCode/cached/CMAKE_C_FLAGS/
CMAKE_EXE_LINKER_FLAGS); compile count exactly 1 with `[1/2]` and the
`cmTC_[hex]` id plus the absolute `/CMakeScratch/TryCompile-<alnum>/src.c`
scratch path; the same object id links count 1 (or 2 with the exact
FAILED echo row for exit-1 shapes); extra unrecognized compiler/link rows
inside the event reject. Exact diagnostic rows are compared via
`collections.Counter` multiset equality against the shape's exact
expected lines built from the pinned paths plus the actual `cm_id`
including the exact `FAILED: [code=1] <cm_id>`, exact collect2, exact
ninja, exact echoed link, and the exact orphan (section, object,
placement/source) rows; missing/extra/wrong/swap/duplicate diagnostics
reject with the precise extras/missing counts, and generic gcc/CMake
warning rows in the event metadata reject with the same strict wording
(no broadening). The retained event prefix/header gaps are scanned too:
any diagnostic dropped outside an event root (before the first event or
in the header) rejects, so a diagnostic cannot disappear by being moved
out of the recognized envelope. Dead helpers (`_event_fields`,
`starting_after`, the second SDK verification inside the record builder
when the first already ran, `sdk_hashes` parameter and `expected_exit`)
were removed. `SDK_ROOT`/home default was removed; all entry points take
an explicit `sdk_root` parameter; portable `native_bsim_probes` accepts
a fixture root so no `/home/...` literal is needed.

The canonical suite keeps the `_start` rejection and adds a synthetic
complete four-event fixture (static/-N/orphan-warn/orphan-error) with a
copied pinned SDK support pair from `ascs_bsim_run.sdk_root()`; no
`/tmp/opencode` hosted-YAML dependency exists in the canonical test
suite (the previously removed one-time external audit records live in
this document's read-only history and in PB-051's notes only).

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

## Exact probe-disposition module (2026-10-06, bounded grammar)

New `scripts/native_bsim_probes.py` owns the only disposition path. It
parses the raw bounded CMake event envelope (exact top-level `-`
separator lines), keeps every input byte unchanged, and recognizes
exactly four diagnostic-bearing capability probes, each validated against
its exact required backtrace (CheckCCompilerFlag 105 / CheckSourceCompiles
104 / extensions 2604/2421/1199 plus the exact `linker_flags.cmake` line
for that option), `CMAKE_C_FLAGS: "-m32"`,
`CMAKE_EXE_LINKER_FLAGS: ""`, `cached: true`, the exact argv compile/link
lines with `--entry=main` and the exact tested option, and an exact
per-shape diagnostic multiset; each probe is recognized at most once; the
unsupported/supported flag reflects the actual recorded exit status (the
failed probes stay genuinely `supported: false`, never converted).
Installed-source identity is verified (`linker_flags.cmake`
`c476ef56c83deb6553a852218fd70fd921aa587a51d66d31082c234159735a26`,
`extensions.cmake`
`6cacb57208f801f9062eac5b75a411d1be0ed7de2a250843ae0334dd3a94a9f3`) only
when actually granting a disposition; cached local YAML without
diagnostics recognizes nothing (empty list) and needs no verification.
Any unrecognized diagnostic (compiler-ID failures, `Ninja` real
build-stopped, wrong flags/exit/shape/extra warning/missing context or a
standalone warning with no envelope) still raises the exact strict error
with the probe name, exit status and line as before, preserving the old
`probe=<name> exit=<code>` wording and the `_start` rejection.
`inspect_warnings` now delegates the configure line-scan to this module
(all CMake/Kconfig/Ninja checks unchanged); the runner records the
recognized records as per-role `capability-probes.json` and the sealed
suite record carries a `capability_probe_dispositions` list; the public
`check-native-bsim-build.py` CLI records
`capability_probe_dispositions` in its verdict too.
`scripts/native_bsim_probes.py` and the two pinned SDK support sources
(`zephyr/cmake/linker/ld/linker_flags.cmake`,
`zephyr/cmake/modules/extensions.cmake`) enter `source_paths` for
snapshot coverage.

Read-only classification audit (never a hosting acceptance): running
`inspect_configure_probes` over the downloaded actual hosted YAML
`/tmp/opencode/pb051-hosted-bsim-37437769198/scenarios/build/receiver/
cmake-configure.yaml` (SHA-256
`816034788a830e83bc614492147741a275fa02d915bb7baeec4b8b2992b79fbf`,
unchanged) returns the four structured records with the exact exit
statuses and dispositions as recorded above; no artifact was modified
(the audit wrote nothing; the recorded facts are in-module memory and
this note).

## Exact four purpose-test dispositions (2026-10-06, source-grounded)

The retained hosted YAML above is a real configure log, not a synthetic
fixture. Running the actual inspector (`inspect_configure_probes`) over
its unmodified bytes returns exactly four recognized capability-probe
records:

| configure line | variable | exit | supported | disposition |
| --- | --- | --- | --- | --- |
| 8886 | `check_C__fuse_ld_bfd__static` | 1 | False | `capability-test-failed-expected-missing-static-libc` |
| 8997 | `check_C__fuse_ld_bfd__Wl__N` | 1 | False | `capability-test-failed-expected-missing-static-gcc_s` |
| 9042 | `check_C__fuse_ld_bfd__Wl___orphan_handling_warn` | 0 | True | `capability-test-warn-supported` |
| 9082 | `check_C__fuse_ld_bfd__Wl___orphan_handling_error` | 1 | False | `capability-test-failed-expected-orphan-error` |

Purpose-grounded meaning of each: Zephyr's `baremetal` linker property
(`zephyr/cmake/linker/ld/linker_flags.cmake` lines 10/18/22) capability
tests `-static` and `-Wl,-N` against an installed toolchain whose x86
32-bit static libc/gcc_s runtime pieces are absent; the probes are
expected to fail their link with the recorded exact diagnostics
(`cannot find -lc` / `cannot find -lgcc_s` plus the "have you installed
the static version" questions, collect2 and ninja failure rows, and the
exact echoed link command). `--orphan-handling=warn` is recorded
supported (exit 0 with exactly the five orphan-placement warnings and no
failure rows); `--orphan-handling=error` is recorded supported-for-error
only in the sense that the probe's exact failed shape is the expected
capability-test result (five `unplaced orphan section` errors plus the
failure triple). The `supported` flag is the plain exit-0 mapping of the
actual recorded exit status: three shapes keep `supported: false` exactly
as recorded, and `cached: true` never implies success.

This is a tiny, recorded-purpose exception only: recognition requires the
exact event kind, exact `try_compile-v1` backtrace entries (absolute
paths ending in the pinned toolchain `cmake-4.2/Modules` files and
`ncs/v3.4.1/zephyr` sources with exact line/function pairs), exact
compile/link argv, exact `-m32`/empty linker flag fields, the exact
per-shape diagnostic multiset, and the exact-hashed installed SDK support
pair. Every other raw compiler, collect2, Ninja build-stopped, gcc
warning, CMake warning, metadata, prefix or unknown-probe diagnostic
still fails the run through the same strict checker with no wording
change; nothing about production build warnings is waived.

Hosted evidence chain for these dispositions (all read-only retained
artifacts; none yet accepted as a passing hosted bsim job):
- run `37422469265` (first `_start` warning shape; `test-heavy (bsim)`
  FAILED): whole-run console
  `/tmp/opencode/pb051-pr16-hosted-run.log` (SHA-256
  `a038e853712f0c36f28fc54ee81b7c5d7045afcd856609a8d216dc1d68293e40`),
  failed-job log `/tmp/opencode/pb051-pr16-bsim-failed.log` (SHA-256
  `910914d4cd462a85c3d7c461a2a6938feff2d437feb42fcd7a1dea6eefdc5211`);
  `_start` warning is now structurally absent from new builds since the
  `-Wl,--entry=main` probe repair and there is no `_start` exemption.
- run `37432566147` (second shape: exact `-static` probe failure
  rejected): artifacts
  `/tmp/opencode/pb051-pr16-hosted-b615-run.log` (SHA-256
  `96189036f5eda268bf7c6bfd9c0da89634a786e03bb93486d60f16237916339d`),
  `/tmp/opencode/pb051-pr16-b615-failed.log`; recorded
  `cmake-configure.yaml` SHA-256
  `9047777c50bc5ea0dada85218054969d947565250411b183fdcf72b2f3ce91`.
- run `37437769198` (same `-static` probe shape again): whole-run
  console `/tmp/opencode/pb051-pr16-hosted-d298-run.log` (SHA-256
  `d1d842978f6c6c454cb0ea5022f6ccb7fa0850e639d93dc838e295f9f7042e08`),
  failed-job log `/tmp/opencode/pb051-pr16-d298-failed.log`; the
  downloaded actual receiver `cmake-configure.yaml`
  (SHA-256 `816034788a830e83bc614492147741a275fa02d915bb7baeec4b8b2992b79fbf`)
  is the classification input recorded above.
- No new hosted acceptance has happened yet for the dispositions; the
  following cold-native phase and canonical gate are the recorded local
  verification stages before any fresh push.

Integration truth (top-level): the executed runner records per-role
`build/<role>-capability-probes.json` for the two build roles only
(receiver, client; the phy role builds no C capability probes) and adds a
top-level suite `capability_probe_dispositions` map of exactly
`{receiver: [...], client: [...]}`; the independently executed family
verdict JSON (checker log shape plus required trace counters) remains
unchanged, never extended by classification data.
