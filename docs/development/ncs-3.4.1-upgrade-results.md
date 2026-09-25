# PB-040 NCS v3.4.1 upgrade: current integration evidence (2026-09-25)

The integration diagnostics below were recorded before the clean verification
at the end of this document. They are retained as dated evidence, not replaced
by later local results. PB-013 edits remain separate; no ARM execution, hosted
CI, hardware qualification, exact-artifact acceptance or release is claimed.

## SDK and historical corpus boundary

- Active NCS workspace: `~/ncs/v3.4.1`, nrf
  `b20f8619ba9a5530f8c34b0a130d829947cfe55d`, Zephyr
  `33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`.
- Active toolchain pin `8285d8ad56` (GNU 14.3 ARM/RISC-V, Zephyr SDK 1.0.1),
  sdk-manager 1.16.1. The official container digest
  `sha256:45b97cad97a9967c52d77d1d1a0f7dd8fe027edd17c05c3eda2eeadc23729418`
  was registry-verified only: no hosted CI execution is claimed. Old v3.3.0
  workspace, historical evidence and active private draft assets are untouched;
  project root `VERSION` remains `0.1.0`. No flake.lock, helper-input,
  user-owned nix-nrf-dev tree or SDK source update was made.
- liblc3 checkout: `48bbd3eacd36e99a57317a0a4867002e0b09e183`,
  semantic label `1.1.2`, unchanged from historical generation.
- Both checked-in fixture manifests retain `ncs_version: v3.3.0`, original
  source formula, flags, recipes, PCM limits, and binary hashes. The stateful
  manifest continues to bind the byte-identical portable manifest. Calibration
  workspace checks now demand v3.4.1; manifest checks still demand historical
  v3.3.0. Host report schema 3 keeps `ncs_version` for the active workspace
  and adds `fixture_ncs_version` for original corpus provenance.
- NCS v3.4.1 `nrf/VERSION` has six version fields, unlike the old single-line
  `3.3.0` file. Python, CMake, and stateful-generator workspace checks validate
  the installed field layout and pinned Git revisions. CI guards actual
  `nrf/VERSION` SHA-256
  `37eaf3af83e09aba188263cedf48d2bc606713193a61df78d532730f3441cfa5`.

## LC3/PCM calibration slice verification (no flash)

- `env -u ZEPHYR_BASE nix develop -c python3 -m pytest -q tests/unit/lc3_pcm_calibrate`:
  **40 passed**. Covers rejected active 3.3.0 workspace, rejected manifest
  relabeling, wrong liblc3 revision, retained corpus, and separate report fields.
- `env -u ZEPHYR_BASE nix develop -c bash tests/fixtures/lc3/generate.sh`:
  **PASS**, legacy and portable manifest hashes unchanged; strict mode only.
- `env -u ZEPHYR_BASE nix develop -c bash tests/fixtures/lc3/generate_stateful_references.sh`:
  **PASS**, stateful manifest hashes unchanged; strict mode only.
- `env -u ZEPHYR_BASE nix develop -c python3 scripts/lc3_pcm_calibrate.py
  --output /tmp/opencode/pb040-lc3-calibrate-20260925.json`: **PASS** after
  correcting the initially assumed single-line VERSION format. This is an
  external, new-file host replay; report records `ncs_version: v3.4.1`,
  `fixture_ncs_version: v3.3.0`, portable manifest SHA-256
  `f82c85fed3097b6943b2d75733f7a377d0beb71a0a79fc5566ac8ed76bc7ba11`,
  and stateful manifest SHA-256
  `2c931ef6c3afc81081583c73bf429543c876cebf2e2166e0d43f4b4674b2519a`.
- `git diff --check`: **PASS**. No fixture manifest or binary appeared in
  `git diff --name-only -- tests/fixtures/lc3` or fixture-specific git status.

No hardware action, baseline update, or PCM-policy change performed in this
slice. ARM calibration build-only evidence is recorded below.

## Primary build repairs (dirty-tree diagnostic, 2026-09-25)

