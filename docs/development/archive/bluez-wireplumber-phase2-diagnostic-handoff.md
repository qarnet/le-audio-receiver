# Phase 2 diagnostic handoff — locate BAP export failure

## Goal

Replace incorrect “WirePlumber has no BAP endpoint registration” conclusion
with captured first-failure evidence between active BlueZ BAP discovery and
PipeWire device export.

## Corrected facts

- PipeWire 1.6.5 BlueZ SPA binary contains `bap_source`, `bap_sink`,
  `RegisterEndpoint`, and LC3 support.
- Official PipeWire/WirePlumber docs list BAP roles as default monitor roles.
- Adapter `org.bluez.Media1.SupportedUUIDs` includes Sink PAC (`2bc9`) and
  Source PAC (`2bcb`) endpoint UUIDs.
- Passive btmon ordinary reconnect shows encrypted ATT, reads both Sink ASEs,
  and enables notifications for both ASEs and ASE Control Point. BlueZ BAP is
  attached; absence of BlueZ-owned endpoint object paths is not proof that
  client-owned MediaEndpoint registration never occurred.

## In scope

1. Keep Phase 2 gate/tests uncommitted while diagnosing.
2. Capture D-Bus traffic during WirePlumber restart to prove exact
   `Media1.RegisterEndpoint` calls, endpoint UUIDs/codecs/capabilities, and
   replies. Use `busctl monitor`/`dbus-monitor`; do not infer from object paths.
3. Enable temporary runtime WirePlumber/PipeWire debug logging, restart user
   services, ordinary BlueZ disconnect/reconnect, and capture logs showing:
   device discovery, remote PACs, endpoint matching, profile creation, selection
   errors, or export suppression. Restore normal services afterward.
4. Capture passive `btmon` reconnect trace and identify PACS reads, PAC values,
   Available Context value, ASE discovery, notifications, and any ASCS writes.
5. If needed, temporarily run bluetoothd with debug using same existing flags
   (`-E`, kernel experimental ISO UUID, same config), capture profile probe,
   BAP attach/PAC/select errors, then restore system service exactly. No config
   file edits.
6. Inspect BlueZ 5.86 and PipeWire 1.6.5 source matching installed versions to
   map observed error to exact check. `/tmp/opencode/bluez-docs/bluez-5.86` is
   available as read-only source evidence; do not edit external source.
7. Correct Phase 2 results and gate classification. Remove unsupported
   WirePlumber `bluez.lua` claims.
8. If exact fix is a receiver standards defect with no design ambiguity,
   implement smallest fix, rebuild/flash normally, and rerun 30 s then 120 s
   stock gate. If fix requires CAP/CAS, host policy/config, codec expansion, or
   another material choice, stop with exact evidence for orchestrator.

## Constraints

- No `bap_central.py`, custom MediaEndpoint, raw-HCI, direct ISO writes, or
  persistent WirePlumber rules.
- No package install/update, NCS edits, mass erase/recover, push, or PR update.
- Preserve and report raw logs.
- Hardware resets/normal flash and reversible service restarts are authorized.
- Do not commit unless stock 30 s and 120 s streams pass all Phase 2 criteria.

## Verification

At minimum, return:

- exact RegisterEndpoint calls/replies;
- exact remote Sink PAC bytes and Available Context bytes;
- BlueZ/PipeWire selection or export error with source location;
- receiver serial state;
- whether PipeWire device/profile/sink appears;
- all service restoration checks;
- file diff/status and any commit only if full acceptance passes.
