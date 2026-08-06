# Phase 2 continuation — stock headless WirePlumber profile

## Goal

Run stock desktop BAP gate with WirePlumber BlueZ monitor active, using its
built-in `main-systemwide` profile for this remote/headless session.

## Grounded host condition

- Current session is Remote=yes with no logind seat.
- WirePlumber `main` profile gates BlueZ monitor on active seat.
- Installed stock `wireplumber.conf` defines `main-systemwide`, inheriting
  `main` plus `mixin.systemwide-session`; this disables logind support and
  `monitor.bluez.seat-monitoring` without custom rules.
- This is standard WirePlumber mechanism for systemwide/headless sessions.

## Execution

1. Preserve current user-service state.
2. Stop only user `wireplumber.service`; leave PipeWire running.
3. Start installed WirePlumber manually with `--profile main-systemwide`, same
   user/runtime/session bus, log to `/tmp/phase2-wireplumber-systemwide.log`.
4. Prove BlueZ SPA plugin loads, system D-Bus opens, and BAP endpoint
   RegisterApplication/RegisterEndpoint succeeds using debug/D-Bus evidence.
5. Ordinary BlueZ disconnect/reconnect only. No raw HCI or custom endpoint.
6. Run `scripts/bluez-wireplumber-gate.py` for 30 seconds, then 120 seconds.
7. Require PipeWire receiver device/profile/playback sink and receiver stream/
   decode/I2S/fault criteria.
8. Stop manual WirePlumber and restart original user service. Verify all user
   services active and no stray process.

## Gate fixes

- Update preflight to detect whether BlueZ SPA monitor is active rather than
  treating service `active` alone as sufficient.
- When current session lacks active seat and monitor is absent, report host
  prerequisite with exact supported remedy: local active desktop session or
  stock `main-systemwide` profile. Do not write host configuration.
- Correct results to distinguish normal local-desktop `main` and standard
  headless `main-systemwide` operation.
- Retain raw diagnostic logs and no unsupported “receiver conformant” claim
  until stream acceptance passes.

## Failure protocol

If monitor starts but no sink appears, capture exact endpoint registration,
remote PAC bytes, BlueZ selection errors, and PipeWire logs. Make only smallest
unambiguous receiver fix. Stop for material CAP/CAS/codec/QoS decision.

## Constraints

No custom WirePlumber config/rules, package changes, NCS edits, custom endpoint,
`bap_central.py`, raw-HCI, direct ISO writes, mass erase, push, or PR update.
Temporary stock profile and service restart authorized. Commit only on full
30 s + 120 s Phase 2 acceptance; otherwise leave work uncommitted.
