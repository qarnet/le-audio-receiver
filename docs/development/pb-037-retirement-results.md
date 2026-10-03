# PB-037 receiver retirement: first two implementation slices

## Scope and evidence boundary

The E83 board tree, receiver helper commands, nRF5340 net-core sysbuild branch,
production identity/APLL choice and source link were removed in the first
slice. No existing nRF54L15 receiver transport limit or audio algorithm was
changed. The second slice retires the nRF5340 build-contract fixture and
receiver test-matrix witness. It does not rebuild or flash firmware while the
fixed-image physical matrix is running. Packaging, release flashing, public
guidance and the exhaustive active-reference audit remain later PB-037 work.

## Build-contract test disposition

| Disposition | Coverage |
| --- | --- |
| Retired, six target-specific tests | `test_sw_split_kconfig_only_half`, `test_sw_split_dts_only_half`, `test_5340_acceptance_parity_inversion`, `test_5340_feature_on_control_fails`, `test_5340_feature_on_input_fails`, `test_5340_wrong_system_workqueue_stack`. Each checked only the deleted nRF5340 receiver/net-core contract. |
| Retargeted generic tests | Config set/unset, comments versus real nodes, chosen HCI reference/status/compatible, I2S pin encoding, host/controller ISO count agreement, multi-error reporting, input errors and dynamic default-domain lookup now exercise the nRF54L15 app and FLPR fixture. Existing nRF54L15 memory, FLPR, clock, GPIO, pairing and source negatives remain. |
| New rejection | The `--nrf5340` CLI option now fails argument parsing; a legacy root cannot be silently accepted. Resolved `CONFIG_SOC_NRF54L15_CPUAPP=y`, `CONFIG_BT_LL_SOFTDEVICE=y` and enabled Nordic SDC chosen HCI node are checked, including negative mutations. |

The first slice's `fw-build-54l15` produced
`build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex`, SHA-256
`9427913c9595f976cf1644d1ed857b6dc37ad0197fefa29dd4efa35d9e2427bd`.
This is software-build evidence, not physical acceptance. The second slice
reads that build's resolved config and devicetree without building again.

## Numeric coverage accounting

The production numeric population previously contained 37 sources. Removing
`src/audio_clock_actuator_apll.c` yields 36; no other production source is
retired in this slice. The old committed baseline still includes that path
with 15/15 lines, 4/4 branches and 3/3 functions. It is **not updated** on
this dirty tree. A clean exact commit and coverage run must establish the new
baseline and enforce the remaining population before acceptance.

Historical APLL conversion, both rails and the no-HFCLKAUDIO path remain in
`tests/unit/actuator_apll/src/audio_clock_actuator_apll_historical.c`, backed
by the unchanged eight APLL cases and one no-HFCLK case. This test-local source
does not enter the production `src/` inventory. `src/audio_timing_none.c`
remains in production inventory because the nRF54L15BSim receiver explicitly
links it as the simulator timing backend; it is not an E83 receiver fallback.
The generic I2S identity passthrough queue/lifecycle witness also remains.

## Validation still due

Run clean-commit coverage, full unit/build-contract/matrix/BSim gates and
applicable HIL smoke after remaining PB-037 cleanup. Fixed-image physical
results belong to their own immutable run records; this document does not
claim them complete.

## 2026-09-25 diagnostic integration boundary

Later diagnostic builds passed receiver, standalone source and HCI roles;
unit phase 75/0/75, resolved receiver build contract 69/0, canonical BSim
17 scenarios/26 runs and fixed-image RH3 matrix 20/20 passed. Report-only
coverage population 36: 4971/5427 lines, 2203/3008 branches, 377/377
functions, no zero-hit numeric functions. The committed baseline remains
unchanged. `nix develop -c bash scripts/test-coverage.sh --output
/tmp/opencode/nrf54-only-clean-coverage-20260925-r1` exited 1 **before
builds**: `FATAL: worktree is dirty — --write-baseline and baseline enforcement
require a clean exact commit`. No clean-gate pass or new baseline is claimed.
Exhaustive active-reference and historical-comment classification remains
unfinished; preserve historical material. Explicit local commit authority is
needed before clean exact-commit acceptance, not before continued technical
investigation. See `nrf54l15-only-continuation-20260925.md`.
