# PB-034 implementation handoff: migrate canonical Stage 1 to nRF54L15BSim

> **Historical PB-034 handoff:** Original low-latency policy, no-production-
> change constraint and reference-reuse assumptions below were disproved by
> primary-repository diagnosis. Integrated SW Split peers now use client ISO
> reliability policy; production Mode A repair and separate zero-start PCM
> references retain original limits and fault semantics. Read
> `docs/development/pb-034-primary-repair-results.md` before acting. Original
> evidence remains below unchanged; dirty-tree passes are not clean acceptance.

## Goal

Replace every active nRF5340BSim dependency in canonical BabbleSim Stage 1
with the NCS v3.3.0 target `nrf54l15bsim/nrf54l15/cpuapp`. Preserve all 17
scenarios, 26 runs, exact client TX hashes, PCM limits, explicit loss and
malformed-SDU injections, reconnect behavior, lifecycle behavior, failure
handling, and log ownership. Pin measured nRF54L15BSim-native startup history
without synthesizing legacy nRF5340 HCI-IPC delay or loss.

PB-034 must finish with one integrated CPUAPP image per peer. It must not
build or reference an nRF5340 CPUNET or `hci_ipc` child image.

## Grounded decisions

1. **Controller is integrated Zephyr SW Split.** Use Zephyr snippet
   `bt-ll-sw-split` for both peers. This supplies required devicetree selection:
   it enables `bt_hci_controller`, disables `bt_hci_sdc`, and chooses
   `zephyr,bt-hci = &bt_hci_controller`.
2. **Do not use nRF54L15BSim default SDC for Stage 1.** An uncommitted probe on
   current source built both peers under SDC and passed 12 of 17 scenarios, but
   failed all 10 ms two-CIS startup/reconnect variants when strict recipes saw
   an initial SDU without corpus payload. Physical nRF54L15 HIL remains SDC
   authority; PB-034 is deterministic software acceptance.
3. **Do not copy upstream broad Bluetooth Audio controller overlay.** With the
   SW Split snippet, that overlay enables unrelated periodic sync-transfer
   features and hits an NCS v3.3.0 compile defect in `ull_conn.c`. Use
   role-specific repository fragments instead.
4. **Rebaseline only target-dependent startup history.** Product direction on
   2026-09-23 selected measured nRF54L15BSim-native startup recipes after both
   default SDC and integrated SW Split proved that legacy 8, 11, and asymmetric
   Mode A PLC histories came from nRF5340 HCI-IPC startup timing. Add new recipe
   IDs and update startup totals, but retain every legacy recipe unchanged. Do
   not change fixture bytes, logical-sequence TX hashes, PCM limits, explicit
   loss/malformed injections, reconnect or lifecycle semantics, run counts, or
   process-failure rules.
5. **No production data-path change.** Do not modify `src/` behavior. BSim
   keeps `CONFIG_I2S=n`, identity resampler, and NONE actuator. Missing FLPR and
   I2S models are explicit simulator limitations already covered by physical
   HIL.

Authority:

- NCS board documentation:
  `/home/thomas-workstation/ncs/v3.3.0/zephyr/boards/native/nrf_bsim/doc/nrf54l15bsim.rst`
- SW Split snippet:
  `/home/thomas-workstation/ncs/v3.3.0/zephyr/snippets/bt-ll-sw-split/`
- Sysbuild native-simulator functions:
  `/home/thomas-workstation/ncs/v3.3.0/zephyr/share/sysbuild/cmake/modules/native_simulator_sb_extensions.cmake`
- Current PB-034 research evidence is recorded in PB-034 Implementation Notes.

### Target-native startup recipe rebaseline

Add these six recipes after all existing records in
`tests/support/lc3_stateful_recipes.c` and
`tests/fixtures/lc3/stateful-reference-manifest.json`. Existing recipe order,
definitions, generated PCM files, and hashes stay unchanged:

