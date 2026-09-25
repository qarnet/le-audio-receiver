# nRF54L15 active-reference audit, 2026-09-25 (PB-037 AC5)

## Boundary and method

This is a reference-hygiene audit of the dirty primary repository, not a
clean-commit gate, physical HCI qualification, or PB-038 integration acceptance.
The current migration and physical evidence boundaries are recorded in
`nrf54l15-only-continuation-20260925.md` and `pb-037-retirement-results.md`.
No hardware action, firmware build, commit, backlog status change, or baseline
rewrite was performed for this audit. Prior fixed-image passes do not erase the
later production-HCI error: the final six-case repeat stopped on Mode A with
H4 parser `-71`, hardware error `0x07`, one I2S underrun and one stream reset.
The traced diagnostic image passed six cases but does not qualify the production
image. Clean coverage enforcement is blocked until an authorized clean commit;
PB-038 remains Backlog. No RH4/FR4, analog, release, or final migration
acceptance follows from the scans below.

Literal inspection covered active root CMake/Kconfig/sysbuild and `prj.conf`,
`boards/`, `hil/`, `dongle/`, `tests/bsim/`, `scripts/bin/`, workflow files,
production `src/`, remaining `scripts/`, public README and docs, product task
files and test documentation. Search terms included `nrf5340`, `nrf53`,
`E83`, `ebyte`, `cpunet`, `hci_ipc`, retired helper names and `APLL` (plus
case variants). Inspected current files rather than deleted Git paths; ignored
`build/`, `.cache/`, `.codebase-memory/`, `.session-checkpoints/`, `.git/` and
generated compile-command links. Those outputs and private evidence are not
live source, are retained rather than deleted, and cannot establish a current
target. Literal substring `e83` inside a SHA-256 digest is not a board match.

## Active-selection check

No positive nRF5340 receiver board or CPUNET app selection was found in
active root configuration, `boards/`, `hil/source/app` configuration,
`dongle/hci_uart` configuration, either BSim peer, `scripts/bin/` or workflow
files. The old E83 board, `hci_ipc` controller, nRF53 source branch and receiver
build/flash helper are Git-deleted, not active buildable targets. Remaining
helper `scripts/bin/fw-flash-hil-source-54l15` *rejects* legacy CPUNET/J-Link
overrides. Current `Kconfig` ISO omission text describes the older SW Split
observation; `tests/bsim/prj.conf` notes that BSim has no physical APLL.
Retained generic no-offload stubs are selected by `!CONFIG_SOC_NRF54L15` in
`src/audio_offload.c`, not by a positive nRF5340 build. Current BSim links
`src/audio_timing_none.c` as its nRF54L15BSim no-op backend; physical nRF54L15
uses `src/audio_timing_nrf54.c`. The production actuator is NONE; historical
APLL lives under `tests/unit/actuator_apll/` and
`tests/unit/actuator_apll_nohfclk/`. This audit updates comments/docstrings in
specified source, central and historical-test files only; executable code and
macros remain unchanged. Pre-existing dirty-tree source edits are not part of
this hygiene slice.

## Remaining literal-reference classes (after prose follow-up)

