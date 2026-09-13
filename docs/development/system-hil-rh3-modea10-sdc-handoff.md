# RH3 ModeA10 handoff: switch HIL source fixture net core from SW-split to SDC

Status: approved fixture rework + one hardware validation run. The fixture
nRF5340DK's hci_ipc net core switches from the Zephyr SW-split controller to
the Nordic SoftDevice Controller (SDC) as the central ISO controller. This is
instrument calibration on the HIL source fixture (the user-settled 2026-09-04
framing: the DK is the TEST INSTRUMENT, not a product). The receiver is
untouched. No product, runner, row, limit, or evidence changes.

## Why (decision record, from completed analysis)

The ModeA9 SN_STRICT validation
(`docs/development/system-hil-rh3-modea9-snstrict-result.md`) proved the
SW-split ISO-AL strict-sequencing expiry is the dominant Mode B delivery-loss
mechanism (recovery from 113 to 10141 valid SDUs of 12644, 0.9% to 80.2%) but
left ~20% residual SDU loss and a 34% stream stretch under frozen limits. The
root cause is pacing x duty: the completion-paced host cannot submit SDUs
ahead of CIS event preparation at ~82% CIG duty, and the SW-split ISO-AL
either drops (SN_STRICT=y) or time-shifts (SN_STRICT=n) the late payloads.
Both remaining SW-split levers are semi-fixes on an experimental-symbol
controller: PSN_IGNORE trades stream alignment and still cannot meet the PLC
ceiling under stretch (PLC scales with stretch events; even 100% delivery
keeps plc near 28% of decoded, above the 5% ceiling); SN_STRICT=n + deep
queue still tunes an experimental controller per shape, and Mode A dual-CIS
at ~106% duty would need yet another calibration round.

SDC is the proper fix and is fully supported in the installed NCS v3.3.0:

- `nrf/subsys/bluetooth/controller/Kconfig:7` `BT_LL_SOFTDEVICE` (default y on
  nRF5340 cpunet; `depends on SOC_COMPATIBLE_NRF5340_CPUNET` +
  `DT_HAS_NORDIC_BT_HCI_SDC_ENABLED`, satisfied by the
  `nrf5340_cpunet.dtsi` default `bt_hci_sdc` node and
  `zephyr,bt-hci = &bt_hci_sdc` chosen) `select`s
  `BT_CTLR_CENTRAL_ISO_SUPPORT`; `BT_CTLR_CENTRAL_ISO` defaults `y` when
  `BT_ISO_CENTRAL` is set
  (`zephyr/subsys/bluetooth/controller/Kconfig:1031`).