| New recipe | Exact actions | Existing reference |
|------------|---------------|--------------------|
| `start0_10ms_l` | corpus 0 through 99 | `bsim_48k_10ms_120b_l.pcm` |
| `start0_10ms_r` | corpus 0 through 99 | `bsim_48k_10ms_120b_r.pcm` |
| `start0_7p5ms_l` | corpus 0 through 99 | `bsim_48k_7p5ms_90b_l.pcm` |
| `start0_7p5ms_r` | corpus 0 through 99 | `bsim_48k_7p5ms_90b_r.pcm` |
| `skip20_start0_10ms_l` | corpus 0 through 19, then 21 through 100 | `stateful_48k_10ms_skip20_l.pcm` |
| `loss48x18_start0_10ms_r` | corpus 0 through 47, 18 PLC, then 48 through 81 | `stateful_48k_10ms_loss48x18_r.pcm` |

The first four records have 100 output actions and 100 valid frames. The
malformed record has 100 output actions and 100 valid frames. The explicit-loss
record has 100 output actions and 82 valid frames. Reusing the two generated
references is valid because pinned liblc3 startup PLC is suspended by the first
valid frame; removing only pre-first-valid PLC does not change later malformed
or explicit-loss decoder state. No new PCM bytes are generated.

Map every canonical nRF54L15BSim scenario to these new IDs in both
`tests/bsim/stage1-scenarios.json` and `tests/bsim/src/audio_sink_stub.c`:

- normal 10 ms mono, Mode A, reverse-start, and Mode B use `start0_10ms_l/r`;
- normal 7.5 ms mono, Mode A, and Mode B use `start0_7p5ms_l/r`;
- malformed resume uses `skip20_start0_10ms_l`;
- one-CIS loss uses `start0_10ms_l` and `loss48x18_start0_10ms_r`;
- lifecycle-prefix scenarios use `start0_10ms_l/r`;
- both reconnect segments use `start0_10ms_l`.

Full-length scenarios have these target-native totals by recipe construction;
the full gate must still confirm them:

| Scenario | `known.total` |
|----------|--------------:|
| `mono_10ms` | 100 |
| `mono_7p5ms` | 100 |
| `modea_10ms` | 200 |
| `modea_7p5ms` | 200 |
| `modea_reverse_start_10ms` | 200 |
| `modeb_10ms` | 200 |
| `modeb_7p5ms` | 200 |
| `invalid_sdu_resume_10ms` | 100 |
| `modea_one_cis_loss_10ms` | 200 |

Do not derive lifecycle-prefix totals by subtracting old startup PLC counts.
Integrated-controller timing can also change how many valid callbacks arrive
before fixed teardown operations. Measure first-run PASS records for
`modea_first_stop_10ms`, `release_without_disable_10ms`,
`disconnect_streaming_10ms`, reconnect segment 1, and
`duplicate_release_10ms`, then add those exact totals to this handoff,
`stage1-scenarios.json`, and parser mirrors. Reconnect segment 2 must complete
the full 100-action `start0_10ms_l` recipe.

Measurement sequence is deliberate: first add the six C recipe records and
their exact C validation mirror, raise binding capacity to 15, and map
`audio_sink_stub.c` scenarios to new IDs. Leave scenario JSON, Python manifest
authority, and parser expectations untouched for this probe. Device processes
then exercise target-native histories and emit complete PASS records; strict
parser mismatch is expected and must not be normalized. Retain logs and stop so
the Delegator can pin observed lifecycle totals before remaining manifest,
parser, test, and documentation changes. If a receiver process itself fails or
any full-length action history differs from the table above, stop and return
evidence.

Update exact mirrors in `scripts/lc3_pcm_calibrate.py`,
`scripts/bsim_stage1_parse.py`, and their unit tests. Increase the BSim recipe
binding capacity from 9 to 15. Calibration metrics gain six stateful-valid
records before the four existing mutation records. Keep all old record indices
and content stable by appending new recipes. Update active fixture documentation
to distinguish nRF5340 legacy startup records from nRF54L15BSim canonical
records.