- NCS 3.4.1 `zephyr/soc/nordic/Kconfig` defines `NRF_PLATFORM_LUMOS` as a
  deprecated, default-on compatibility alias with no SDK consumers. Seven
  target configs explicitly disable that alias while retaining the real
  `SOC_SERIES_NRF54L` selection. `src/audio_i2s.c` and its unit expectation
  use the equivalent non-deprecated I2S controller clock options from
  `zephyr/include/zephyr/drivers/i2s.h`; audio transfer behavior is unchanged.
- FLPR mapped RRAM code partition at `0x165000` still spans `0x18000` bytes.
  NCS 3.4.1 omits `FLASH_LOAD_SIZE` for mapped partitions. Contract 54l15-033
  now requires resolved `USE_DT_CODE_PARTITION=y`,
  `FLASH_USES_MAPPED_PARTITION=y`, `FLASH_SIZE=96` (KiB), and correct chosen
  partition reference plus absolute DTS size. Checks 54l15-030/031/032 and
  all other assertions remain. Negative fixture tests reject absent/wrong
  size or mapped flags, and wrong DTS partition size.
- `python3 -m pytest -q tests/unit/build_contract/test_build_contract.py`:
  **54 passed**. Post-build actual checker
  `python3 scripts/check-build-contract.py --nrf54l15 build/nrf54l15`:
  **69 assertions, 0 failed**. `git diff --check`: PASS.
- Pristine `env -u ZEPHYR_BASE nix develop -c` builds via
  `fw-build-54l15`, `fw-build-hil-source-54l15`, and `fw-build-dongle`:
  **3/3 exit 0**. Full logs:
  `/tmp/opencode/pb040-{receiver,source,hci}-build-20260925-r2.log`.
  **0 compiler/Kconfig warnings or errors** in these logs. Each log begins
  with Nix's `warning: Git tree ... is dirty`, expected because upgrade
  worktree remains uncommitted; not a compiler or Kconfig warning.
- SHA-256 `zephyr.hex`: receiver CPUAPP
  `e02ab5f15213c05038d6e85cbcef585cf1a8db7f41b3653571dfcdb7f3a43aa6`,
  FLPR `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`,
  source CPUAPP `805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c`,
  HCI CPUAPP `ee0640e0de597aaec299f3a48ef74d37e2460b6ff92fb30f0de061556053c3b2`.
  No board run, SDK source edit, commit, or coverage baseline update.

## PB-040 BabbleSim dependency compiler policy (dirty-tree diagnostic, 2026-09-25)

- Stage 1 uses `bs_2G4_phy_v1` plus receiver and client, not every upstream
  BabbleSim device. The pinned `ext_2G4_phy_v1/Depends` closure is
  `libUtilv1`, `libPhyComv1`, `libRandv2`, `ext_2G4_libPhyComv1`. CI now builds
  that target through `scripts/build-bsim-components.sh --force` after its
  existing pinned west population and environment guards. This changes only
  dependency-tool build scope, not 17 scenarios / 26 runs. Unused
  `device_time_monitor` is not compiled; its `tic_end` uninitialized path is
  not fixed or qualified.
- Dependency compiles and links use `-Werror` through
  `scripts/bsim-component-cc.py`, passed only as BabbleSim Make's `CC`.
  Exactly five upstream source-byte SHA-256 and canonical relative-path
  matches permit one diagnostic class each; changed audited bytes fail before
  compiler execution with `re-audit required`. No exception crosses into
  firmware, native units, BSim applications, or other SDK files:

  | Upstream component source | Pinned SHA-256 | Scoped exception |
  | --- | --- | --- |
  | `libUtilv1/src/bs_oswrap.c` | `0ff55f3d11d79892a5f8c7425f393fad8a17bd11ef96c05f17e10a844d05f869` | `-Wno-unused-result` |
  | `libPhyComv1/src/bs_pc_base.c` | `fa6d7926e16716e86d29f461c02650ff0b99b7d869c14118ad1248539d848b77` | `-Wno-unused-result` |
  | `ext_2G4_libPhyComv1/src/bs_pc_2G4_stateless.c` | `63a6f6ee4b462b485a4d4cb9b98f00812b928a06c624cff01565e7fef74e1163` | `-Wno-unused-result` |
  | `ext_2G4_libPhyComv1/src/bs_pc_2G4_stateless_wo_callbacks.c` | `b036d89380ec5ed5c1bd50f7ff24b946d23984d1afbae3181c957277f9efd600` | `-Wno-unused-result` |
  | `ext_2G4_libPhyComv1/src/bs_pc_2G4_utils.c` | `d75e783a86c96b954ae4f4f964eaadaa80a5b1a26c75992e853a5df9d27693c6` | `-Wno-maybe-uninitialized` |