- SDC central ISO is BAP-tested per
  `nrfxlib/softdevice_controller/doc/isochronous_channels.rst` ("Connected
  Isochronous Stream - Central" among tested configurations, "focuses on the
  audio use-case configurations described in the Basic Audio Profile").
- Nordic ships exactly this shape in production on the same DK:
  `nrf/applications/nrf5340_audio/` runs SDC netcore
  (`sysbuild/ipc_radio/prj.conf`: `BT_ISO_CENTRAL=y`,
  `BT_CTLR_SDC_PERIPHERAL_COUNT=1`) with a BAP unicast client host on cpuapp.
- The receiver's own SDC peripheral already passes this exact Mode B 240-byte
  shape from other SDC centrals (dongle history: `SDUs=11659, plc=2` at 120 s
  and 300 s), so SDC central -> SDC peripheral is field-proven at this shape.
- The unframed-PDU limitation DRGN-21099
  (`nrfxlib/softdevice_controller/limitations.rst`) does not block the frozen
  shapes: unframed is enforced when `SDU_Interval` is a multiple of 1250 us
  or a divisor of 5000 us; 10000 us and 7500 us both qualify.
- The upstream `bap_unicast_client` sample uses SW-split only because it is
  upstream Zephyr code that cannot depend on the NCS-only precompiled SDC
  libraries (`nrfxlib/softdevice_controller/lib/nrf53/soft-float/
  libsoftdevice_controller_multirole.a`); the nrf53 SDC variant is MULTIROLE
  only, which unconditionally includes central ISO. It is a licensing-distribution
  artifact, not a technical limitation.

No shipped hci_ipc cpunet conf uses SDC (all ten nrf5340 confs are SW-split),
so this repository owns the new SDC overlay. That is the same ownership model
as the current SW-split overlay.

## Scope

### In scope

1. New repo-owned SDC net-core overlay
   `hil/source/app/overlay-nrf5340_cpunet_sdc.conf`.
2. `hil/source/app/sysbuild.cmake`: SDC branch, replaces the SW-split wiring
   for hci_ipc (base `prj.conf` + our SDC overlay; drop the
   `bt-ll-sw-split` snippet and the SW-split sample conf from the hci_ipc
   image).
3. `hil/source/app/boards/nrf5340dk_nrf5340_cpuapp.conf`: comment update only
   (HCI buffer sizes are controller-agnostic and unchanged).
4. Software verification: source build (pristine, double-build determinism
   proof), resolved-config proof, native source-app Twister suite unchanged
   pass, host `tests/hil` regression unchanged pass.
5. One hardware validation run, same fixed identity shape as ModeA9:
   row `rh3.fresh_mode_b_48_4_1`, new run ID `rh3-modeb-sdc-20260907`.
6. Result documentation `system-hil-rh3-modea10-sdc-result.md` + resume-state
   update + commit.

### Out of scope

- Any receiver firmware change (`src/`, `boards/`, receiver Kconfig).
- Any runner, row, limit, or matrix change. The frozen limits and the
  `run`/`run-rh3-matrix` CLIs stay exactly as committed.
- Any production (`src/`, dongle) firmware change.
- Removing the SW-split path (files stay in-tree for future A/B diagnostics;
  the SW-split overlay returns to its committed SN_STRICT=y state; the staged
  uncommitted SN_STRICT=n line was reverted after ModeA9 recorded its
  outcome).
- RH3-7p5 work (after the 10 ms matrix passes, separately planned).
- Mode A depth/pacing changes on the source host. The current completion-paced
  host with target 3 is unchanged in this phase; if SDC needs queue-depth
  matching, that is a follow-up with its own handoff.

## Exact implementation

### 1. `hil/source/app/overlay-nrf5340_cpunet_sdc.conf` (new file)

Modeled on the upstream SW-split central-ISO base
`zephyr/samples/bluetooth/hci_ipc/nrf5340_cpunet_iso_central-bt_ll_sw_split.conf`
and the NCS SDC references (`nrf/applications/nrf5340_audio/sysbuild/ipc_radio/prj.conf`,
`nrf/samples/bluetooth/iso_time_sync/sysbuild/ipc_radio/prj.conf`,
`zephyr/samples/bluetooth/hci_ipc/nrf54h20_cpurad-bt_ll_softdevice.conf`),
translated to SDC symbols and tightened for exactly one central peer with two
ISO TX streams:

```text
# SPDX-License-Identifier: Apache-2.0
#
# Repository net-core overlay for the dedicated LE Audio source fixture:
# SoftDevice Controller (SDC) variant. Applied on top of the hci_ipc
# sample base prj.conf (controller-agnostic) instead of any SW-split
# sample configuration. The DT default on nrf5340_cpunet selects the
# SDC node (bt_hci_sdc okay, zephyr,bt-hci chosen), so no snippet and
# no DT overlay is needed; only this Kconfig overlay is applied.
#
# Decision record: docs/development/system-hil-rh3-modea10-sdc-handoff.md.
# The SW-split ISO-AL expires (SN_STRICT) or time-shifts (SN_STRICT=n) late
# TX SDUs under the completion-paced host at high CIG event duty; ModeA9
# proved the mechanism and left the row failing frozen limits. SDC is the
# production-grade central ISO controller (BAP-tested per nrfxlib SDC
# isochronous_channels.rst; same shape as nrf5340_audio's SDC netcore +
# BAP unicast client host). Fixture connects to exactly one receiver over
# up to two connected ISO streams, central role only.

CONFIG_BT_BROADCASTER=n
CONFIG_BT_OBSERVER=y
CONFIG_BT_EXT_ADV=y
CONFIG_BT_PER_ADV=n
CONFIG_BT_PER_ADV_SYNC=n
CONFIG_BT_ISO_BROADCASTER=n
CONFIG_BT_ISO_SYNC_RECEIVER=n
CONFIG_BT_CENTRAL=y
CONFIG_BT_PERIPHERAL=n
CONFIG_BT_ISO_CENTRAL=y
CONFIG_BT_ISO_PERIPHERAL=n

# Host-side ISO channels for the raw-HCI IPC link (shared pool with the
# controller side through the HCI flow; keep equal to controller TX buffers
# per the upstream sample's Newton's-cradle note).
CONFIG_BT_MAX_CONN=1
CONFIG_BT_ISO_MAX_CHAN=2
CONFIG_BT_ISO_TX_BUF_COUNT=6
CONFIG_BT_ISO_RX_BUF_COUNT=1

# Controller: SDC (default y on nrf5340 cpunet; DT default node), central
# ISO roles and buffers. One CIG, up to two CIS streams, sink-only traffic
# from the fixture's perspective is TX (fixture is the audio source).
CONFIG_BT_LL_SOFTDEVICE=y
CONFIG_BT_CTLR_ASSERT_HANDLER=y
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX=191

CONFIG_BT_CTLR_CONN_ISO=y
CONFIG_BT_CTLR_CENTRAL_ISO=y
CONFIG_BT_CTLR_PERIPHERAL_ISO=n
CONFIG_BT_CTLR_CONN_ISO_GROUPS=1
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2

# SDC ISO TX buffer pools. HCI buffer count is shared across streams; the
# help text recommends larger values for short SDU intervals; six covers
# the completion-paced host bursts (outstanding target 3 per stream, two
# streams). PDU buffers per stream cover subevent retransmissions.
CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=6
CONFIG_BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT=6

# SDC role counts: central-only fixture.
CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT=0

CONFIG_NCS_BOOT_BANNER=n
CONFIG_BOOT_BANNER=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_STDOUT_CONSOLE=n
CONFIG_PRINTK=n
CONFIG_EARLY_CONSOLE=n
```

Notes on deliberate choices:

- `CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=6`: SDC default is 3; its help
  text says the number may need to be larger with small SDU intervals or
  quick successive sends. The completion-paced host bursts up to target-3 per
  stream; 6 matches the previous SW-split pool depth and the app-core
  `CONFIG_BT_ISO_TX_BUF_COUNT=6`, keeping the three depths equal (matching
  the repo's existing buffer-matching gotcha policy).
- `CONFIG_BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT=6`: SDC default 3;
  raised to 6 to preserve deep pre-transmission margin per stream, mirroring
  the previous SW-split `CONFIG_BT_CTLR_ISO_TX_BUFFERS=6` intent.
- TX power: SDC on nRF5340 defaults to the supported +3 dBm
  (`CONFIG_BT_CTLR_TX_PWR_ANTENNA` default 3; the SW-split
  `CONFIG_BT_CTLR_TX_PWR_PLUS_3=y` symbol does not exist under SDC). The
  fixture keeps +3 dBm by default, matching the current fixture RF margin.
- Stack sizes: hci_ipc `prj.conf` base sets `MAIN_STACK_SIZE=512`,
  `SYSTEM_WORKQUEUE_STACK_SIZE=512`, `HEAP_MEM_POOL_SIZE=4096`; SDC selects
  larger defaults (`nrf/Kconfig.nrf:68` `MAIN_STACK_SIZE default 1128 if
  BT_LL_SOFTDEVICE`; `SYSTEM_WORKQUEUE_STACK_SIZE` default 2048). Do not pin
  smaller values: let the SDC defaults win over the base prj.conf only where
  they are conditional defaults (Kconfig conditional defaults do not override
  explicit assignments; if the base 512/512/4096 assignments cause a Kconfig
  "assigned value but got" warning or a runtime stack overflow, raise them in
  the overlay to 2048/2048/8192, the nrf5340_audio netcore values, and record
  that change in the result doc). Verify the resolved config explicitly lists
  the final values.
- The console-silencing block is carried over from the SW-split overlay
  unchanged (net core is silent; the app core owns the HIL1 console).

### 2. `hil/source/app/sysbuild.cmake` (edit)

Replace the SW-split wiring inside the `if(SB_CONFIG_NET_CORE_IMAGE_HCI_IPC)`
block:

- Remove the `${NET_APP}_CONF_FILE` cache pin to
  `nrf5340_cpunet_iso_central-bt_ll_sw_split.conf`.
- Remove `list(APPEND ${NET_APP}_SNIPPET bt-ll-sw-split)`.
- Keep `add_overlay_config(hci_ipc
  ${CMAKE_CURRENT_LIST_DIR}/overlay-nrf5340_cpunet_sdc.conf)`.
- Keep the merged-hex foreach block unchanged.

Resulting hci_ipc image configuration: sample base `prj.conf` (already in
the sample's own CMake default) + our SDC overlay, with the DT-default SDC
node selected (no snippet). Header comment in the file updated to say the
net core runs SDC.

### 3. `hil/source/app/boards/nrf5340dk_nrf5340_cpuapp.conf` (comment only)

The four HCI buffer-size symbols stay (controller-agnostic on the app-core
host side). Update the comment to reference the SDC controller exchanges
through hci_ipc instead of "SW Split controller".

### 4. `hil/source/app/Kconfig.sysbuild` (comment only)

Comment currently says "The nRF5340DK net core runs the hci_ipc sample with
the SW Split controller"; update to SDC.

## Verification plan

Software (all before any hardware):

1. `git diff --check` clean.
2. Two pristine builds: `nix develop --command fw-build-hil-source` twice.
   Record all four hashes; require build 1 == build 2 byte-identical
   (CPUNET hash will change from `4e4b82f5...` to a new value; app-core
   CPUAPP hash must stay
   `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
   because the app core is unchanged).
3. Resolved-config proof from `build/hil-source/hci_ipc/zephyr/.config`:

   ```text
   CONFIG_BT_LL_SOFTDEVICE=y
   # CONFIG_BT_LL_SW_SPLIT is not set
   CONFIG_BT_CTLR_CENTRAL_ISO=y
   CONFIG_BT_CTLR_CONN_ISO=y
   CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT=0
   CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=6
   CONFIG_BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT=6
   CONFIG_BT_ISO_CENTRAL=y
   CONFIG_BT_PERIPHERAL=n (or "is not set")
   ```

   and app core unchanged:
   `CONFIG_HIL_SOURCE_QOS_RTN=5`, `CONFIG_HIL_SOURCE_QOS_PHY=2`,
   `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`.
4. No new actionable compiler/Kconfig warning. Allowed set: documented
   dirty-tree notice, global `__ASSERT()` notice, receiver watchdog
   empty-library warning (receiver builds only), and any NCS-sysbuild
   deprecation notice already documented in STATUS.md. A Kconfig
   "assigned value but got" warning on any of our overlay symbols is an
   error: fix the overlay (most likely the stack-size interaction above)
   before proceeding.
5. Native source-app Twister (app core is unchanged; run to prove no
   accidental app-core drift):

   ```text
   nix develop --command env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-sdc-twister
   ```

   Expect `68/68` passed (same suite as ModeA8 preflight).
6. Host runner regression (sequential with the build, never concurrent;
   both touch `build/hil-source`):

   ```text
   nix develop --command python3 -m pytest tests/hil/rh2_test.py tests/hil/rh3_matrix_test.py -q
   ```

   Expect the same pass counts as the committed baseline (RH2 154, matrix 14).

Hardware (one run, exactly this command, outer timeout 3600000 ms):

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modeb-sdc-20260907 \
  --junit /tmp/opencode/hil-runs/rh3-modeb-sdc-20260907.junit.xml \
  --row rh3.fresh_mode_b_48_4_1
```

Preflight: disk gate 80 GiB; `git rev-parse HEAD` + `git status --porcelain`
(expected: only the new/edited fixture files and this handoff); run-ID
validation (both output paths absent, non-symlink, `.locks` empty); fixture
validate returns `{"capture_capability": "none", "fixture_id":
"local-nrf54l15-receiver"}`. Receiver build: normal current-HEAD
`fw-build-54l15` (CPUAPP hash derived at execution time, APP_COMMIT-embedded;
FLPR `45ab8d15...` expected unchanged), resolved config proves
`CONFIG_AUDIO_OFFLOAD_ASRC=y` and `CONFIG_BT_ISO_RX_BUF_COUNT=3`.

Prediction (falsifiable): if the SDC central removes the SW-split ISO-AL loss
class, Mode B 10 ms fresh delivery recovers to PASS under frozen limits
(`rx_valid >= 11379`, `plc <= 5% of decoded`). The SDC dongle history
(`SDUs=11659, plc=2`) shows the receiver accepts this shape near-perfectly
from an SDC central, so a FAIL with a new signature (for example
HCI-level reject of the CIG create, buffer-count mismatch warning, or
connection-establishment loss) points at the new SDC fixture config, not the
receiver, and gets its own classification.

Classification arms:

- PASS: SDC becomes the fixture default. Write the ModeA10 result doc
  (canonical), update resume-state (run count 35, newest flash identity,
  verdict), commit everything (overlay + sysbuild.cmake + board conf comment
  + Kconfig.sysbuild comment + handoff + result + resume-state) as:

  ```text
  fix(hil): switch fixture net core to SDC for central ISO transport
  ```

  Then report readiness for the full RH3 matrix attempt (separate phase, new
  run ID `rh3-matrix-<date>-2`, `run-rh3-matrix`, 14 child runs, outer
  timeout 10800000 ms).

- FAIL same signature (delivery collapse, near-total `rx_unreceived`, zero
  CRC): SDC fixture config is wrong at a deeper level than buffer counts.
  Record the result doc with prediction-vs-outcome, revert nothing (SDC
  wiring stays uncommitted), update resume-state, commit

  ```text
  docs(hil): record SDC fixture validation outcome
  ```

  and STOP for redesign review (two consecutive same-shape failures is the
  plan's stop point; do not iterate blindly on the hardware).

- Other boundary (HCI reject, build failure, connection loss, warning
  tripping, etc.): record, classify (product / fixture / environment), and
  either fix-firmware-side and rerun once as fix-validation, or STOP for
  user decision if the classification is not clean.

## Constraints

Runner owns all hardware. No manual target operations. Actionable build
warnings are errors (allowed: documented dirty-tree notice, nRF54L15
watchdog empty-library, global `__ASSERT()`). No NCS patches. No receiver
firmware changes. No rows/limits/matrix changes. No evidence mutation. No
retry: one run, whatever it yields. Status 0/1/130 immutable. Preserve
`/tmp/opencode/hil-runs/rh3-modeb-snstrict-20260904/` and all prior evidence
unchanged. `tests/hil/fixture.local.json` stays uncommitted.

## Serials and identity (runtime-resolved, never assumed)

- Receiver: XIAO nRF54L15, probe `8EE9B3FF`, console `/dev/ttyACM2`
  (per binding udev `2886:0066` interface `02`).
- Source: nRF5340DK, J-Link `001050023938`, console `/dev/ttyACM1`
  (per binding udev `1366:1061` interface `02`).
- Resolve at run time via the runner's own identity capture; retain raw
  evidence in the run directory (it already does this).

## Result documentation

`docs/development/system-hil-rh3-modea10-sdc-result.md` (canonical):
scope, decision record summary, prediction vs outcome, preflight, build
proof and determinism table, resolved-config proof, per-slot summary and
limits verdict, FLPR active evidence, QoS and ISO tail, sanity checks
(FLPR active submit>=1 success, `c_phy=2`, `c_max_pdu=240`, `nse`
present), integrity (SHA256SUMS all OK, JUnit byte-identical), raw
identity, restoration note, stop point.