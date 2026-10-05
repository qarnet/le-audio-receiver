# PB-051 mandatory ASCS child in canonical software gate

## Bounded source and acceptance boundary

The reviewed encoded ASCS matrix is an additive mandatory BSim child
of `scripts/test-all.sh`'s `bsim` and `all` phases, immediately after
the unchanged 17-scenario/26-run Stage 1 child. Unit and coverage
phases remain unchanged. This plan changes neither Stage 1 scenarios,
client test code, frozen transport limits, firmware release workflows,
nor hardware acceptance. Independent `ascs_bsim_run.py` still requires
its fixed 60 cases, 65 renders, 259 checked raw exchanges and 269
ordered response records, and its own policy/source/image/three-peer
owner checks. A passing fake shell dispatch is not ASCS execution.

The ASCS execution root must be a new `<external container>/run`, with
the parent created by exclusive `/tmp/le-audio-ascs.XXXXXX`. Retain
the container after success or failure; never include it in the
test-all private `TMP_ROOT` removal trap. When `TEST_OUTPUT_DIR` is set,
create an exclusive uniquely named symlink under it to that run path
before child launch; the approved pinned `upload-artifact` search uses
`followSymbolicLinks: true`. The symlink is only a retained artifact
pointer. Actual native process execution and the runner's exclusive
root stay outside the repository, home and previous PB-051 evidence.

The canonical Stage 1 wrapper uses SDK `compile.source`, which writes
`cmake.out` and `ninja.out` to the shared per-role SDK build directory.
Initialize its owned `LOGROOT` and EXIT retention trap before the first
compile. Copy each role's original build logs, child resolved `.config`
and child `CMakeFiles/CMakeConfigureLog.yaml` to `LOGROOT/build/<role>`
after successful build; retain any available raw files on build failure.
Check the copied bytes with the previously reviewed exact experimental
warning/native-product notice policy and diagnostic grammar, including
the full bounded successful CMake configure YAML. `error.c.obj` progress
is a filename, not a warning. Any unlisted compiler/Kconfig/CMake/linker
diagnostic fails this mandatory gate; neither process exit 0 nor a
capability probe can grant a blanket waiver. Record file identities
for source/copied evidence. No canonical runtime recipe or parser limit
changes are authorized here.

## Verification before real canonical/CI execution

The real copied `test-all.sh` and fake child executables must show
7 PASS / 0 FAIL / 7 TOTAL in all mode and Stage 1 followed by ASCS
in bsim mode. A failing ASCS stub must increment aggregate FAIL,
not skip or mask Stage 1, and retain the output-root symlink/marker.
Repeat invocations must use distinct retained links. The unit and
coverage dispatch remain unchanged. Focused ResourceWarning-as-error
phase/ASCS runner/checker/native link tests, shell syntax and
`git diff --check` precede any real build or hosted CI attempt.

No full gate, new SDK build, hardware, stage, commit or push in this
design/implementation phase. Record any ordinary failing focused test
without removing mandatory children or weakening warning policy.

## Focused implementation checkpoint

`scripts/test-all.sh` now adds mandatory label `bsim: ascs-protocol` after
Stage 1 in `bsim` and `all` phases only. It allocates a fresh, retained
`/tmp/le-audio-ascs.XXXXXX/run` absent child path and, when a validated
`TEST_OUTPUT_DIR` exists, installs an exclusive `ln -sT` link before the
child. The trusted upload-artifact pin follows symlinks, so the hosted
BSim result root can retain original ASCS logs/images and source snapshots
without moving the actual native execution into home. Private test-all
`TMP_ROOT` cleanup does not touch this ASCS container. Aggregate failure
accounting continues after a failing mandatory child; no subset/skip flag.

Stage 1 still uses its original 17/26 scenario recipe, source/runtime
arguments, PCM oracle and limits. Its evidence `LOGROOT` and EXIT retention
trap now exist before SDK `compile.source` starts. Each build captures
the source build tree's `cmake.out`, `ninja.out`, correct sysbuild child
`.config`, and actual `CMakeFiles/CMakeConfigureLog.yaml` into an owned
`LOGROOT/build/{receiver,client}` before running
`scripts/check-native-bsim-build.py --log-root <LOGROOT> --role <role>`.
The bounded stdlib checker imports the independently reviewed
`inspect_warnings`, anchors SDK from `ZEPHYR_BASE`, requires the role's
resolved 32-bit native controller/coverage/assert/warnings-as-errors
configuration, hashes the four copied raw files and writes a separate
exclusive role verdict. It rejects missing/oversized/unlisted CMake,
Kconfig, compiler, linker, Ninja or successful configure-YAML diagnostics;
object build progress ending `error.c.obj` remains a valid filename.
Build failures retain whichever raw outputs already exist, instead of
waiting until after both builds to allocate logs. No SDK source, workflow
or canonical scenario was edited.

The real shell dispatch fixture reports `7 PASS / 0 FAIL / 7 TOTAL`
for all, and two ordered mandatory `bsim` children. Failing ASCS child
leaves Stage 1 event and its external result link/marker intact while
making the gate FAIL; failing Stage 1 still attempts ASCS and aggregates
both. Repeated all invocation creates a second link without replacing
the first. Unit and coverage phase events/counts remain unchanged;
fixture-only containers are removed after test, never real lab evidence.
Synthetic checker CLI tests cover receiver/client good profiles and
missing YAML, config drift, real compiler error and incompatible-linker
diagnostics through real file reads and public exit status. They do not
prove a newly run canonical/ASCS firmware matrix or hosted CI result.

ResourceWarning-as-error tests: `test_coverage_runner` 45/45,
`ascs_runner` 16/16, `ascs_results` 11/11,
`bsim_link_env` 7/7 (79 total), plus `bash -n` for both edited shell
scripts and `git diff --check` passed in this phase. A real clean
candidate gate, hosted heavy worker and commit/PR gate remain separate.
