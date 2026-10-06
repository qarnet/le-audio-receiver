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
  `__real_bt_audio_data_parse`), linked via `-Wl,--wrap=bt_audio_data_parse`.

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