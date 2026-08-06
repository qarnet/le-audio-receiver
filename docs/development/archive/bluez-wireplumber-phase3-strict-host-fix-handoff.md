# Phase 3 correction — strict playback and autonomous stock host lifecycle

## Goal

Remove remaining Phase 3 acceptance bypasses, own WirePlumber profile lifecycle,
and rerun full sequence with continuous receiver evidence.

## Required fixes

1. Own stock WirePlumber lifecycle:
   - detect active local logind seat;
   - use normal service/profile when active;
   - for current headless session, stop user service and launch installed
     `wireplumber --profile main-systemwide`;
   - wait for BlueZ SPA monitor and restore original service in every cleanup.
2. Make BlueZ disconnect failure fatal.
3. Make explicit advertising restart evidence fatal. Use continuous serial event
   capture or fresh D-Bus scan event; do not call a helper then ignore false.
4. Remove global `pkill -f bt-agent`. Stop only process group owned by gate.
5. Replace weak playback parser with strict Phase 2 checks. Capture UART
   continuously from before `pw-play` through stream summary, without resetting
   away startup evidence. Require:
   - ASCS config/start;
   - exact `I2S DMA started`;
   - explicit SDUs/decoded > 0;
   - duration-consistent SDU count for negotiated frame duration;
   - decode_err/i2s_underrun/stream_reset/malformed/offload faults all zero.
   Prefer direct reuse of `BluezWirePlumberGate.parse_receiver_log()` over
   duplicated weaker logic.
6. Preserve independent fresh raw logs for all three playbacks. Add runtime
   timestamps and counters to results.
7. Add tests proving disconnect failure, missing advertising evidence, missing
   I2S start, short SDU count, WirePlumber launch failure, early cleanup, and
   no unrelated-agent kill all fail safely.
8. Correct docs: prior Phase 3 commits rejected until this rerun passes.
9. Run Phase 2/3 tests, canonical gate, both builds if firmware unchanged may
   use recent build evidence plus `git diff --check`; rerun full hardware clean
   pairing → 30 s playback → reconnect 30 s → reset reconnect 10 s.

## Constraints

No custom MediaEndpoint, raw-HCI, `bap_central.py`, direct ISO, custom host
rules, package/NCS edits, CAP/CAS, mass erase, global process kills, push/PR,
amend/rewrite. New correction commit only after autonomous strict pass and clean
worktree.
