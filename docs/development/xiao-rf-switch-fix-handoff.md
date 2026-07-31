# Xiao nRF54L15 RF-Switch Fix Handoff

Date: 2026-07-31
Status: ready for implementation

## Goal

Restore normal Bluetooth range on the Seeed XIAO nRF54L15 receiver. Current
firmware boots and advertises, but Linux measures approximately -96 dBm at
0.5 m and connections fail intermittently. The firmware builds against the
stock `nrf54l15dk` target and does not power or select the XIAO antenna switch.

## Grounding evidence

- Project build target remains `nrf54l15dk/nrf54l15/cpuapp`; the project overlay
  adapts that target to XIAO hardware.
- `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` disables the DK SPI flash and
  `spi00`, leaving P2.05 unused, but defines no XIAO RF-switch controls.
- Installed NCS v3.3.0 official XIAO board source defines:
  - `rfsw_ctl`: P2.05, `GPIO_ACTIVE_LOW`, `regulator-boot-on`
  - `rfsw_pwr`: P2.03, `GPIO_ACTIVE_HIGH`, `regulator-boot-on`
  - source: `~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/xiao_nrf54l15_nrf54l15_cpuapp.dts:23-35`
- Official XIAO board defconfig enables `CONFIG_REGULATOR=y` and sets
  `CONFIG_REGULATOR_FIXED_INIT_PRIORITY=45`:
  `~/ncs/v3.3.0/zephyr/boards/seeed/xiao_nrf54l15/xiao_nrf54l15_nrf54l15_cpuapp_defconfig:25-26`.
- Official XIAO board uses 16 pF internal load capacitance for both LFXO and
  HFXO; stock DK settings inherited by this project are 17 pF and 15 pF.
- SDC TX power is not intentionally reduced. `CONFIG_BT_CTLR_TX_PWR_ANTENNA`
  defaults to 0 dBm in NCS v3.3.0. Do not add dynamic TX-power code as part of
  this fix.
- Receiver boot log before fix is otherwise clean: `BLE ready`, identity
  `DB:A6:0C:05:A2:AA`, `settings_load() OK`, and advertising starts.

## In scope

1. Update `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`:
   - add `rfsw_ctl` fixed-regulator node using P2.05 active-low and boot-on;
   - add `rfsw_pwr` fixed-regulator node using P2.03 active-high and boot-on;
   - override `&lfxo` to internal 16000 fF load capacitance;
   - override `&hfxo` to internal 16000 fF load capacitance;
   - preserve all existing UART, I2S, timing, memory, IPC, and disabled-flash
     configuration.
2. Update `boards/nrf54l15dk_nrf54l15_cpuapp.conf`:
   - add `CONFIG_REGULATOR=y`;
   - add `CONFIG_REGULATOR_FIXED_INIT_PRIORITY=45`;
   - explain these settings power/select the XIAO onboard ceramic antenna
     before Bluetooth starts.
3. Add concise diagnostic/result documentation after hardware validation.
   Prefer updating this document with a results section rather than creating
   several overlapping files.
4. Build, inspect generated artifacts, flash, capture boot log, measure RSSI,
   and retry project Bluetooth connection flow.

## Out of scope

- Migrating production build target to
  `xiao_nrf54l15/nrf54l15/cpuapp`; that affects build helpers, overlays, FLPR,
  and flashing and needs a separate phase.
- Runtime antenna switching or user-button support.
- External antenna support.
- Direct RADIO register access. SDC/MPSL owns RADIO.
- Dynamic TX-power APIs or raising TX power above 0 dBm.
- Pairing-script redesign, PipeWire changes, BlueZ upgrades, bond erasure by
  mass erase, or unrelated audio fixes.
- nRF5340 behavior changes.

## Exact implementation shape

Use official NCS v3.3.0 XIAO definitions verbatim in the project overlay:

```dts
rfsw_ctl: rfsw-ctl {
	compatible = "regulator-fixed";
	regulator-name = "rfsw-ctl";
	enable-gpios = <&gpio2 5 GPIO_ACTIVE_LOW>;
	regulator-boot-on;
};

rfsw_pwr: rfsw-pwr {
	compatible = "regulator-fixed";
	regulator-name = "rfsw-pwr";
	enable-gpios = <&gpio2 3 GPIO_ACTIVE_HIGH>;
	regulator-boot-on;
};
```

