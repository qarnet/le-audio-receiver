# PB-051 ASCS results evidence (2026-10-05)

This document records the retained, read-back evidence for the PB-051
ASCS lane as of 2026-10-05: the native parser baselines, the physical
pure-parser review, the earlier failed or cancelled matrix roots, and the
final accepted full six-family matrix r3. Nothing here is inferred; every
value below was read back from the retained record files at the cited
paths. Clean-commit and hosted-CI gates are explicitly **pending** and no
hardware was used for this document.

## Native public parser baselines (host, no board action)

- Baseline before the guard (unchanged installed SDK parser): the earlier
  pre-option state of `tests/unit/ltv_bounds` had no `PB051_LTV_GUARD`
  CMake option at all (the option was added together with the guard), so
  the baseline ran with the plain unmodified SDK parser; 6 pass / 1 fail /
  0 skip / 7 total,
  `METADATA_VALIDATION ret=0 delivered_entries=1 value=aa` (the off-by-one
  entry passes through the raw parser); west exit 1 expected and observed.
  Raw owner log `/tmp/opencode/pb051-ltv-native-resume-r1/build-and-run.log`,
  SHA-256 `fc23ad805a4d492149a99dcd2c3293deab7a965349603f33b8d4f43328e66b1b`.
  Today the same comparison shape is reproduced with the test-only
  `PB051_LTV_GUARD=OFF`, which must not be read as implying the option
  existed at baseline time.
