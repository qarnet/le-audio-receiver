# Phase 3 handoff — clean pairing and reconnect lifecycle

## Goal

Prove normal BlueZ first pairing, persisted-bond reconnect, and repeated stock
PipeWire playback without repository BAP host helpers.

## Existing mechanism

Receiver already exposes serial shell command `bt unpair`, implemented with
`bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY)`. While connected, Zephyr initiates
asynchronous disconnect; existing main lifecycle waits for disconnect then
restarts advertising. Do not add full `CONFIG_BT_SHELL`.

## In scope

### Automation

- Add a focused pairing/reconnect lifecycle gate or extend stock desktop gate
  cleanly. It may use normal BlueZ D-Bus/`bluetoothctl`, `wpctl`/`pw-dump`,
  `pw-play`, and receiver serial shell. It must not register MediaEndpoint,
  open ISO sockets, invoke raw-HCI, or call `bap_central.py`.
- Preserve strict Phase 2 receiver criteria: explicit duration-consistent SDUs,
  decoded frames, exact I2S DMA start, and all summary faults zero.
- Add unit tests for state polling, timeout/failure classification, stale bond,
  pairing rejection, service resolution, reconnect, and log scoping.

### Hardware sequence

Use nRF54L15 receiver and stock WirePlumber `main-systemwide` profile because
current automation session has no active logind seat. Preserve/restore normal
user service and adapter settings.

1. Start fresh UART capture and verify current firmware boot/identity.
2. With current connection active, send receiver `bt unpair`; require success,
   receiver disconnect callback, advertising restart, and receiver bond erased.
3. Remove host BlueZ device normally (`bluetoothctl remove ...`); verify no
   paired/bonded device object remains.
4. Enable normal BlueZ agent/default-agent and adapter pairable state. Discover
   receiver through normal scan; no direct address injection/raw HCI.
5. Pair, trust, and connect through normal BlueZ. Require Just Works/SC success,
   encrypted services resolved, PACS/ASCS/VCS present, and no stale-bond error.
6. Require WirePlumber BAP device/profile and PipeWire playback sink.
7. Run strict 30 s stock `pw-play`; preserve raw log and counters.
8. Ordinary BlueZ disconnect. Require receiver advertising restart.
9. Reconnect using persisted bond without Pair(); require services/sink return.
10. Run second strict 30 s stock playback with independent fresh log/counters.
11. Reset nRF54L15 normally (no erase/recover), reconnect with same persisted
    bond, and require services/sink return. Run a short strict 10 s playback or
    30 s if startup timing requires it.
12. Restore original WirePlumber service and adapter pairable/discoverable
    settings. Leave valid bond in place; verify no stray processes.

### Firmware quality

- Fix `cmd_bt_unpair()` to return its negative error instead of always returning
  success if review confirms shell convention. Keep clear success/error output.
- Do not add auto-delete-on-pair-failure or weaken SC pairing.
- Any warning, pairing error, I2S fault, decode fault, or reconnect failure keeps
  phase open.

### Docs

- Add Phase 3 results with exact BlueZ state transitions, pairing callbacks,
  service UUIDs, PipeWire object names, playback durations/counters, reset
  reconnect evidence, raw artifact names, and zero-fault proof.
- Mark Phase 3 accepted only after all sequence steps pass.

## Validation

```bash
python3 -m unittest <new pairing gate tests>
bash scripts/test-all.sh
fw-build-5340
fw-build-54l15
git diff --check
```

Hardware sequence above required after builds.

## Out of scope

CAP/CAS, phone interoperability, A2DP, host-specific WirePlumber rules,
multi-peer policy, bond-capacity UX, physical button factory reset, analog
audibility, mass erase/recover, nRF5340 hardware.

## Delivery

Commit only after full clean-pair/reconnect/reset-reconnect acceptance. No push,
merge, PR update, package/NCS edits, amend, or history rewrite. Return exact
files/tests/builds/hardware evidence/commit/service restoration.