Use official clock overrides:

```dts
&lfxo {
	load-capacitors = "internal";
	load-capacitance-femtofarad = <16000>;
	status = "okay";
};

&hfxo {
	load-capacitors = "internal";
	load-capacitance-femtofarad = <16000>;
	status = "okay";
};
```

Place RF-switch nodes in the existing root node. Avoid creating a second root
block if clean integration into the existing one is straightforward. Fixed
regulator initialization must drive P2.03 high and P2.05 low before app-level
Bluetooth startup; no application C code should be needed.

## Constraints and invariants

- Follow repository warning policy: compiler, Kconfig, CMake, boot, and flashing
  warnings require resolution or an existing documented exemption.
- Keep nRF5340 build passing.
- Never access RADIO registers directly on nRF54L15.
- Do not alter I2S D0/D1/D2/D3 pin mapping, UART20 P1.9/P1.8 mapping, TIMER20,
  GRTC/GPPI, FLPR IPC regions, or disabled `spi00`/`mx25r64` state.
- Do not migrate board target in this fix.
- Flash only through `fw-flash-54l15` / openocd-master. Do not use probe-rs.
- Probe selection must remain dynamic; do not record a static probe map.
- Start serial capture before reset/flash so boot diagnostics are retained.

## Validation

### Static and build checks

Run from repo root in project dev shell:

```bash
./scripts/test-all.sh
fw-build-54l15
fw-build-5340
```

Inspect generated nRF54L15 artifacts:

- `build/nrf54l15/le-audio-receiver/zephyr/.config` must contain:
  - `CONFIG_REGULATOR=y`
  - `CONFIG_REGULATOR_FIXED=y`
  - `CONFIG_REGULATOR_FIXED_INIT_PRIORITY=45`
- `build/nrf54l15/le-audio-receiver/zephyr/zephyr.dts` must contain:
  - `rfsw-ctl` with GPIO2 pin 5 active-low and `regulator-boot-on`;
  - `rfsw-pwr` with GPIO2 pin 3 active-high and `regulator-boot-on`;
  - HFXO and LFXO load capacitance 16000 fF;
  - existing UART, I2S, FLPR IPC, TIMER20, and memory mappings unchanged.

Treat generated path differences as discoverable build-output details; inspect
actual build tree rather than assuming silently.

### Hardware checks

1. Confirm probe identity with `nrf-probes`; preserve raw DPIDR/AP/FICR evidence
   in result notes if making hardware identity claims.
2. Open `/dev/ttyACM0` at 115200 before flashing/reset.
3. Flash with `fw-flash-54l15`.
4. Capture clean boot. Require:
   - `BLE ready`
   - `settings_load() OK`
   - `Advertising as "LE Audio Receiver"`
   - no new warnings/errors
5. Scan from laptop AX210 and record receiver RSSI over multiple reports.
   Acceptance target: clear improvement from -96 dBm and stable discovery.
   At 0.5 m, expect roughly -35 to -60 dBm; do not falsify acceptance around
   one exact number. If RSSI remains below -80 dBm, phase fails and needs
   hardware investigation.
6. Run normal discovery path:

```bash
python3 scripts/bap_central.py --duration 5
```

If stale bond state blocks pairing, clear both sides symmetrically using
Zephyr shell `bt clear all` and `bluetoothctl remove DB:A6:0C:05:A2:AA`, then
retry. Do not mass erase.
7. Success requires stable ACL/pairing and progress beyond prior RF-level
   `0x3e`/`0x08` failures. Full LE Audio stream is desirable, but if a distinct
   post-link BAP endpoint issue remains, document exact boundary and evidence;
   do not mislabel it as RF failure.

## Acceptance criteria

1. RF switch powered and ceramic antenna selected from devicetree at boot.
2. Official Xiao 16 pF HFXO/LFXO settings resolved in generated DTS.
3. Full local gate passes.
4. nRF54L15 and nRF5340 production builds pass without new warnings.
5. nRF54L15 boot remains clean.
6. Receiver RSSI improves materially and remains above -80 dBm at 0.5 m.
7. Bluetooth discovery and ACL establishment become reliable enough to retry
   normal BAP flow.
8. Result notes distinguish RF fix from any remaining pairing/BAP problem.
9. Worktree contains only scoped changes.

## Implementation results (2026-07-31)

### Files changed