- After the repository-owned per-entry LTV guard:
  12 pass / 0 fail / 0 skip / 12 total,
  `METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`;
  `TESTSUITE ltv_bounds succeeded`, PROJECT EXECUTION SUCCESSFUL.
  Raw owner log `/tmp/opencode/pb051-ltv-native-guard-r1/build-and-run.log`,
  SHA-256 `784bdfa23ca5be117d307ab1be9fa916aad64d3da0cda4de2dfb4deda61ade6b`.
  Source under test: `src/bt_audio_ltv_guard.c`
  (`__wrap_bt_audio_data_parse` forwarding valid entries to
  `__real_bt_audio_data_parse`), linked via
  `-Wl,--wrap=bt_audio_data_parse`.
  Native guard executable identities are preserved in Git history at
  revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`
  (`docs/development/pb-051-per-entry-ltv-guard-handoff-20261005.md`,
  dated native-link verification, distinct from the later ARM
  production proof below): final native executable
  `build/zephyr/zephyr.exe` carried both the real
  `bt_audio_data_parse` at `0x403bab` and `__wrap_bt_audio_data_parse`
  at `0x403c4a` with disassembly calls into the wrapper; native
  executable SHA-256
  `90d720996bb11fe5f9d787adb4fdad781e33cc7ca0e1b42ac0239970e32196ef`;
  intermediate `build/zephyr/zephyr.elf` SHA-256
  `cad0420376afe11474318a95f2ab5fc7b4d2ef9b2fdf610695f9cd01271d3f67`.

## Physical reviewed pure-parser run r4 (CPUAPP diagnostic image)

Board side: explicit candidate `158D8E1D` (VID 2886 / PID 0066, interface 2,
deepest matching USB sysfs parent, current tty `/dev/ttyACM3`), fresh CMSIS-DAP
identities before and after (DPIDR `0x6ba02477`; AP0/1 `84770001`, AP2
`32880000`, AP3 `00000000`; FICR PART `0x00054b15`, VARIANT `0x41414330`),
identical at posttest; retained proofs under
`/tmp/opencode/pb051-ltv-physical-guard-execution-r4/`
(`initial-identity.json`, `pre-reset-identity.json`,
`posttest-identity.json`, `posttest-readonly-proof.json`). The tty and USB
paths in this section are DATED raw identity evidence from that 2026-10-05
capture only, not a current probe-to-board mapping; never reuse a serial or
tty from old evidence, always re-resolve with fresh identity checks.

Image artifacts (prepared in `/tmp/opencode/pb051-ltv-physical-guard-r1/`,
owner log SHA-256 `17928e9db13046891d84a45e5d2b4ecaed64aab4abf45c60714eb875c8f9ec44`):

- `build/zephyr/zephyr.hex` 165910 bytes, SHA-256
  `54c236f21759d6326db427c149825599be7a5507c231655d100ff69c8439177c`.
- `build/zephyr/zephyr.elf` (ARM) SHA-256
  `f3514949805634306ed81ffdc4db8cd2954b04cfb0edebbb94836cdb4fc29425`.

Capture: raw unmodified UART file `uart.log`, 3823 bytes, SHA-256
`7f9c4f4e26379a1b8dea12bdff90c7ef1c715262b6dc51cfabd14af9aeaef639`. The
whole file contains a 64-byte prior-run tail (`[0,64)` SHA-256
`82fd810705a3f7de7169e78456d50ed0e0bdc485b262d926bbae2239a120edcb`); the
fresh boot window is `[64,3823)`, 3759 bytes, SHA-256
`64ff257f5e9f992343d5133fcffa60328f299c05e127a88befd1b0796da8f03a`.

Result: 12 unique `START`/`PASS` lines, summary
`SUITE PASS - 100.00% [ltv_bounds]: pass = 12, fail = 0, skip = 0,
total = 12`, `METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`,
single terminal `PROJECT EXECUTION SUCCESSFUL`, no fault or warning in the
raw window. Review verdict
`/tmp/opencode/pb051-ltv-physical-guard-execution-r4/reviewed-physical-verdict.json`
SHA-256 `4f986206b060e90bd035309675cd36fb38a21f7d52edff122d44e193f9f1f5db`,
`accepted: true`, six negative controls (missing-case, fail-one,
wrong-guard, extra-boot, truncated-terminal, fault) all `rejected: true`
with semantic reasons. The original failed verdict
(`native-capture-verdict.json` SHA-256
`11e6b4d2eb32ff379c6969b9fcfcb56dd4b73c053b9bb69bc31e86c86b2ad22a`,
accepted false because a generic FAIL regex matched the valid
`fail = 0` summary) is preserved unmodified, not rebaselined.

Acceptance boundary: physical CPUAPP public parser API 12/0/12 only. No
physical ASCS, RF, I2S, codec quality, analog or FLPR acceptance is claimed.
The diagnostic parser image remains on the board; any later physical work
requires a fresh identity and role provisioning.

## Earlier matrix roots (all immutable, none supersede r3)

| Root | Suite-record SHA-256 | accepted | cancelled | run_id |
|---|---|---|---|---|
| `pb051-owned-full-matrix-r1` | `e2cf1f6dd9eecc4446f39c5f5daa9b8b85a09f9b8dd50945a7a46ccba6acbe2b` | false | none | `03bfe0ebac654fe883441a8f1a4ce8a6` |
| `pb051-owned-full-matrix-r2` | `a1ece653b093e4056ed1396205be1fb6dd529a168f74740937d6a936fb7080eb` | true | none | `0f6e3024ced843f481dcca2633dca6f4` |
| `pb051-owned-procedure-matrix-r1` | `0a79610553d9078f6df5f7aa8582c8dc751164fed8c4804cf49b863cffab5784` | false | none | `04c79016b24f4c7fbf537bcfc46f23b8` |
| `pb051-owned-audit-matrix-r1` | `8ce12f41cd757eaac8f1fc764e0012d4470e75464cf79cb529c8dc73d32b2746` | false | 15 | `bc13cb555c85444a9895a7359c772df8` |
| `pb051-owned-audit-matrix-r2` | `1ac67c47bddf9d1a697c4afd655f898150b187c538d54f345958b0168f676d6a` | true | none | `9c6654340a5d4c4a9cc51caec4c928c7` |

- `pb051-owned-full-matrix-r1`: failed before any family with
  `receiver: unlisted compiler/Kconfig/CMake/link warning`; no families
  recorded, runtime capture never reached.
- `pb051-owned-full-matrix-r2`: accepted full matrix (exact 60/65/259/269,
  18 zero-exit actors) on the runner source before the runtime-boundary
  review and before the later stream-ownership/audit runner and client
  repairs. Earlier source and earlier accounting contract: historical
  evidence of that pre-ownership code state only, not the current client
  or runner source and not current acceptance proof.
- `pb051-owned-procedure-matrix-r1`: five families accepted
  (control 7/7/21/21, metadata 13/14/67/67, codec 7/7/27/27, lifecycle
  19/23/91/91, dual 10/10/42/52) then reconnect generation 3 failed with
  client `-ENOMEM` before TX/render; the retained-audit allocation gap was
  later fixed by explicit retirement, not by any canonical change; old
  `suite-record.json` accepted=false, raw reconnect client log
  SHA-256 `202dd5f128eee536926b6f7cefde5755a2e0ebf60c4f38125ef9924532a03189`.
- `pb051-owned-audit-matrix-r1`: cancelled by signal 15 while staging its
  first three families; runner sealed one terminal record
  (accepted=false, `ValueError: operation cancelled by signal 15` three
  times) with control/metadata/codec_qos trace verdicts; not an accepted
  matrix and not reused per the exclusive-root rule.
- `pb051-owned-audit-matrix-r2`: accepted full matrix after the TX audit
  retirement repair and before the runtime-boundary call-site review and
  the post-r3 worker-cohort race repair; earlier than the current final
  runner source and labeled evidence of that intermediate source, not
  final-source acceptance.

## Final matrix r3 (2026-10-05, one authorized run)

Command (repo root, exact):
`env -u ZEPHYR_BASE nix develop -c bash -c 'ASCS_OUTPUT_ROOT=/tmp/opencode/pb051-owned-audit-matrix-r3 bash scripts/ascs-bsim-run.sh'`;
target root verified absent before launch (no clobber of any previous
root). Owner log `/tmp/opencode/pb051-owned-audit-matrix-r3.log`, SHA-256
`7c83232682276bee9053e3d7958f3398fb9bec4b0eb71f7bb3d033526dadf5b0`;
runner exit 0.

Sealed record `suite-record.json`, SHA-256
`479a5237f591b30b97c0a73e6b75e3e40e6b90c70ee42a25a87e69f0e87477f7`,
`accepted: true`, `cancelled_signal: null`, `errors: []`,
`cleanup_errors: []`, run_id `480d5bd5f6204df69479cc862d0586f8`, policy
digest `addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`.

Exact totals: 60 cases / 65 render phases / 259 raw exchanges / 269
response records. Family matrix identical to the table above and every
family's independent trace accepted: control
(`verdict_sha256 3cfc37d643a1741295636f345cbeacfad8feab4514c10ab22fdc3c946058d1cb`),
metadata `b37e11ea20fde7ffa5e023a16c79b2f5f14bb1e68b4c741f688c5b5e161d8cbe`,
codec_qos `a7bb3fcb88b7946f27d43df1339d8a212bc2206efb52df80b770c62e5a627741`,
lifecycle `fcb2b77e3f511d3941a1fea511d5da19f0375cbdd84ff2c6cfffc403abed5c5e`,
dual `33fb0465911ff8b55ba7b199f5a74b89e067c32737062c96c595e9c629f20694`,
reconnect `9d637f2c02cdc90ff75afa47f489386e539330b8a8c8f3dc68b17739a919497a`.

Raw per-family peer log hashes from the retained files:

| Family | client.log SHA-256 | receiver.log SHA-256 |
|---|---|---|
| control_frame_validation | `4046ce2dddc8f61f1b6f6d846c2c2bbc2d51ab00c03720ff843b14ed1ab022d0` | `642c4e369ad569757f2142d2e44d4ba360aebfb9e385b67c6afb2775dad1635a` |
| metadata_length_validation | `0e176060820e83212e2343559d631ce8935662b7833749893725101a5504accc` | `279bf8e33c26fc1f97e975161bcacc98a8a158cfc7631ad2aff1e2b5cf212a57` |
| codec_qos | `6eac0423f51b4d771f3f174fdc07d0f1321293a6bbdc7971a4f05d8192d821c6` | `f11dab5ede702ae6484fc066686f796b623c88f06b7c15eae0f4215328b80d6b` |
| lifecycle | `655a276efd3b0095876b88703eff737688a776cd078bdf82620db971a69fe49b` | `4979e3375b314d27d1d993fb17ef4200ecaf5310752ed11e6491e499c01f09e3` |
| dual | `8b5f080ce85b3e818abd2842897f7e71e5021d111baccb214539ab53e9eed729` | `d6190d6f2fc49e58857958b2a898439639e3624ba920c325116ca73ccfedda85` |
| reconnect | `fa943a4dc02c9047c6deed46cc076f6e6f7f306e7d0ec614e5e18a246dee0178` | `90782b1f4d83820d6888df081ab120ced2e596a8b86ff489d44b99923ea4c16a` |

images: client.elf SHA-256
`991fab96396d25c5e10046a1e304db15b03447f2162fb75488cd9a786713ed6e`,
`images/receiver.elf`
`5cc32900878060b4baab0fe3e627c0e91f570c86f2333f03c5259149cd511c7b`,
`images/phy.elf`
`5a6919e710a8811e70d10c9beb619a776cd797e23a943697893fe212f70bbda6`.

Runner-source note: r3 ran on scripts/ascs_bsim_run.py SHA-256
`ca974e1c2c082fbfd3b4b6eecb4a9d4200f46763239185a50472ce210577339d`.
After r3 a worker-cohort race repair (failure attribution for fast-exiting
workers) changed the runner to SHA-256
`811da2c768b75df0e976fe136247904765bcbe74b96ea18aaa043fe709acd821`; r3 is
therefore evidence of the pre-repair runner, not final-source acceptance.
The upcoming clean canonical gate must rerun the full matrix on the
committed race-fix source.

Review-required properties confirmed by direct record reads after the run:

- All 18 actor process records (`*-process.json` across six families)
  returncode 0, `ok: true`, no timeout, no cancellation, empty
  `cleanup_errors`, `descendant_cleanup_required` false; every cohort
  result accepted with `scope.ok` true and no unexpected live descendants;
  final suite scope clean.
- Actual PHY argv in every family job carries literal `-nodump`
  (for example `reconnect/phy-job.json` has
  `["-D=2", "-nodump", "-sim_length=250e6"]` after device, `-v=2` and the
  `-s=ascs_480d5bd5f6204df69479cc862d0586f8_<family>` session).
- 15 component headers (`libUtilv1` 11 + `libPhyComv1` 4) plus
  `FindBabbleSim.cmake` are inside the frozen `source-hashes.json` (150
  source entries total, snapshot copies under `source-snapshots/`).
- Runtime identity recorded once at capture and unchanged at every
  boundary class of this runner source: `runtime-ready:pre` right after
  the capture, `runtime-ready` after the readiness preflight,
  `cohort:<family>:pre` and `cohort:<family>:post` around each of the six
  cohorts, `final` in the success tail, and `post-run` in the execute
  finally (the earlier procedure-matrix runner source had only the
  final success check). Suite record `runtime` equals the same three
  library hashes
  (`libCryptov1.so` 4043672 bytes `c6cff2c6...cbf3bf1`,
  `lib_2G4Channel_NtNcable.so` 24576 bytes `7e925234...abe0a`,
  `lib_2G4Modem_Magic.so` 27976 bytes `8a7c205e...98dbe`); no
  `post-run runtime integrity` error exists. A vanished root or missing
  library would be reported by this same finally check instead of being
  silently skipped.
- Reconnect generation proof from the raw log: gen 1 teardown emits
  `stage=unused result_ret=-61 forget_ret=-61` for both indices; every
  later generation registers `ret=0`, exercises `stage=active
  forget_ret=-16` (EBUSY guard), sends 30 (partial streaming stream sends
  10, retained `ab61d129`), unregisters with retained result matching,
  completes `stage=retire result_ret=0` with the exact retained sends/FNV,
  `stage=forget ret=0`, `stage=forgotten result_ret=-61`, then
  `ASCS_CLEANUP retired=1`; generations 2, 3, 4, 5 all re-register fresh
  streams after explicit retirement with no `-ENOMEM`.
- Receiver rendered output for all four reconnect phases present
  (`ASCS_RENDER` count 4, summary `phases=4`).
- Warning envelope: build logs contain exactly the approved experimental
  Kconfig notices (client `BT_CTLR_CENTRAL_ISO`, receiver
  `BT_CTLR_PERIPHERAL_ISO` plus shared `BT_LL_SW_SPLIT`,
  `BT_CTLR_SET_HOST_FEATURE`) and the documented native SoC CMake notice;
  no compiler or linker diagnostic. Runtime receiver warnings are exactly
  the pinned case-local rejection sets (control 5, metadata 19, codec_qos
  0 with the four exact `Codec config rejected: code 0x08 reason 0x02`
  info lines plus 0x0903/0904/0905 response handling, lifecycle 11
  including the policy-pinned `Unknown ase 0x00`, dual 6, reconnect 0);
  no unlisted warning.

SDK identity, toolchain bundle, repo HEAD `e289bc6e9e` and dirty-inventory
digest `7380c2a4b024a5ba7cdf9a2560562c3f28594107d984fd1aedf20cb2a02c814f`
(status-listing digest only, content proven by per-file hashes) all match
the record.

## Focused verification commands (host-only)

All under `python3 -W error::ResourceWarning -m unittest discover -s <dir>
-p 'test_*.py'`, all OK:

- `tests/unit/ascs_runner`: 21/21 (includes the real-`execute()` lifecycle
  boundary test with ready/cohort/vanish hooks and the fresh fake SDK;
  `verify_runtime_identity` never mocked).
- `tests/unit/ascs_results`: 13/13 (including `phy missing -nodump`
  execution-record negative and the full TX-audit control set).
- `tests/unit/bsim_link_env` 7/7, `tests/unit/bluez_host_process` 11/11,
  `tests/unit/bluez_host_descendants` 4/4.
- `bash -n scripts/ascs-bsim-run.sh` and `git diff --check` clean.

## Additive coverage contract (2026-10-05, measured-data grounded)

The frozen baseline `tests/coverage-baseline.json` stays byte-for-byte
unchanged (SHA-256
`5bb01f95afc12c0771086a537cb70c92d20f7d96c8b9b4323528b6d9ed76de7a`). A
separate additive sidecar `tests/coverage-additions.json` pins newly added
sources with exact reference metrics measured from real instrumented code;
this is an independent strict non-regression contract, never a baseline
"refresh", never an exclusion, never a waiver.

Measured provenance (read back from the retained instrumented artifacts in
the delegator-owned root `/tmp/opencode/pb051-guard-coverage-r1`, dated
2026-10-05; a host instrumented measurement, not a physical or on-target
claim):

- Real west build of `tests/unit/ltv_bounds` for
  `native_sim/native/64` with `CONFIG_COVERAGE=y` plus an actual
  `zephyr.exe` run: build log SHA-256
  `a27301d070ac58e3d6c2b97c07d11c802b9997f840be1638d97d3f20b24ed067`,
  run log SHA-256
  `e7578bd0dc8249d13d72e6796a9989deb8a7cdc7dd8a24571b68446699959337`,
  12/0/12 ztest result retained.
- gcovr 8.4 invocation with `--include-internal-functions` over the
  retained build: `guard-coverage.json` SHA-256
  `e0815292de225af235e075a6c047b082b14e6d70a72bceabb7fc9dbb6dff33b3`,
  summary SHA-256
  `90198ee0ed9a0bbc33cadf23cfc0ccf2be72d4a79e885e2ec90925b6bd8bd861`;
  measured `src/bt_audio_ltv_guard.c`: lines 13/13, branches 12/12,
  functions 1/1 (all 100%), guard source SHA-256
  `d0f482830a7f9bf9d19490c800bf232675e179e441f75e46358db9f834f8a09a`.
- Toolchain: pinned GCC 14.3 / gcov (GCC) 14.3.0 / gcovr 8.4 (verified
  through the dev shell); only the documented native-only SoC CMake
  product notice plus the native test fake-entropy banner; no compiler or
  Kconfig warnings.

Why the earlier numeric summary showed 0/0/0 for the guard: gcovr 8.4's
default internal-function filter (installed
`gcovr/configuration.py` option `exclude_internal_functions`, exposed by
`--include-internal-functions` as its opposite;
`gcovr/exclusions/__init__.py` `_function_can_be_excluded`) drops every
function whose mangled or demangled name starts with `__`. The guard's
sole symbol is exactly `__wrap_bt_audio_data_parse`, so its whole function
coverage was filtered at trace collection. This corrects the earlier
inaccurate claim that the guard had zero instrumentable logic: the per-entry
validation branches are ordinary instrumentable code; the data was hidden
by the name-based filter, not by any property of the logic itself. The
retained failed r2 run stays historical evidence.

Runner behavior after the fix: only the `ltv_bounds` suite trace relaxes
the filter; the final merge re-adds the flag so the already-relaxed trace
data survives into the reports; all other suite traces keep the default
filtering, so previously filtered historical internals are not restored
and the frozen population's measured metrics are unchanged. The baseline
enforcement accepts an optional `tests/coverage-additions.json` sidecar:

- Strictly decoded (duplicate keys and non-finite constants rejected,
  one bounded non-follow regular read from the sole path
  `tests/coverage-additions.json`, never a second unbounded re-open,
  at most 64 KiB, empty file/dangling symlink/directory/oversized all
  explicit errors; no environment override is offered and only a truly
  absent path takes the legacy no-sidecar enforcement path); no silent
  fallback to ignoring it.
- Exact schema: int 1 (not bool), exact anchor equal to the actual
  SHA-256 of the supplied frozen baseline file, nonempty `files` with
  `src/*.c` keys disjoint from the frozen population, metrics exactly
  lines/branches/functions with `[covered, total]` int pairs,
  0 <= covered <= total and total > 0.
- Required current population becomes frozen UNION additions; a missing
  original, a missing addition or a further unknown new source all fail.
- Frozen-population overall AND per-file ratios are computed only over
  current frozen-population records, so a perfectly covered additive
  source can never mask a regression in the frozen 36; missing records
  are errors, never skipped.
- Additive sources must have present, nonzero, valid current metrics with
  the exact reference totals and ratios (the measured guard reference is
  13/13, 12/12, 1/1); no 0/0 pass, no automatic population growth, no
  missing-sidecar exception.
- The run manifest records the sidecar's SHA-256 (or null); the separate
  frozen-population and additive-sidecar displays make the combined
  totals in the numeric summary truthfully attributable.

## Clean canonical gate and production build (commit 33310f0, 2026-10-05)

The local clean gate is now **complete** at exact final-source commit
`33310f0f37069221872bc17ba131b6893d832e2f` (`PB-051: preserve frozen
coverage and enforce instrumented guard`). Only the hosted CI run on PR 16
remains outside this document.

Committed source identity: the runner carries the race-repair source
(`scripts/ascs_bsim_run.py` SHA-256
`811da2c768b75df0e976fe136247904765bcbe74b96ea18aaa043fe709acd821`) and
`scripts/test-coverage.sh` SHA-256
`91e29a87ee50d162cae134ff7ae108c33bcdbd42077993fce3712709046308c1`
(post-cleanup form with context-managed reads and the single-snapshot
baseline anchor); `tests/coverage-additions.json` SHA-256
`30814793c111583030bcdba24485d0c14a56b4c47e3429e66808aee4024b3166`;
post-repair runner-suite source `tests/unit/test_coverage_runner/
test_test_coverage_runner.py` SHA-256
`8b1279de249af164739027bfb7aabff04d7d2ef7a827c975fc1c5a9c9e48d807`.

Clean candidate: `/tmp/opencode/pb051-clean-candidate-r3` (detached worktree
at exactly that commit; tracked/untracked status clean before the gates;
source bytes verified against the commit).

Whole canonical gate, once, from the candidate workdir, exclusive
noclobber raw log `/tmp/opencode/pb051-canonical-r3.log` (SHA-256
`0bba01fe0d3dbca3fadcb8d7432689a4ae20d0f56c2bc3cbe0bcebbb44aa5aad`),
`GATE_EXIT=0`:

- `Gate complete: 98 PASS / 0 FAIL / 98 TOTAL` and every child green.
- Frozen population coverage (separately displayed alongside the additive
  group, `numeric frozen-population ...`):
  lines **5049/5491**, branches **2245/3036**, functions **378/378**,
  equal to the frozen numeric metrics with the frozen baseline file still
  byte-for-byte SHA-256
  `5bb01f95afc12c0771086a537cb70c92d20f7d96c8b9b4323528b6d9ed76de7a`
  and `baseline enforcement: 0 error(s)`; the combined truthful totals are
  5062/5504 lines, 2257/3048 branches, 379/379 functions over a 37-file
  population.
- Additive sidecar enforced at exactly the measured values: lines **13/13**,
  branches **12/12**, functions **1/1** for `src/bt_audio_ltv_guard.c`
  (guard source SHA-256
  `d0f482830a7f9bf9d19490c800bf232675e179e441f75e46358db9f834f8a09a`),
  recorded in `run-manifest.json` (`coverage_additions_sha256`) and
  `numeric-summary.json` (`addition_sidecar`, sidecar SHA-256, separate
  `frozen_population_totals` / `additive_sidecar_totals` groups) with the
  internal-function-filter disposition pinned as
  `relaxed-only-for-ltv_bounds-trace`.
- `matrix: manifest + coverage.json` PASS with `0 error(s), 0 note(s)`.
- `bsim: stage1` PASS: unchanged 17 scenarios / 26 runs, strict-checked.
- `bsim: ascs-protocol` PASS with the complete current-source matrix
  sealed at `/tmp/le-audio-ascs.cicaWA/run`; terminal `suite-record.json`
  SHA-256
  `f0c8d2c7f53494bfebcb8fca14634b92ff07508a8cdc878e2ed753106ef341ca`,
  accepted=true, run_id `f567e010d7f0458c873a0aa2112bc46c`, zero errors and
  zero cancellation, exact totals **60 cases / 65 render phases / 259 raw
  exchanges / 269 response records**, all six families accepted
  (control 7/7/21/21 `c261d2a18048...`, metadata 13/14/67/67
  `910977b8c3df...`, codec_qos 7/7/27/27 `cb00fd4e4616...`, lifecycle
  19/23/91/91 `1159e531f1fb...`, dual 10/10/42/52 `159b1d8f46d6...`,
  reconnect 4/4/11/11 `c46a30aac8e1...`), clean-commit worktree (dirty
  status inventory digest `e3b0c442...b7852b855` = empty status listing)
  and unchanged runtime identities (`libCryptov1` `c6cff2c6...374c`).

Production build proof (BUILD ONLY, no flash, same clean candidate):

- `fw-build-54l15` exit 0; raw log
  `/tmp/opencode/pb051-canonical-r3-fw-build.log` SHA-256
  `6103590b912dfc0f74e1be85e20539063f20f1758a650216a414ddada3d1efd8`
  containing exactly the two approved dispositions (`warning:
  Experimental symbol BT_CONN_TX_NOTIFY_WQ is enabled.` and the CMake
  informational `__ASSERT() statements are globally ENABLED`) and no
  other compiler/Kconfig/linker/runtime warning.
- Final images/maps (rehashed from the actual paths): CPUAPP
  `build/nrf54l15/pb051-clean-candidate-r3/zephyr/zephyr.elf` SHA-256
  `70f01610a45583625ce131851e7a0a297c7626a821ae9ae3b6e1662fd8786a77`,
  map
  `d8cc22928fc67bb1f00bfc091d718ca1b78612129c735b98bd63a48fde2d1c03`;
  FLPR `build/nrf54l15/flpr/zephyr/zephyr.elf` SHA-256
  `62ba69d41cc142db729d2f1ba0d032d21d4db33bae4facf7825248f75e64caaf`,
  flpr map `e7e88c88...188e9` (FLPR build retained).
- Build contract: `python3 scripts/check-build-contract.py --nrf54l15
  build/nrf54l15` from the candidate, exit 0, raw log
  `/tmp/opencode/pb051-canonical-r3-build-contract.log` SHA-256
  `b405f421936422c61c17fea5829a63ed0b6db657a1bd86b8196f464db876a7db`,
  `73 assertions, 0 failed`, `BUILD CONTRACT PASSED` (including
  `CONFIG_BT_CONN_TX_NOTIFY_WQ` stack/priority and
  `CONFIG_WARN_EXPERIMENTAL=y` retention).
- Actual ARM wrap proof, external log
  `/tmp/opencode/pb051-canonical-r3-wrap-proof.log` SHA-256
  `383d6f8d4d1ac6d795a3c6e7c54004a76eda67f08d97527aa24812bd29f6ab39`
  (actual installed `arm-zephyr-eabi-objdump` GNU 2.43.1 from the pinned
  toolchain bundle on the rehashed final CPUAPP ELF, actual tool output;
  not fabricated): `nm` shows `__wrap_bt_audio_data_parse` at `0x0001a72c`
  and the real SDK parser `bt_audio_data_parse` at `0x000477c4`; the
  wrapper's disassembly contains its inner `bl 477c4
  <bt_audio_data_parse>` (at wrapper offset `1a74a`, per-entry forward
  loop) and the production `src/bt_bap.c` `lc3_config` callsites at
  `0x19160` and `0x19222` execute `bl 1a72c
  <__wrap_bt_audio_data_parse>` - two real production callsites bound
  through the wrapper, not merely symbol presence. `zephyr.map` binds
  `.text.__wrap_bt_audio_data_parse` (0x0001a72c) and
  `.text.bt_audio_data_parse` (0x000477c4) exactly.

Earlier failed/cancelled clean-gate attempts stay immutable evidence:
checkpoint `af83365`'s canonical run at
`/tmp/opencode/pb051-canonical-r1.log` (SHA-256
`1aedc228a6a1a86aa30afc4932e35395d1fc89c66db1d3f72078444d52debc46`,
94/4/98, missing checker + missing manifest entry) and packaging
`a067d8a`'s run at `/tmp/opencode/pb051-canonical-r2.log` (SHA-256
`d941099c697822c3c6c938f2409dc7c9bb7bf504c91a5f9f9b0ec737f523cb54`,
97/1/98, frozen-baseline population conflict) plus their retained ASCS
records; the current-sidecar and filter semantics were produced by those
findings and verified by the 33310f0 gate (the r2 numeric guard 0/0/0 was
the gcovr 8.4 `__`-name collection filter, now corrected per the
correction note above; those runs remain historical, not acceptance).

## Explicit boundaries and pending gates

- The matrix is a host-executed protocol and lifecycle lane against the
  production receiver integration inside BabbleSim. Not established:
  physical RF, physical I2S/DAC output, LC3 conformance against
  independent vectors, FLPR offload behavior, release acceptance.
- The local clean-commit canonical gate (software) AND the production
  wrap-verified receiver CPUAPP+FLPR build (BUILD ONLY) are now complete at
  final-source commit `33310f0` (see the section above). The latest hosted
  CI on PR 16 remains **PENDING** from this section's original date; the
  2026-10-06 final section below records the actual completed state
  (historical text kept, not a current claim). Because
  the guard wrapper linkage changes production link behavior, any later
  physical use still requires a fresh identity, role provisioning and the
  usual safety boundaries.
- Earlier roots listed above remain documented as dated historical
  evidence (see the 2026-10-06 disclosure below for the one prelaunch
  ASCS container that the executor reported removing); no root is
  deleted, overwritten or reused by any later work.

## Final pass and source/version boundaries (2026-10-05/06)

The track completed through the existing PR gate. Final state, exact and
current:

- Latest passing hosted CI on PR 16 (PB-051 content combined with the
  owner-approved PB-045 prefix scope): head
  `2e642642b765fc27ade7dbefbf2e25c559022710`, run `37461484949`,
  all five required contexts SUCCESS (`test-unit`,
  `test-heavy (coverage)`, `test-heavy (bsim)`, `tests`, `firmware`),
  `release` SKIPPED (pull_request). The hosted bsim lane returned the
  retained receiver `build-warning-verdict.json` (artifact root
  `/tmp/opencode/pb051-hosted-bsim-37461484949`) accepted=true with
  exactly the four recognized capability-probe records (configure lines
  8886 static supported False, 8997 `-Wl,-N` False, 9042 orphan-warn
  True, 9082 orphan-error False), each carrying both exact pinned SDK
  source hashes.
- Local canonical gate: cold-cache native phase `Gate complete:
  2 PASS / 0 FAIL / 2 TOTAL` (raw
  `/tmp/opencode/pb051-cold-bsim-r1.log` SHA-256
  `48bac5c7e74b67ba804ca4eb8ab7e06787c4aa41c0d8f6ff16a3ee6dd83373dd`,
  sealed suite
  `8ce5546c1c9e77732bd333026da3f8de3a411193b9b08c41901aeacc0fd7a018`,
  run id `a251985ac7594862a6f38a4b75b50cdb`) and full
  `Gate complete: 98 PASS / 0 FAIL / 98 TOTAL` (raw
  `/tmp/opencode/pb051-canonical-r5.log` SHA-256
  `a067d6fe19b25fed0d65692e852d2d61d6fbdf258eedc1e52edc426df1309630`,
  sealed suite `4cf1f433d03e56e6419c8207df93915fab587bb069e6ad7183151
  fd9869f5c47`, run id `e992a189943b4759be45ebda889337ee`), both from
  the detached clean candidate at
  `e132fd274087fee3d78177b52304cfea20b96033` + the test-only
  portability commit. Every accepted lane run seals the exact declared
  matrix: 60 cases / 65 render phases / 259 raw exchanges / 269 response
  records across all six families; hosted run 37461484949's
  `ascs-le-audio-ascs.BMBtz5` suite record accepted=true, run id
  `74fd0f6aea774a0a8a7c9503cee72e79`, same exact totals.
- Source/version boundaries (unchanged, exact): production receiver
  sources are the `33310f0` build identity (CPUAPP
  `70f01610a45583625ce131851e7a0a297c7626a821ae9ae3b6e1662fd8786a77`,
  FLPR `62ba69d41cc142db729d2f1ba0d032d21d4db33bae4facf7825248f75e64caaf`,
  build contract 73 assertions 0 failed, GNU wrap proof at two real
  callsites); the SDK/toolchain pins stay Zephyr
  `33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6` / nrf
  `b20f8619ba9a5530f8c34b0a130d829947cfe55d` /
  bundle `8285d8ad56`; the frozen policy anchor
  `addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`
  and coverage
  baseline `5bb01f95afc12c0771086a537cb70c92d20f7d96c8b9b4323528b6d9ed
  76de7a` plus the additive sidecar
  `30814793c111583030bcdba24485d0c14a56b4c47e3429e66808aee4024b3166`
  are intact. LC3plus remains excluded; the frozen BSim Stage 1
  17-scenario/26-run recipe, HIL limits and PCM metric limits are
  unchanged.
- Agent-side completion is recorded and Done through the PR gate (the
  earlier sentence that criteria could only be checked after a human
  merge was misleading and is corrected by this section: the agent
  records Done through the PR gate, and the human product-owner PR
  merge remains the official, human-only acceptance act).
- Execution-deviation disclosure (truthful): one prelaunch cold-bsim
  attempt allocated ASCS container
  `/tmp/le-audio-ascs.sTgQ0p` was reported removed by the executor
  after the prelaunch fatal missing-output-dir abort; the container
  path is now absent on disk, so its contents cannot be re-verified
  and it is NOT claimed as a sealed failed record and never presented
  as retained acceptance evidence. It is not used for any acceptance;
  the canonical r5, cold-r1 and hosted 37461484949 records above are
  separate complete accepted evidence. The first attempt/retry report
  remains preserved transparently in the dated history; nothing was
  restored or fabricated, and no further roots or logs are deleted.

## Historical diagnostic provenance (2026-10-05)

This appendix preserves dated diagnostic identities (artifact paths,
SHA-256 hashes, observed values) carried by the retained PB-051 handoff
documents at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`
(cited per subsection below as historical Git-history citations, not
current-file dependencies). It restates those quoted facts so the
historical identities survive; it adds no new execution, rerun, or
recheck. Every path/hash below is a description retained from the cited
handoff text, read on 2026-10-08, and is NOT fresh artifact validation.
Accounting limits are stated per stage, not generalized: the early
one-case wire-baseline, wire-owned and focused-family diagnostic runners
did not record individual peer numeric exit codes (even where cohort
waits returned zero), while the later owned procedure and full-matrix
runs carry per-actor process records in their retained roots. Early
partial trials are not full matrix acceptance; later owned failed and
full runs should only be interpreted at their exact recorded source and
outcome. Execution order below follows the dated 2026-10-05/06 work
stages; it is not a permission or disposition statement about any
handoff document. Where the completed backlog note for PB-051 under
`docs/product/backlog/completed/` (file name starts with
`pb-051 - ASCS-protocol-rejection`) already documents an item in richer
dated context, this appendix cites
the handoff that carried the exact artifact identity and defers the
chronological narrative to that note; nothing is duplicated into a new
coordination report.

### Earlier physical pure-parser baseline (2/1/3)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-parser-resume-baseline-handoff-20261005.md`:

- Diagnostic image SHA-256 `14105caa24b1088e37187701fb59471ab13ab661389061b66f8c1ddff6f94496`.
- UART capture SHA-256 `d34754a6a67b36e78c31a4678367bf757a0a17b47ba8b7985a04e676696a677c`.
- SDK parser source identity: `source-hashes.json` pinned
  `.../subsys/bluetooth/audio/audio.c` SHA-256 `07b0b016ba518dbdb7f324c9948dbfd19619569f62f216aa75cde2e88e574d1d`.
- Result: standalone NCS v3.4.1 CPUAPP diagnostic reported 2 pass /
  1 fail / 0 skip / total 3; the exact-end missing-value case returned
  `ret=0 exposed=1 value=aa` for a padded, logically short input.
  Handoff also claimed `verdict.json`, `source-hashes.json` and
  `uart.log` under `/tmp/opencode/pb051-ltv-physical-baseline/` were
  present at that time (dated claim, not rechecked now). These are
  historical pure-parser observations, not current device identity, not
  a new flash, and not encoded ASCS execution.

### Wire baseline R1

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-wire-baseline-handoff-20261005.md`:

- Export-copy failure before client/peer launch; the original r1 export
  copy explanation was later corrected to a missing sysbuild export (see
  the source diagnosis row below); no MTU/request/response/state or
  recovery proof existed at this root.
- Owner records in `/tmp/opencode/pb051-wire-baseline-owner-r1/`:
  `process-record.json` SHA-256 `95f44cbbbedba8a3aa089a94875f8d00cf162468dc687c7153d6f045787e7c08`,
  `scope-record.json` SHA-256 `c4f0794de3b1b0595168284d731d1a82e671817f095dbc3ca9810cf966b5bcc1`.
- Receiver build logs retained then at
  `/home/thomas-workstation/ncs/v3.4.1/zephyr/bsim_out/tests/ascs_bsim/receiver/bs_nrf54l15bsim_nrf54l15_cpuapp_ascs_receiver/`
  (historical machine path only, not a universal setup instruction):
  `cmake.out` SHA-256 `2502259d2eebd47a33b54bc3b145eace51c32afc0e79703b0033ef1e5fe213e8`,
  `ninja.out` SHA-256 `d6ec420928f981140fd0054ff9d5b36383555eed281b50c90a076fc425f4bcb9`.

### Wire baseline R2 (before guard)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-sysbuild-export-handoff-20261005.md`:

- R2 hit the Kconfig 251-versus-69 range failure before peers launched;
  no wire proof. Corrected diagnosis: missing sysbuild export, not a
  background build race.
- Owner records in `/tmp/opencode/pb051-wire-baseline-owner-r2/`:
  `process-record.json` SHA-256 `a1a6dd073c9b7928077cf59677aa470e678322b67a2759f164dce609e2d4d046`,
  `scope-record.json` SHA-256 `1ae007bd992e61d0a0fa252e89a69f3107a2ee34c704ebc0cb6e3573531567b3`.
- Build artifacts in `/tmp/opencode/pb051-wire-baseline-r2/`:
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `b80b3635a521be539a08ec3bf1e24980fb05d2f5354326b08e5ff1fc91c76cfe`,
  `receiver.config` `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821`,
  `client-cmake.out` `025ef9d52dd8054f4441773e3b9e68f70805fae1b405ff911c61d9d155f3ac56`.
- Prechange source archive `/tmp/opencode/pb051-build-repair-prechange-r1/`
  (receiver `cmake.out`/`ninja.out`/`receiver.config` plus source
  path/hash `record.json`; record SHA-256 `e8630fda024566b808cc01ee2fed4d167a4a60cb9e6db5043a32ee410d817b93`);
  its cmake/ninja digests matched the first attempt and its config
  SHA-256 equals the r2 receiver.config value above.

### Wire baseline R3 (before guard)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-client-mtu-controller-handoff-20261005.md`:

- R3 pre-request client error `-5` for the
  metadata_length_validation case; the run ended before any encoded
  request, and the MTU negotiation outcome is unverified (no negotiated
  MTU value was logged; the handoff records "No logged negotiated MTU,
  raw encoded request, CP response, ASE state comparison, valid
  recovery, or ASCS verdict"). Exact failing client
  line: `d_01: @00:00:10.636924 ERROR:
  (CMAKE_SOURCE_DIR/client.c:534): ASCS_CLIENT
  case=metadata_length_validation error=-5 assertions=0 phases=0`
  followed by `d_01: @00:00:10.636924  The TESTCASE FAILED (test return
  code 2)`. Receiver retained its independent `255/251/65/255` config
  values in the same order (ACL RX 255, controller 251, L2CAP 65, ISO
  255).
- Owner records in `/tmp/opencode/pb051-wire-baseline-owner-r3/`:
  `process-record.json` SHA-256 `9f50297dcc92dcaead2090fec0f1a67465e7f9a4662c7acc7add88332b85dc24`,
  `scope-record.json` SHA-256 `7796fff1b02fbe10870ea9315f3b217f178a9768f1200cc8a866c03f674d5d01`.
- Build/raw artifacts in `/tmp/opencode/pb051-wire-baseline-r3/`:
  `receiver-cmake.out` `48cf45d6cc951c397676a4c755d8e5c6133e080274cda19d09bfa91444e546e4`,
  `receiver-ninja.out` `fad901acd6a179b25a31ea0e241dc8deee077f72fbf519f5e2917f2df184e13a`,
  `receiver.config` `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821`,
  `client-cmake.out` `61a5aee67de12e586d0946ac161fe4e9da8d9a288ee7936430a04c1c62a5d849`,
  `client-ninja.out` `7bda6c2b2f724a3c53c88e9116cda05df8fd2529f5d0594306c15455f68577fd`,
  `client.config` `e41bf8290281f90e6d421bb56cdd2828bb9e56830926db3d13314497bc91250b`,
  `receiver.log` `4e876a7802e3d57a596ae06aadc19f59457c26e2c5868d4ee4f1da3e80f673ce`,
  `client.log` `6e0ce0c877d41eb33f14e0410677307702c3d80083723c5c32fb38ef5f61dd8d`,
  `phy.log` `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`
  (empty file hash).

### Wire baseline R4 (before guard)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-cp-subscription-handoff-20261005.md`:

- R4 actual request returned success where rejection was expected
  (client error `-74`, `ASCS_CP generation=1 raw=0301010000` at
  `00:00:07.176844`, opcode 03 success 00/00 rather than required
  rejection 0c/00); no preservation or recovery proof. The PHY log is
  empty (same empty-file SHA-256 as above), the runner returned 2, and
  no individual PHY exit code was recorded.
- Owner records in `/tmp/opencode/pb051-wire-baseline-owner-r4/`:
  `process-record.json` SHA-256 `89eee346bf16b51ece8caa709322cf2d62d0e8316494bbda1307ffe511fd3485`,
  `scope-record.json` SHA-256 `7b4f43edb3759e5bbf7f6bb5d12ee7327ac66c3b4c3963eaa98a90f01ff3279d`.
- Build/raw artifacts in `/tmp/opencode/pb051-wire-baseline-r4/`:
  `receiver-cmake.out` `48cf45d6cc951c397676a4c755d8e5c6133e080274cda19d09bfa91444e546e4`,
  `receiver-ninja.out` `47021c5e96cb065cb880554ed0b18e9a70c06bbb0ce9f26b42f2148e11ea5e66`,
  `receiver.config` `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821`,
  `client-cmake.out` `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc`,
  `client-ninja.out` `ac104cecb668ca4b90fd0790db9dadd7132b069a4e4f125fe76fbd5414396a44`,
  `client.config` `e41bf8290281f90e6d421bb56cdd2828bb9e56830926db3d13314497bc91250b`,
  `client.log` `0b0e93a054d5821f10eff62b99849fa62dedb35c56004a8824e9f58a5928c520`,
  `receiver.log` `2d4f20c637a41550c0cb5e1e61fd844bc2b60c8219278c21439cf5f0a2e5bc`,
  `phy.log` `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

### Wire-owned R1 (post-guard early diagnostic)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-wire-ownership-ledger-handoff-20261005.md`:

- Valid Enable returned helper CP notifications labeled transaction 0;
  both ASEs reached state 4; the independent TX audit logged 30
  successful sends each with unregister code 0; receiver logged
  `ASCS_RENDER phase=1 pushes=27`; the receiver sink checked nonzero
  separate-channel energy, distinct stereo samples, correct sample
  geometry and no bad pushes. Pair these values only with the
  pre-adjustment client image below; a client source change followed
  R1 and canonical notes already record that the R1 image does not
  cover the changed source. The R1 helper used transaction-zero CP
  labels that the later procedure-owner repair superseded; retain this
  run as early one-case diagnostic context only (also not a timeout,
  cancel, reconnect or full matrix acceptance).
- Owner records in `/tmp/opencode/pb051-wire-owned-owner-r1/`:
  `process-record.json` SHA-256 `c10c1140e10ace99370d62fcea7a666d3e969adedb078010056b940fcf3996d3`,
  `scope-record.json` SHA-256 `063f68b67f2650e70388c524964c01c048d6347747ab094ffd10119c3af12c1e`.
- Raw images/logs: `/tmp/opencode/pb051-wire-owned-r1/client.log` `20a4b74748a2ce0865cfe4fd0159e080785bb5e8e258edd6f86a1eeaae30402d`,
  `receiver.log` `adba0e6ad5c06f593d27229921a7aeddccb0669833e32bafc1efe3829cb6151d`,
  `phy.log` `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`;
  receiver image `f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`;
  client image `8a0ca5134ca5d2aa2794199045c53f64210ea76185f986557cf931f643ef4066`.

### Wire-owned R2 (post-guard second one-case diagnostic)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-wire-ownership-ledger-handoff-20261005.md`:

- Receiver image identity stayed `f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`;
  the client source had been adjusted between R1 and R2 (canonical
  notes record the required fresh verification for that change; this
  run is not full matrix acceptance).
- Owner/source records in `/tmp/opencode/pb051-wire-owned-owner-r2/`:
  `process-record.json` SHA-256 `ba4b3358217f4024978817e1a67ae0442a41f37b565502b6f08e1936bf929a2e`,
  `scope-record.json` SHA-256 `bab8835eb7b4a9819c70021d65691e5f1ef6c4697fc68ec04e8552e32114d67c`,
  `source-record.json` SHA-256 `e525bd93e824c68fab4aa3351ae958b0f1be7ade57132b9bd8c837f69736fdeb`.
- Raw logs/build outputs in `/tmp/opencode/pb051-wire-owned-r2/`:
  `client.log` `5da1d0b192b1063e7fcf98fe3633c62558e72ec49e7d44fe8c6cb82e665910c1`,
  `receiver.log` `adba0e6ad5c06f593d27229921a7aeddccb0669833e32bafc1efe3829cb6151d`,
  `phy.log` `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`,
  `client-cmake.out` `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc`,
  `client-ninja.out` `63e8d70c4be1e62a88153c1dd1879098abe063824c0c1b42700db4b615c0db4b`,
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `7ed22269b70d53dab9e1c014b213356384ebc60bad593db22595ec5e7cc0b46a`.

### Focused family matrices r1 (post-guard; metadata also r2)

- Accounting limit for this early focused-family diagnostic runner: it
  did not record individual peer numeric exit codes; each run's child
  waits completed successfully (zero) except where noted. All focused
  runs in this subsection used receiver image SHA-256
  `f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`
  (it binds the receiver image for every focused family diagnostic
  below). The two completion
  timestamps below are the recorded historical wall times of those runs
  and are not new limits.

- Metadata r1 from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-metadata-matrix-handoff-20261005.md`:
  completion at 66.187224 seconds of simulation within the existing
  240-second ticker; one unexpected `metadata_updated` warning despite
  client exit zero (the observer repair r2 followed). Owner records:
  `process-record.json` `e819d64c5344df85c6f28c01e94cbca0bd2ce1357bc75052e56c25f1c8739500`,
  `scope-record.json` `1ecb8f460fecd9a24e9e7c5b851903621f749eb0f4734f422992f92d69ef7fcb`,
  `source-record.json` `ca10c9882efe340c9edac4d83e637ef1002226435f88ba3abcced8e00a86fe52`
  in `/tmp/opencode/pb051-metadata-matrix-owner-r1/`. Raw/build:
  `receiver.log` `279bf8e33c26fc1f97e975161bcacc98a8a158cfc7631ad2aff1e2b5cf212a57`,
  `phy.log` (empty) `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`,
  `client-cmake.out` `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc`,
  `client-ninja.out` `8df207c4a1b4f1507e6833c7e201b93f4f2ae3c45037c1205c3ba3b55e64bd11`,
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `20f3529d6fd96c97f350b11494620ba0779eefa6031f02a8a046bbcd1bbe3bfa`
  in `/tmp/opencode/pb051-metadata-matrix-r1/`.
- Metadata r2 (repair) from the same handoff: owner records
  `process-record.json` `b2b7e4b84e26877783d4104181b0be8ef82f97a0456b087537b2305a0edbc089`,
  `scope-record.json` `00c9c52e9a1f6a6504e9f9814e76fb09a436bb6de6dbfbdb5bdb39b06c4b1429`,
  `source-record.json` `c9068c10566a9bc029d7bb759aa6c8f5af747cca642a212a0a989a0b44155eac`
  in `/tmp/opencode/pb051-metadata-matrix-owner-r2/`; raw/build in
  `/tmp/opencode/pb051-metadata-matrix-r2/`:
  `receiver.log` `279bf8e33c26fc1f97e975161bcacc98a8a158cfc7631ad2aff1e2b5cf212a57`,
  `phy.log` (empty) same, `client-cmake.out` `1519e81606edb1fd161fe152c5f44a6db04d800cb21f21e8bbb90ba0b033eae3`,
  `client-ninja.out` `3ee5f451fa0f980b0c575b043e4577835856a57907ccd0b6974833c5953ceef8`,
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `9eea5e5a8e61a883191f70f0f6eb32a8a236587bbe1a35e92eef1c066cd2eced`.
- Codec/QoS r1 from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-codec-qos-matrix-handoff-20261005.md`:
  owner records `process-record.json` `847ce089b8239cd17fd6f7f5c247fcf05b37e8c5463f066324f5242379403c4b`,
  `scope-record.json` `0948bdaccafabc152df24876a36186b1f89f1f0b386aad3b7794b7a9c34bd4bb`,
  `source-record.json` `64d7d7611b4877ecfcd0c062afba248f41efdd7bf654a7fee76551f9e16cf3e4`
  in `/tmp/opencode/pb051-codec-qos-owner-r1/`; raw/build in
  `/tmp/opencode/pb051-codec-qos-r1/`:
  `receiver.log` `f11dab5ede702ae6484fc066686f796b623c88f06b7c15eae0f4215328b80d6b`,
  `phy.log` (empty) same, `client-cmake.out` `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc`,
  `client-ninja.out` `283f770f8bd0aa04ad1f190693fa786aba031d057aadb9c71a6f4f5dc64b9ad2`,
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `87695c332a72d37cb2c1d52aab268b535121282b6cf901c30c2a3b4f9f582827`.
  Public case counts 7/7/27/27 equal the frozen codec_qos family row.
- Lifecycle r1 from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-lifecycle-matrix-handoff-20261005.md`:
  completion at `00:01:53.157224`, before the original 240-second
  bound, with no simulator ticker increase. Public case counts 19
  cases / 91 raw exchanges / 23 response records equal the frozen
  lifecycle row. Owner records
  `process-record.json` `b56616efb8267cc1dcfb8f53b56497dd76651cd956ac4d7a2568dcad7b5f1efa`,
  `scope-record.json` `a061c853ef58796f4dd544fe693e2e86d930b8e3b00ea20a871adb95528d7e38`,
  `source-record.json` `d791ae6079fd9d9aff32ea2feff42e99c080dcf5bfeb183fcd811b75c334e6dc`
  in `/tmp/opencode/pb051-lifecycle-owner-r1/`; raw/build in
  `/tmp/opencode/pb051-lifecycle-r1/`:
  `receiver.log` `4979e3375b314d27d1d993fb17ef4200ecaf5310752ed11e6491e499c01f09e3`,
  `phy.log` (empty) same, `client-cmake.out` `00e7b31331668985e9dbef2180501b365e16c59f51a4e389f16a5a18b1659ecf`,
  `client-ninja.out` `83754c0f96163f061af0e9dd3a74576a587156c0f7c035855c4d0b77b2b2565f`,
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `b6cce7256c845cfe9b77a02e47e94f62b097f19e39a7f25ee42cbd92c723066b`.
- Dual r1 from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-dual-ase-matrix-handoff-20261005.md`:
  owner records `process-record.json` `be0288cf785fe338827c14ff8884ea86dec89ff68975ad58488bf73d0577a415`,
  `scope-record.json` `318d2808d670697b812466752d347611a523ab0248ce537cac1c46283a7d3f99`,
  `source-record.json` `44376b2441f44c7780e98ad735fe264b7f327e69d8fc06d85af09aa3f8465ff2`
  in `/tmp/opencode/pb051-dual-owner-r1/`; raw/build in
  `/tmp/opencode/pb051-dual-r1/`:
  `receiver.log` `d6190d6f2fc49e58857958b2a898439639e3624ba920c325116ca73ccfedda85`,
  `phy.log` (empty) same, `client-cmake.out` `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc`,
  `client-ninja.out` `e3bf137f53f20812b7e01c18d65f0bae6379cc00796df11224ad5c59f7965157`,
  `receiver-cmake.out` `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d`,
  `receiver-ninja.out` `aa5db2f5f4017d304623ff5af55d8ab529dd9addabbebebb3b774b7eb9fe64bb`.
  Public case counts 10 cases / 42 raw exchanges / 52 records equal the
  frozen dual row.
- Control framing r1 from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-control-framing-matrix-handoff-20261005.md`:
  owner records `process-record.json` `410ceca6ea8c9fe1a6545dd1555de311375077ce1a2a6bbd8b40924329cbdaf8`,
  `scope-record.json` `4696f3a71c37e14625a463dc363273d6fc094b399b0ddc0b1a04bd541f2ac732`,
  `source-record.json` `387ea24d707ce0f89d863cf9eedc7b290d7d04482bddfce49b8e30116f3bc2ec`
  in `/tmp/opencode/pb051-control-framing-owner-r1/`; raw/build in
  `/tmp/opencode/pb051-control-framing-r1/`:
  `receiver.log` `642c4e369ad569757f2142d2e44d4ba360aebfb9e385b67c6afb2775dad1635a`,
  `phy.log` (empty) same, `client-cmake.out` `49d5226d38e7fc6fd274b986cb4759d5ad8e5b5b0403e4a54f77989ceee36bc6`,
  `client-ninja.out` `4e55a810076fd1c9a0462e304f1076bbe1eccc1c7b9d8d18f921a05d782aae5e`,
  `receiver-cmake.out` `2502259d2eebd47a33b54bc3b145eace51c32afc0e79703b0033ef1e5fe213e8`,
  `receiver-ninja.out` `96b817649088f1b397d0f5303b908e13104bb690f797106c50043a8bbebceacc`.
  Public case counts 7/7/21/21 equal the frozen control row.
- Reconnect r1 from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-reconnect-matrix-handoff-20261005.md`:
  five distinct generation contexts (generations 1..5), owned CP
  retirement, public ACL disconnect, both-released callback barrier,
  group deletion, fresh discovery with MTU 65 and both Idle ASEs,
  partial A-CIS source acceptance of exactly 10 frames with unregister
  0 and no stereo or peer-delivery claim; public case counts 4 cases /
  11 raw exchanges / 11 records equal the frozen reconnect row. The
  old unicast group is deleted through the public API with only `-EBUSY`
  retried within five seconds; the client resets its endpoint cache and
  the connection/MTU/security/sink-discovery semaphores before each
  fresh connect. Owner records in `/tmp/opencode/pb051-reconnect-owner-r1/`:
  `process-record.json` `aa8f2494d78fb098c314f8a1b4546da906ff85671572f42449c0e0e7ac72c6af`,
  `scope-record.json` `56cb79d14f4f920a2b722054f1eefe6ace02477a26134a39867b1591dee57670`,
  `source-record.json` `e46e399db0487078a412d48f10d9f0be1fb50bee0a90d540629f093028f358f4`;
  raw/build in `/tmp/opencode/pb051-reconnect-r1/`:
  `receiver.log` `90782b1f4d83820d6888df081ab120ced2e596a8b86ff489d44b99923ea4c16a`,
  `phy.log` (empty) same, `client-cmake.out` `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc`,
  `client-ninja.out` `9fb140ee3f1226cc270685dd31b43dd0cfe12f1d8dc3071245a53e3b081a2969`,
  `receiver-cmake.out` `2502259d2eebd47a33b54bc3b145eace51c32afc0e79703b0033ef1e5fe213e8`,
  `receiver-ninja.out` `940693e7f2d21c8ed20344bbca2a87cf6211b5dc69b24bca5432f71d109e1a8c`.

### Failed procedure matrix (five-family partial)

Historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-stream-procedure-owner-handoff-20261005.md`:

- Five families ran (control, metadata, codec_qos, lifecycle, dual) with
  reconnect failing at generation 3 with client `-ENOMEM` before
  TX/render. Logged procedure `BEGIN` counts across those five were 56,
  113, 71, 234 and 86 respectively; helper counts 35, 46, 44, 143 and
  44; raw counts 21, 67, 27, 91 and 42. The owned client image then was `4edd4e87d2e631a8123f9f6ca560f013fc1440ba6df30fd9d1a017a3e13ae5e0`,
  receiver `2d76431521f2f437bedfe83a20863b9b079f668c748e1aaae27ddfa168ad0daa`,
  PHY `5a6919e710a8811e70d10c9beb619a776cd797e23a943697893fe212f70bbda6`.
  Raw client log SHA-256 `202dd5f128eee536926b6f7cefde5755a2e0ebf60c4f38125ef9924532a03189`;
  cohort result SHA-256 `4fb827087e9eb2ccbde57ce85299a53904a3c26b5029c8c7378f828b875a2b11`.
- Local procedure IDs in these logs are run-local diagnostics, not wire
  transaction IDs; ASCS notifications cannot cryptographically
  distinguish byte-identical delayed replies. This appendix repeats the
  limit alongside the guide's interpretation section.
- The observed `-ENOMEM` placement is consistent with a source-grounded
  fixture explanation; the individual failing call has no own raw
  return marker in that log, so callsite attribution there is
  source-grounded reasoning, not an independently measured callsite.
- Accounting limit for this later owned procedure runner: unlike the
  early focused diagnostics above, it retains per-actor process records
  in its root. Earlier handoff said "all nine process records"; that
  count described only some rows. The complete six-family actor count is
  eighteen, as recorded in the current lane guide and results; do not
  propagate nine.

### Native ABI smoke and linker environment (dated machine evidence)

Historical sources at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`: `docs/development/pb-051-native-probe-abi-handoff-20261005.md` and
`docs/development/pb-051-native-link-search-handoff-20261005.md`:

- R1 linker environment observed then: `gcc -m32 -print-file-name`
  reported `libc.so` under
  `/nix/store/yhawd8dka2563b5mg3vjm5h14sw5lv95-glibc-multi-2.40-224/lib/32`
  and a 64-bit `libgcc_s.so.1` under
  `/nix/store/yygma80xg8axc2df157lvdnf181zhx7s-gcc-14.3.0-lib/lib64`;
  the helper checked four ELF32 i386 glibc siblings and selected the
  verified GCC `/nix/store/yygma80xg8axc2df157lvdnf181zhx7s-gcc-14.3.0-lib/lib`.
  These are observed evidence paths from that date, never production
  hardcodes.
- R1 review finding (fixed later): the original fixed GCC query captured
  output in memory before checking its 64 KiB limit and accepted
  nonempty stderr below that limit; the current bounded contract is in
  the lane guide's resolver section.
- R1 focused public-boundary controls: ResourceWarning-as-error unit run
  seven methods passed; new negative controls reject exit-zero compiler
  stderr and space-containing paths; authored compiler fixtures flood
  stdout/stderr in repeated 4096-byte writes or stall with a live child
  and each is rejected with no live descendant left.
- r2 smoke root `/tmp/opencode/pb051-native-link-smoke-r2`: authored
  `main.c`, separate full compile/run stdout and stderr, ELF32 i386
  executable, `record.json` with real search directories, old/new flags,
  header and exit codes; `gcc -m32 -Wall -Werror` compile/link and
  executable returned 0 with zero bytes on each captured stream; ELF
  header `7f454c4601010100000000000000000003000300` confirms 32-bit
  little-endian i386. Neither canonical nor ASCS matrix was rerun, so
  actual receiver/client Ninja warning removal was unverified.
- ABI smoke evidence from historical source at Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`, `docs/development/pb-051-native-probe-abi-handoff-20261005.md`:
  compiler ID YAML declares `Build flags: -m32` at
  line 4374 and `C_COMPILER_SUPPORTS_WFORMAT_SIGNEDNESS` uses `-m32` at
  lines 7356 and 7367-7368; `file -L` identifies the compiler ID probe
  and the outer receiver executable as ELF32 i386; the one-shot
  smoke-record SHA-256 is `42b3c9ca5aa6f7cc9f18791828c71e14364af0190682072917994dcd126d42f3`.

### Historical individual peer exits and empty PHY logs

Across the early one-case wire-baseline, wire-owned and focused-family
runs above (each documented in its own subsection with its cited
historical source), the then-current diagnostic runner recorded no
per-peer numeric exit codes (even where all child waits returned zero);
the individual statements
`child waits returned zero`/`no separate numeric per-peer exit-code
records` appear verbatim in each cited handoff. The later owned
procedure and full-matrix runs are different tools and do carry per-actor
records. The `phy.log` hash
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` is the
empty-file SHA-256 and recurs because those runs captured a zero-byte PHY
log; the same value appears in the reproduced Git-history citation tables
(at revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`) from, among others,
`docs/development/pb-051-codec-qos-matrix-handoff-20261005.md` and
`docs/development/pb-051-dual-ase-matrix-handoff-20261005.md`. Early
partial trials are not full-matrix acceptance; the later owned failed and
full runs (procedure r1, full r2, audit r2, and the clean-gate and hosted
records) should be interpreted only at their exact recorded source and
outcome. All path/hash pairs in this appendix bind evidence identities as of
2026-10-05 and were not rechecked against external artifact availability
during this update.