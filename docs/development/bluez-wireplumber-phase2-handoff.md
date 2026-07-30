# Phase 2 handoff — stock desktop stream gate

## Goal

Prove updated receiver appears as normal WirePlumber/PipeWire playback sink and
accepts standard PCM playback, with no repository BAP endpoint or ISO injector.

## In scope

### Host gate

Add `scripts/bluez-wireplumber-gate.py` (plus focused unit tests) that:

1. Uses existing system BlueZ, WirePlumber, PipeWire, and command-line/D-Bus
   interfaces only.
2. Never imports/runs `bap_central.py`, registers `MediaEndpoint1`, opens ISO
   sockets, invokes `hci_raw_connect.py`, or writes LC3 SDUs.
3. Accepts receiver address/name, playback duration, receiver log path, and
   optional controller index.
4. Preflights:
   - BlueZ version and running service;
   - powered LE controller with `cis-central` and secure connections;
   - BlueZ experimental ISO UUID enabled;
   - local PACS/ASCS UUID exposure as evidence BAP roles are active;
   - running WirePlumber/PipeWire and active user session.
5. Uses normal BlueZ discovery/connect/pair behavior. Existing valid bond may be
   reused in Phase 2; clean first pairing belongs to Phase 3. No raw-HCI bypass.
6. Requires connected, bonded/paired where applicable, trusted, and
   `ServicesResolved=yes`, with remote PACS/ASCS/VCS UUIDs.
7. Polls `pw-dump`/`wpctl` until receiver Bluetooth device, BAP playback profile,
   and playback sink appear. Report exact objects/properties on timeout.
8. Generates temporary 48 kHz stereo PCM/WAV and plays it to that exact sink via
   `pw-play` or `pw-cat` for requested duration.
9. Requires sink remains present and playback command succeeds.
10. Parses captured receiver log for ASCS configuration/start, nonzero audio
    frames, I2S DMA start, expected steady rate, and zero malformed/decode/I2S/
    offload integrity faults. Keep raw logs and exact failure evidence.
11. Exits nonzero on missing host prerequisite, connection failure, missing
    PipeWire objects, playback failure, or receiver fault. Classify host
    prerequisite failures separately from receiver interoperability failures.

Do not alter host WirePlumber configuration to force BAP roles. Current stock
controller UUID exposure must be enough; if not, return blocker evidence.

### Hardware execution

- Build nRF54L15 pristine.
- Start `/dev/ttyACM0` capture before flash/reset using repo helper.
- Flash with `fw-flash-54l15`; no mass erase/recover.
- Reconnect current paired receiver through ordinary BlueZ.
- Run stock desktop gate for an initial 30-second diagnostic stream.
- If green, run 120-second acceptance stream.
- Preserve `/tmp` receiver, bluetoothd, WirePlumber, PipeWire, and gate logs.

### Documentation

- Record exact host versions, commands, PipeWire object/profile/node names,
  receiver counters, duration/rate, and failure-free evidence in a Phase 2
  results document.
- Correct Phase 1 wording: contexts persist because firmware no longer changes
  them; do not claim Zephyr restores them on disconnect.

## Out of scope

- Fresh bond deletion or pairing-reset firmware command (Phase 3).
- CAP/CAS, codec, QoS, advertising, or host policy changes without captured
  evidence.
- `bap_central.py` fallback.
- nRF5340 hardware flash.
- Analog audibility claim.

## Acceptance

- WirePlumber creates receiver device and BAP playback profile.
- PipeWire creates exact receiver playback sink.
- Standard PCM playback reaches receiver for 30 seconds, then 120 seconds.
- Receiver reaches ASCS streaming, nonzero decode/I2S, near 100 fps, zero
  malformed/decode/I2S/offload integrity faults.
- No custom endpoint/raw-HCI/direct-ISO path participates.
- Gate has focused parser/discovery unit tests and `git diff --check` passes.

## Failure protocol

If stock desktop still exposes no sink, stop before speculative firmware edits.
Capture:

- `bluetoothctl info/show`;
- `busctl introspect` device;
- `pw-dump`, `wpctl status`, `wpctl inspect` candidates;
- bluetoothd/WirePlumber/PipeWire journals;
- receiver serial log;
- optional passive `btmon` trace.

Return exact first missing layer. Do not enable CAP/CAS or add host rules in
Phase 2.

## Delivery

Commit only if Phase 2 acceptance passes. If blocked, leave changes uncommitted
and return evidence for orchestrator diagnosis. No push, merge, PR update,
mass erase, package install, or NCS edits.