- Rationale, not dismissal: upstream `libPhyComv1/src/bs_pc_base.h:67-74`
  already explicitly ignores `write()` results with a local pragma, as does
  `ext_2G4_phy_v1/src/p2G4_com.c`. Out-of-process FIFO robustness and `/proc` conversion
  failure gaps remain real; current controlled strict matrix does not qualify
  malformed `/proc` state, host IPC resource failure, or new invalid channel
  values. Frequency helper `p2G4_center_freq_from_ble_ch_nbr` leaves `freq`
  uninitialized for `ch_idx >= 40`, a real upstream bug. The current nRF model
  calls a different `p2G4_freq_from_d` path with valid channel inputs; this
  does not fix the helper. External fault-path repairs belong upstream, not
  in a local SDK-source patch. All other dependency warnings remain fatal;
  receiver warning policy is unchanged. Acceptance means explicitly audited
  scoped exceptions, **not** zero compiler warnings without qualification.
- `env -u ZEPHYR_BASE nix develop -c python3 -m pytest -q
  tests/unit/bsim_components/test_bsim_components.py
  scripts/test_firmware_build_ci.py`: **40 passed** (36 CI contract tests,
  four process-boundary wrapper tests). `env -u ZEPHYR_BASE nix develop -c
  bash scripts/build-bsim-components.sh --force`: **PASS**, fresh forced
  rebuild of 64-bit and 32-bit closure plus PHY link, no unwaived compiler
  warnings in `/tmp/opencode/pb040-bsim-components-scoped-20260925-r1.log`.
  First attempt with top-level `-B` failed on forced directory/Depends rules;
  second with child `-B` failed on upstream missing-library error targets;
  final `--force` uses virtual `-W` on all closure C sources in recursive
  component makes, without deleting old SDK output or evidence.
- Strict Stage 1 rerun `BSIM_LOG_ROOT=/tmp/opencode/pb040-bsim-scoped-20260925-r1
  bash scripts/bsim-stage1-run.sh`: **PASS**, unchanged oracle, 17 scenarios /
  26 runs, all three peers exit 0 each run. Summary log:
  `/tmp/opencode/pb040-bsim-stage1-scoped-20260925-r1.log` and preserved
  per-scenario logs at output root. No hardware, baseline, SDK source patch,
  or commit. Hosted CI and clean-commit acceptance remain parent follow-up.

## Full integration gates and warning boundaries (dirty tree, 2026-09-25)

- Native unit suite: **76 pass**. HIL Python suite: **340 pass, one intentional
  hardware skip**. Three pristine physical builds and resolved 69-assertion
  contract are detailed above. Generated compile-command links now point to
  v3.4.1; CMake/tools own them. No firmware test waiver was added.
- Coverage: `/tmp/opencode/pb040-coverage-20260925-r1/full.log` records all
  **46 suites pass**, report-only on a dirty tree. All 36 population files,
  ratios and counts match the committed baseline exactly: 4971/5427 lines,
  2203/3008 branches, 377/377 functions. Baseline unchanged; this is not
  clean-tree baseline enforcement.