## In scope

### Board and executable ownership

- Change `scripts/bsim-env.sh` default board to
  `nrf54l15bsim/nrf54l15/cpuapp`.
- Keep `BOARD_TS="${BOARD//\//_}"` executable derivation. Expected canonical
  binaries are:
  - `bs_nrf54l15bsim_nrf54l15_cpuapp_le_audio_receiver_bsim_prj_conf`
  - `bs_nrf54l15bsim_nrf54l15_cpuapp_bsim_client_bsim_prj_conf`
- Change `tests/bsim/testcase.yaml` allow/integration target to nRF54L15BSim.
- Change `tests/bsim/test_scripts/le_audio_receiver.sh` board token to
  `nrf54l15bsim_nrf54l15_cpuapp`.
- Change `scripts/gen-lsp-links.sh` BSim receiver/client database targets to
  nRF54L15BSim build directories.

### Single-image sysbuild

Apply same shape in receiver and client:

- Remove `NET_CORE_BOARD`, `NET_CORE_IMAGE_HCI_IPC`, nRF5340 board defaults,
  `ExternalZephyrProject_Add(hci_ipc)`, CPUNET configuration, and child-image
  assembly from:
  - `tests/bsim/Kconfig.sysbuild`
  - `tests/bsim/sysbuild.cmake`
  - `tests/bsim/client/Kconfig.sysbuild`
  - `tests/bsim/client/sysbuild.cmake`
- Keep `source "share/sysbuild/Kconfig"`.
- Keep final executable ownership with
  `native_simulator_set_final_executable(${DEFAULT_IMAGE})`.
- Propagate primary MCU index only to `${DEFAULT_IMAGE}`. nRF54L15BSim is one
  simulated MCU and index 0.

### Role-specific controller fragments

Rename nRF5340-specific fragment names to target-neutral names:

- `tests/bsim/overlay-bt_ll_sw_split.conf`
- `tests/bsim/client/overlay-bt_ll_sw_split.conf`

Receiver fragment must pin only receiver-required integrated-controller
behavior:

```conf
CONFIG_BT_LL_SW_SPLIT=y
CONFIG_BT_CTLR_ASSERT_HANDLER=y
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
CONFIG_BT_CTLR_ADV_DATA_LEN_MAX=191
CONFIG_BT_CTLR_PERIPHERAL_ISO=y
CONFIG_BT_CTLR_CONN_ISO_GROUPS=1
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2
CONFIG_BT_CTLR_CONN_ISO_PDU_LEN_MAX=251
CONFIG_BT_CTLR_ISO_TX_BUFFERS=3
CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE=255
CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX=255
CONFIG_BT_CTLR_ISO_RX_BUFFERS=4
CONFIG_BT_CTLR_ISOAL_SOURCES=1
CONFIG_BT_CTLR_ISOAL_SINKS=2
CONFIG_ASSERT=y
```

Client fragment must pin only source/client-required integrated-controller
behavior:

```conf
CONFIG_BT_LL_SW_SPLIT=y
CONFIG_BT_CTLR_ASSERT_HANDLER=y
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX=191
CONFIG_BT_CTLR_CENTRAL_ISO=y
CONFIG_BT_CTLR_CONN_ISO_GROUPS=1
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2
CONFIG_BT_CTLR_CONN_ISO_PDU_LEN_MAX=251
CONFIG_BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY=y
CONFIG_BT_CTLR_ISO_TX_BUFFERS=6
CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE=255
CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX=255
CONFIG_BT_CTLR_ISO_RX_BUFFERS=1
CONFIG_BT_CTLR_ISOAL_SOURCES=2
CONFIG_BT_CTLR_ISOAL_SINKS=1
CONFIG_ASSERT=y
```