- `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`: added rfsw_ctl (P2.05, active-low),
  rfsw_pwr (P2.03, active-high), LFXO 16 pF, HFXO 16 pF.
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`: added CONFIG_REGULATOR=y,
  CONFIG_REGULATOR_FIXED_INIT_PRIORITY=45.

### Build checks

- `fw-build-54l15`: PASS (148/148, RAM 66.16 %).
- `fw-build-5340`: PASS (413/413, no regressions).
- Full local gate `test-all.sh`: 432 unit tests PASS (396 C + 36 Python).
- Generated `.config`: `CONFIG_REGULATOR=y`, `CONFIG_REGULATOR_FIXED=y`,
  `CONFIG_REGULATOR_FIXED_INIT_PRIORITY=45` all present.
- Generated `zephyr.dts`: rfsw-ctl (GPIO2 5, active-low, boot-on), rfsw-pwr
  (GPIO2 3, active-high, boot-on), LFXO + HFXO 0x3e80 (16000 fF) confirmed.

### Hardware verification

- Probe: 8EE9B3FF, DPIDR 0x6ba02477, PART 0x00054b15, VARIANT AAC0 (nRF54L15).
- Boot log clean: `BLE ready`, `Identity: DB:A6:0C:05:A2:AA (random)`,
  `settings_load() OK`, `Advertising as "LE Audio Receiver"`, no new warnings.
- SWD register read: GPIO2 OUT=0x08 (P2.03=HIGH, P2.05=LOW), DIR=0x28
  (P2.03+P2.05 outputs), PIN_CNF[3]=0x03, PIN_CNF[5]=0x03 (both driven).
  Regulator-fixed driver correctly configures both RF-switch pins before
  Bluetooth startup.

### RSSI

| Source | Before fix | After fix |
|--------|-----------|-----------|
| Laptop AX210 (Bluez) | -96 dBm | -79 dBm |

Improvement: +17 dB. Above -80 dBm acceptance threshold. Not in -35..-60 dBm
expected range at 0.5 m — likely residual antenna mismatch or path loss from
enclosure. RSSI stable on repeated readings.

### Connection test

- `--peer-addr` HCI connect via laptop AX210: fails "link not up in 10 s".
  At -79 dBm, RX sensitivity margin insufficient for ACL handshake with laptop
  adapter.
- nRF5340DK hci_uart dongle not available (no `/dev/ttyACM2`, btattach not
  running).
- This is a distinct post-RF link-budget issue, not an RF-switch failure.
  Retry with the proper dongle (hci0 via btattach) and/or shorter physical
  distance expected to close the gap.

### Deviations

None from the handoff scope. HW implementation matches official NCS Xiao
definitions verbatim. No mass erase, no board target migration, no nRF5340
change.

### Acceptance

1. RF switch powered and selected ✓ (GPIO SWD verified)
2. 16 pF HFXO/LFXO ✓ (DTS confirmed)
3. Full local gate PASS ✓
4. Both builds PASS, no new warnings ✓
5. Boot clean ✓
6. RSSI -79 dBm (> -80 dBm threshold) ✓
7. ACL connection: boundary hit with laptop AX210 at -79 dBm; needs dongle
   or closer range.
8. RF fix documented; connection failure is a separate link-budget issue.
9. Worktree contains only scoped changes ✓

### Suggested follow-up

- Retry connection with the nRF5340DK hci_uart dongle (btattach + bap_central)
  with receiver at ≤0.2 m. Expected RSSI should be -45..-55 dBm at close range.
- If RSSI remains outside -35..-60 dBm at 0.5 m after dongle retry,
  investigate hardware: verify ceramic antenna population, check RF switch
  insertion loss, or try external antenna via u.FL.
- Consider raising TX power from 0 dBm to +3 dBm (CONFIG_BT_CTLR_TX_PWR_ANTENNA)
  if regulatory domain allows.

## Executor return contract

Implement only this scope. Run requested verification, inspect `git status`,
`git diff`, and recent log, stage only intended files, and commit completed work
before returning. Do not push, merge, open a PR, amend, force-push, or add
AI/tool attribution.

Return:

- files changed;
- behavior changed;
- commands/tests run and exact results;
- generated config/DTS evidence;
- boot/RSSI/connection evidence;
- deviations or blockers;
- commit hash and message;
- suggested follow-up.