- `native_sim` host-only builds emitted one NCS CMake product-support notice
  per build across 46 host builds, from `nrf/cmake/device_support.cmake:34`:
  `SoC native is not supported by this release.` `native_sim` is a legitimate
  Zephyr host test target, not a Nordic production SKU. Retain the raw notice;
  this classification applies only to host-only `native_sim` and is not a
  compiler/Kconfig warning or general waiver. Native fake-entropy warnings
  are existing test-only banners, not a reason to change production entropy.
  All three physical builds have **zero compiler/Kconfig warnings**, with the
  verified unused deprecated `NRF_PLATFORM_LUMOS` alias disabled per target,
  not with disabled warning reporting. I2S MASTER-to-CONTROLLER bits are
  equivalent; the DTS-mapped FLPR load-size contract was updated to match
  resolved partitions, not weakened.
- ARM LC3 calibration image: **296 build steps completed**, no ARM test execution;
  `/tmp/opencode/pb040-arm-calibration-20260925-r1.log`. Neither ARM execution
  nor flash nor physical audio is claimed. Strict manifest regeneration and
  host replay retain original v3.3.0 fixture bytes and provenance; calibration
  tests remain **40 pass**, as detailed above.
- HCI UART compatibility: NCS v3.4.1 driver core functions remain unchanged;
  audited full-sentinel generated patch is guarded against original source
  SHA-256 `d68f45fbef9da8077efe6c9f94c609393fc3485bd1d486e4f710288f6d808bd3`.
  Exhaustive byte-value proof: original 3/8192 mismatches, generated 0/8192.
  This addresses old-slot `0xAA` false replacement, not the still-open physical
  HCI failure. No SDK source file was patched on disk. No upgrade claim for
  UART physical qualification, RTT, PB-013 360-frame feature, RH4/FR4,
  publishing, or the provenance of historical release assets.

## Local environment and remaining verification

Enter a fresh shell via `env -u ZEPHYR_BASE nix develop` if currently in a
v3.3.0 shell; a new-login `nix develop` is fine. Do not mix old `ZEPHYR_BASE`
with the new toolchain. During NCS v3.4.1 dependency fetch, the Nix west wrapper
initially contaminated the SDK Git/libcurl runtime. Failed dependency attempts
`deps-r1`/`deps-r2` remain evidence; `deps-r3` succeeded with a scoped
invocation of SDK Python `-m west`, `LD_LIBRARY_PATH` limited to the
`8285d8ad56` toolchain's `usr/local/lib`, and `GIT_EXEC_PATH`,
`GIT_TEMPLATE_DIR`, `PYTHONHOME`, `PYTHONPATH` unset. This is a local setup
recipe/helper-tooling limitation, not an SDK patch or extra OS package; do not
edit the user-owned nix-nrf-dev tree. The strict new dependency helper log is
`/tmp/opencode/pb040-bsim-components-scoped-20260925-r1.log`; the strict
Stage 1 summary is `/tmp/opencode/pb040-bsim-stage1-scoped-20260925-r1.log`.
The five source/hash-specific BabbleSim exceptions above describe real upstream
error-path gaps, not false positives; `-Werror` stays on for the dependency
closure and no exception leaks into firmware tests.

The preceding integration evidence was diagnostic because that tree was dirty.
No hardware operation, SDK patch, release operation, or commit was part of that
slice. Subsequent clean verification follows.

## Clean local software verification at `daf7cd9` (2026-09-25)

- Separate validation clone was clean before and after the canonical gate at
  `daf7cd9404e32bacbff4b6431dafccbd28e4a8eb`. Full log
  `/tmp/opencode/pb040-clean-canonical-daf7cd9-r1.log` ends **80 PASS / 0 FAIL /
  80 TOTAL**: 41 Twister, five exec-only, 31 Python, coverage, matrix and
  strict BSim Stage 1 (17 scenarios / 26 runs; unchanged oracle and PCM limits).
  `/tmp/opencode/pb040-clean-canonical-daf7cd9-r1/coverage/run-manifest.json`
  records `mode: baseline`, `dirty: false`, exact source commit and all suites
  OK. Every one of the 36 population baseline pairs is **IDENTICAL**;
  4971/5427 lines, 2203/3008 branches and 377/377 functions. Committed
  coverage baseline was not changed.