Set `CONFIG_BT_ISO_TX_BUF_COUNT=6` in `tests/bsim/client/prj.conf` so host and
controller each provide six client TX buffers. NCS's own BAP unicast client SW
Split overlay pins both values to 6 because Number of Completed Packets arrives
one ISO interval later; two outgoing streams need three enqueued buffers each.
The earlier three-buffer client probe produced periodic source starvation,
continuous non-explicit loss, premature Mode A termination, and a
`modea_first_stop_10ms` send timeout. Do not rebaseline those fixture defects as
target-native receiver behavior.

Do not pace from `bt_bap_stream_ops.sent`. A bounded probe capped each stream at
three accepted sends awaiting that callback, but single-CIS startup still
produced a source hole, the hole moved with callback timing, Mode A first-stop
regressed to 44/45 sends, and the loss scenario no longer matched its
peer-delivery contract. NCS documents `sent` as implementation-dependent:
completion may mean enqueue, air transmission, or flush.

Also do not retain the greedy aggregate-buffer-driven loop. With official 6/6
buffer counts it submits logical packet sequence numbers faster than SDU time;
single-CIS traces then repeat six valid payloads plus one expired event, while
two-CIS round robin happens to stay aligned. SW Split enables
`CONFIG_BT_CTLR_ISOAL_SN_STRICT=y`: sequence numbers assign unframed SDUs to
fixed events, and stale payloads are acknowledged then dropped. Convert
`tests/bsim/client/src/bsim_tx.c` to an initial three-SDU prefill per active CIS,
then one shared SDU-period tick that sends at most one payload per active CIS
every negotiated 10 ms or 7.5 ms interval. Three is the host's fixed per-CIS
controller submission depth and the upstream SW Split requirement. Prefill must
round-robin active streams so two CISes consume exactly the six aggregate
buffers. A one-SDU initial fill was measured and rejected because it underfilled
the controller pipeline and produced later source holes. Keep `sent` for no
scheduling decision. Use separate logical and transport sequences.
Logical sequence selects corpus bytes, malformed injection, send count, and FNV
hash; transport sequence goes to `bt_bap_stream_send()` and advances every live
CIS interval, including deliberately omitted intervals. Implement the one-CIS
loss as a TX-owned scheduled gap after 48 successful right payloads for exactly
18 intervals, then resume right at logical frame 48. This preserves payloads,
hashes, and explicit peer-visible loss without completion-clock inference.

Grounding: `zephyr/include/zephyr/bluetooth/audio/bap.h` requires packet sequence
to advance at least once per SDU interval; `zephyr/samples/bluetooth/iso_central`
schedules exactly that interval; `zephyr/subsys/bluetooth/controller/Kconfig.ll_sw_split`
documents strict-sequence stale drops; and
`zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_central_iso.c` removes
expired payload counts. Do not disable strict sequence handling or rebaseline
periodic sender-induced loss as target behavior.

These values preserve two-CIS capacity, receiver RX count 4, and client RX
count 1. ISOAL source and sink pools are global: receiver must provide two sinks
for its two incoming CISes, while client must provide two sources for its two
outgoing CISes. This follows NCS v3.3.0
sample overlays
`zephyr/samples/bluetooth/bap_unicast_server/overlay-bt_ll_sw_split.conf`
(sources 1, sinks 2) and
`zephyr/samples/bluetooth/bap_unicast_client/overlay-bt_ll_sw_split.conf`
(sources 2, sinks 1), whose global pool sizing is implemented in
`zephyr/subsys/bluetooth/controller/ll_sw/isoal.c`. The client scan-data limit
is required because the receiver's connectable extended advertisement exceeds
the SW Split default of 31 bytes. NCS's own BAP unicast client SW Split overlay
pins the same 191-byte value. Do not set the CIS creation policy in the
peripheral-only receiver: both policy choices depend on central ISO, so forcing
the low-latency choice there produces an invalid-choice warning. Do not change
counts or role shape without escalation.

### Build-diagnostic repairs