| Class | Specific remaining paths / meaning |
| --- | --- |
| Unsupported-target regression and input rejection | `scripts/test_firmware_build_ci.py` forbids old workflow names; `tests/unit/build_contract/test_build_contract.py`, `tests/unit/bsim_target/test_nrf54l15bsim_contract.py`, `tests/unit/hil_source_target/test_hil_source_target.py`, `tests/hil/rh2_test.py`, and `tests/hil/rh4_artifact_test.py` inject old boards, J-Link/nRF53 or CPUNET to ensure rejection. `scripts/test_hil_runner.py` includes negative legacy probe-family fixtures. The old strings here are test *inputs*, not current selection. |
| Retained test-local history | `tests/unit/actuator_apll/`, `tests/unit/actuator_apll_nohfclk/` and `tests/unit/actuator_sample_adjust_historical/` preserve historical actuator behavior without production selection. `tests/unit/audio_i2s_identity/prj.conf` now calls out generic host-only identity regression, `tests/unit/timing_none/CMakeLists.txt` describes current nRF54L15BSim no-op direct test, and drift test metadata uses a `clock_recovery` tag. None selects nRF5340 hardware. |
| Current headers and external reference patterns | `dongle/hci_identity.h` now labels the unchanged lab BD_ADDR for XIAO and identifies the old DK zero-FICR motive as historical, without claiming XIAO silicon has zero FICR. `hil/source/app/src/hil_source_controller_time.h` documents direct GRTC and actual NULL/not-initialized/not-ready errors, not a mirrored network-core epoch. `hil_source_tx.h` calls Nordic's iso_time_sync/nrf5340_audio patterns external SDK references; `hil_source_app.h` preserves frozen 3000/2000 us timing, explains historical nRF53 IPC-margin origin and current direct GRTC clock. Headers/macros and executable bodies were not changed. |
| Historical documents, not current execution recipes | `docs/design.md`, dated `docs/development/` plans/results (including RH3 and PB-033/PB-037 records), `docs/testing/pre-refactor-hardware-baseline.md`, `v0.0.1-baseline.md`, `t1-flpr-production-tests.md` and `t4-bap-bsim-matrix.md` preserve old targets, hashes and old-run commands. `STATUS.md` and amended development plans distinguish historical checkpoints from current work. `docs/testing/behavior-contract.md` now labels historical CLOCK-001/CLOCK-010/BUILD-002/dual-target BUILD-007 clauses and prepends a current-path amendment; `docs/testing/coverage-matrix.md` corrects APLL, Mode A, no-op timing and current diagnostic population/checker while labeling old rows as dated; `docs/testing/t3-audio-i2s-tests.md` retains original T3 67/65 evidence and old commands beneath a historical banner, explicitly separating today's identity host test. Old measurements are not recast as nRF54L15 evidence. |
| Baseline and inactive local outputs | `tests/coverage-baseline.json` still lists retired `src/audio_clock_actuator_apll.c` from population 37. Report-only diagnostic population is 36; committed baseline is deliberately untouched pending clean-commit enforcement. A gitignored `tests/hil/fixture.local.json` can contain old nRF53/J-Link concrete bindings; active defaults/examples and session-bound role checks replace it. Do not use stale local binding or treat it as current default. Old generated build artifacts, if present, are inactive and not acceptance evidence for current source. |
| Public historical receiver and external context | `README.md` explicitly calls E83 receiver retired and quotes Nordic's nRF5340 platform recommendation as context. `docs/technology/nrf5340.md`, historical sections of `docs/hardware-wiring.md` and `docs/flashing.md`, and `release/flashing/nrf5340-e83.md` retain old facts while marking procedures not runnable. `docs/technology/nrf54l15.md` and `docs/known-limitations.md` relay external platform caveats. `docs/supported-sources.md` and `docs/bluetooth-adapter-evaluation.md` discuss third-party adapter candidates, not supported repo receiver/controller builds. All ten links in README Documentation table resolve to present files. No new public em dashes added. |
| Product-owned items | `docs/product/backlog/tasks/pb-018 - Validate-nRF5340-DK-as-Linux-HCI-UART-adapter.md` is **Backlog external adapter research**, not an active Linux HCI replacement; PB-019 owns current XIAO HCI migration and remains unqualified after the latest production-image error. PB-020 includes nRF5340 Audio DK among external USB transmitter candidates. PB-033 describes the earlier retained-fixture phase; PB-034/035/036/037/038/039 retain problem statements, historical implementation notes and acceptance text without making old boards active. PB-009 excludes an nRF5340 release ZIP; completed PB-032 and archived PB-005 retain earlier policy/history. No product status, archive, description or acceptance-criteria edit was made. |

## Disposition

No residual *executable* nRF5340 board/controller/receiver dependency was
identified by this source/config/helper/workflow literal check. Remaining
literal mentions are classified by exact historical evidence, external SDK or
third-party adapter context, unsupported-target test input, or product-owned
old proposal, not hidden under "all docs historical". PB-018 remains Backlog
external research; no archive or product-owned field was changed. Stale local
fixture data and historical old-run tables are not active selection.

Pending work is the production-image HCI defect and requalification, explicit
commit authority for a clean canonical gate/baseline, and PB-038 lifecycle
plus old-proposal owner decisions. This reference audit does not prove final
build, clean canonical gate, HCI six-case qualification or PB-038 acceptance.