- Clean HIL Python result: **340 passed, one intentional hardware-opt-in skip**;
  `/tmp/opencode/pb040-clean-hil-daf7cd9-r1.log`. Strict regeneration of LC3
  manifests and reference bytes retained the original v3.3.0 corpus. Actual
  host LC3 replay report `/tmp/opencode/pb040-clean-lc3-daf7cd9-r1.json`
  (execution log `/tmp/opencode/pb040-clean-lc3-daf7cd9-r1.log`) records active
  NCS v3.4.1, original `fixture_ncs_version: v3.3.0`, schema 3, original
  portable manifest SHA-256
  `f82c85fed3097b6943b2d75733f7a377d0beb71a0a79fc5566ac8ed76bc7ba11`
  and stateful manifest SHA-256
  `2c931ef6c3afc81081583c73bf429543c876cebf2e2166e0d43f4b4674b2519a`.
  liblc3 revision `48bbd3eacd36e99a57317a0a4867002e0b09e183` remains
  unchanged. ARM calibration completed 296 build steps; no ARM tests executed.
- After the canonical pre/post-clean run, three pristine physical-target builds
  passed (receiver CPUAPP + FLPR, standalone source CPUAPP, HCI CPUAPP).
  `/tmp/opencode/pb040-clean-{receiver,source,hci}-daf7cd9-r1.log` and
  `/tmp/opencode/pb040-clean-build-contract-daf7cd9-r1.log` retain the raw
  builds and resolved checker **69 assertions, 0 failed**. Image `zephyr.hex`
  SHA-256: receiver CPUAPP
  `716d43fe57b5af2ed1bc8fec9197c9e07bd81f5cab88673cdc8d4aa5fac700bd`,
  FLPR `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`,
  source `805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c`,
  HCI `c2108956760a770e45d8bf52f86736c0bc7da3fa881410c6248414446d88a1d9`.
  Builds produced only generated `compile_commands.json` symlink changes in the
  clone (root, `src/flpr/`, `hil/source/`, `dongle/hci_uart/`), so no false
  post-build clean-tree claim; the checker log's Nix dirty-tree notice is from
  that generated state, not a compiler/Kconfig diagnostic.

### Warning classification and remaining boundaries

- Three physical builds have **zero compiler and Kconfig warnings**. Each
  retains one CMake message `__ASSERT() statements are globally ENABLED`.
  Fresh NCS v3.4.1 source inspection of Zephyr `CMakeLists.txt:2356-2359`
  shows it is intentionally emitted iff `!CONFIG_TEST && CONFIG_ASSERT &&
  !CONFIG_FORCE_NO_ASSERT`. All three development/acceptance images keep
  assertions on as fault guards. This exact informational configuration
  diagnostic also existed in the old SDK's builds; it is not an unexplained
  new warning, a compiler/Kconfig warning, or a reason to disable assertions.
  No filtering or general warning waiver; inspect other CMake warnings
  separately. The host-only `native_sim` notice `SoC native is not supported
  by this release.` remains separately classified above and present in raw
  host logs. Five upstream BabbleSim dependency compiler exceptions remain
  limited to the exact source paths, hashes and classes listed above, with
  `-Werror` elsewhere; **do not claim zero warnings across every dependency**.
- SDK v3.3.0 remains installed side by side. Active NCS v3.4.1 nrf and Zephyr
  revisions are pinned above; sdk-manager 1.16.1 was upgraded for this SDK;
  toolchain bundle `8285d8ad56` and repository `flake.lock` were not changed.
  CI workflow contract tests passed (36); the official container digest above
  was registry-verified only. No hosted CI execution, flash, board test, RTT
  change, user-owned nix-nrf-dev edit, PB-013 360-frame feature, physical HCI
  qualification, RH4/FR4 acceptance, draft-asset change or public release.
  Root `VERSION` remains `0.1.0`. Software upgrade implementation has clean
  local verification; PB-040 awaits human PR acceptance in Review, not Done.