The first nRF54L15BSim build exposed three deterministic host-build diagnostics.
Repair their causes; do not hide them:

- In both repository BSim `CMakeLists.txt` files, set
  `CMAKE_EXPORT_COMPILE_COMMANDS` to `ON` before `find_package(Zephyr ...)`.
  Remove the current target-local `-Wno-cpp` workaround. This preserves the
  receiver/client compile databases without passing the variable through the
  language-less sysbuild root, which reports it unused.
- In `scripts/bsim-stage1-run.sh`, after loading the toolchain and before
  compiling, remove only the exact `fortify` and `fortify3` tokens from an
  explicitly populated `NIX_HARDENING_ENABLE`. Native-simulator BSim compiles
  at `-O0`, where Nix's injected `_FORTIFY_SOURCE` is invalid. Do not disable
  warnings or add `-Wno-cpp`; leave every other hardening token unchanged.
- Set Stage 1 `cmake_args` to
  `-DCONFIG_COVERAGE=y -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y`. Set nonempty
  `cmake_extra_args=-DCONFIG_ASSERT=y`. The split is deliberate: upstream
  `compile.source` otherwise appends one empty command-line argument and CMake
  warns `Ignoring empty string (\"\") provided on the command line.`
- Apply the same Nix fortify-token filtering and nonempty `cmake_extra_args`
  shape in `scripts/bsim-official-smoke.sh`. Keep warnings as errors there too;
  remove its existing `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n` workaround.

After a pristine compile, `cmake.out` may retain only the NCS experimental
symbol notices listed by repository policy. It must not contain the invalid
receiver policy choice, ignored empty argument, or unused
`CMAKE_EXPORT_COMPILE_COMMANDS` warnings. `ninja.out` must not contain the
glibc `_FORTIFY_SOURCE` warning.

### Canonical runner

In `scripts/bsim-stage1-run.sh`:

- Set `snippet="bt-ll-sw-split"` before both compiles.
- Before receiver compile, set `conf_overlay` to absolute receiver fragment.
- Before client compile, replace `conf_overlay` with absolute client fragment.
- Preserve explicit executable names, lock, matrix, private/external log-root
  semantics, process exits, parser use, and summaries.
- Keep compiler warnings as errors. Inspect each generated `cmake.out` and
  `ninja.out`; assigned-value warnings, missing symbols, ignored controller
  choices, and runtime `LOG_WRN` are failures. NCS experimental-symbol notices
  required by SW Split ISO may be recorded as existing SDK diagnostics, not
  silently omitted.
- Verify resolved receiver/client `.config` has
  `CONFIG_BT_LL_SW_SPLIT=y` and not `CONFIG_BT_LL_SOFTDEVICE=y`; resolved DTS
  must choose enabled `bt_hci_controller` and disabled `bt_hci_sdc`.
- Verify resolved receiver `.config` has
  `CONFIG_BT_CTLR_ISOAL_SOURCES=1` and `CONFIG_BT_CTLR_ISOAL_SINKS=2`, and
  resolved client `.config` has `CONFIG_BT_CTLR_ISOAL_SOURCES=2` and
  `CONFIG_BT_CTLR_ISOAL_SINKS=1`.
- Verify resolved client `.config` has
  `CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX=191`. The failed pre-repair image resolved
  this value to 31, received valid advertising packets at PHY level, but never
  delivered the receiver's extended advertising report to `device_found()`.
- Verify resolved client `.config` has `CONFIG_BT_ISO_TX_BUF_COUNT=6` and
  `CONFIG_BT_CTLR_ISO_TX_BUFFERS=6`.

### Official smoke helper

Update `scripts/bsim-official-smoke.sh` so nRF54L15BSim remains its default
through `bsim-env.sh` and its upstream Bluetooth Audio app explicitly receives:

- `snippet=bt-ll-sw-split`
- `conf_overlay=${ZEPHYR_BASE}/tests/bsim/bluetooth/audio/overlay-bt_ll_sw_split.conf`

Do not change evidence-based partial acceptance or teardown-race classification.

### Regression tests

Add focused static/public-boundary coverage under
`tests/unit/bsim_runner/` (new file is acceptable) that proves:

- default board is exactly `nrf54l15bsim/nrf54l15/cpuapp`;
- Stage 1 sets SW Split snippet and separate receiver/client fragments;
- sysbuild files have no nRF5340 CPUNET or `hci_ipc` child image;
- testcase and standalone script use nRF54L15BSim;
- LSP paths use nRF54L15BSim executable names;
- active BSim scripts/configs do not contain `nrf5340bsim` or
  `nrf5340_cpunet`.

Test behavior through script inputs/outputs or stable configuration contracts,
not private helper call counts.

### Documentation and evidence

Update only active claims in:

- `AGENTS.md`
- top/current sections of `STATUS.md`
- active BSim section of `docs/design.md`
- active fixture statement in `docs/testing/behavior-contract.md`

Retain historical result text and exact old paths when they describe past
nRF5340 runs. Add `docs/development/pb-034-nrf54l15bsim-results.md` with:

- controller decision and resolved config/DTS proof;
- exact validation commands;
- full 17-scenario/26-run summary, target-native startup totals, unchanged TX
  hash evidence, and PCM results within unchanged numerical limits;
- exact warnings/diagnostics disposition;
- simulator limits versus physical HIL authority.

Update PB-034 notes, acceptance checkboxes, final summary, and status to Review
after all criteria pass. Do not mark Done; no PR gate exists yet.

## Out of scope

- Any `src/` behavior change.
- Scenario count, transport fixture, TX hash, PCM limit, explicit-loss,
  malformed-SDU, reconnect, lifecycle, or process-rule changes beyond the
  approved target-native startup recipe/totals rebaseline above.
- Artificial client delay, startup loss injection, or sender scheduling changes
  intended to imitate legacy nRF5340 HCI-IPC timing.
- FLPR/I2S simulation.
- HIL source, artifact, HCI-UART, E83 receiver, release, or public product work
  owned by PB-019 and PB-035 through PB-038.
- Rewriting immutable historical evidence.
- Push, PR, merge, amend, or force-push.

## Verification

Run inside repository dev shell:

```bash
python3 -m unittest discover -s tests/unit/bsim_runner -p 'test_*.py'
bash scripts/gen-lsp-links.sh
bash scripts/gen-lsp-links.sh --check
BSIM_LOG_ROOT=/tmp/opencode/pb034-stage1-evidence bash scripts/bsim-stage1-run.sh
nix develop -c backlog doctor
git diff --check
```

`/tmp/opencode/pb034-stage1-evidence` must be absent or empty before run. Retain
it for review. Also inspect both generated `cmake.out`, `ninja.out`, resolved
`.config`, resolved `zephyr.dts`, and all 52 device logs for warnings/errors.

Run official smoke if migration changes its compile behavior:

```bash
bash scripts/bsim-official-smoke.sh
```

Its documented NCS teardown race may remain PARTIAL only when parser proves
accepted traffic and exact known marker. Any unrelated failure blocks PB-034.

## Commit and return contract

Current worktree already contains intended migration backlog files PB-019 and
PB-034 through PB-038. They are not foreign changes. Include them, this handoff,
PB-034 implementation, results, and PB-034 Review transition in one scoped
commit with message:

```text
test: migrate BabbleSim to nRF54L15 (PB-034)
```

Do not include generated build output or `/tmp` evidence. Do not push or open a
PR. Return changed files, resolved controller proof, tests and results, full
Stage 1 metrics/hashes, warning disposition, commit hash, deviations, and
blockers.

Stop and escalate before commit if target-native action histories differ from
the pinned table above, a required Kconfig symbol cannot be satisfied without
weakening acceptance, TX hashes differ, PCM exceeds existing limits, runtime
warning appears, or scope seems to require production behavior changes.
