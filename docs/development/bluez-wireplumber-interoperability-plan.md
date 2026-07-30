# Standard BlueZ/WirePlumber interoperability plan

## Goal

Normal Linux desktop discovers, pairs, connects, exposes, and streams audio to
receiver through stock BlueZ, WirePlumber, and PipeWire behavior.

Final gate forbids repo `bap_central.py`, custom `MediaEndpoint1`, direct ISO
socket writes, `--peer-addr`, and raw-HCI connection helpers.

## Baseline evidence

- BlueZ 5.86 sees receiver, pairs, bonds, trusts, connects, and resolves GAP,
  GATT, VCS, PACS, and ASCS.
- WirePlumber 0.5.14 / PipeWire 1.6.5 creates no audio device or sink.
- Firmware clears Sink Available Audio Contexts to `NONE` immediately on ACL
  connect, before desktop PACS/ASCS policy runs.
- Lab harness registers its own endpoint and selects fixed LC3/QoS, so prior
  success did not validate desktop policy.

## Phase 1 — standard PACS availability semantics ✅ DONE (2026-07-30)

Keep supported/available sink contexts truthful during ACL connection and
PACS/ASCS discovery. Match upstream bare BAP server behavior: Media remains
available until actual resource policy says otherwise. Do not use connection
existence as ASE ownership. Keep standard ASCS service-data advertising and
SC Just Works pairing. Keep CAP/CAS disabled for this phase.

Validation:

- focused source/unit checks for context lifecycle;
- both production builds;
- full local unit/BabbleSim gate;
- no warnings beyond recorded NCS/devicetree diagnostics.

Acceptance:
- `src/bt_bap.c`: removed `bt_pacs_set_available_contexts(BT_AUDIO_DIR_SINK,
  BT_AUDIO_CONTEXT_TYPE_NONE)` from `connected()` callback and context restore
  from `disconnected()`. Available contexts persist from initial registration.
- BSIM test: PACS assertion at PASS point verifies available sink contexts
  non-NONE after connection + 100-frame stream.
- All existing tests pass; both nRF5340/nRF54L15 builds clean.

## Phase 2 — stock desktop gate

Add reproducible host gate using only standard commands/APIs:

1. Verify BlueZ experimental ISO support and WirePlumber `bap_source` support.
2. Discover by ASCS advertisement/name through BlueZ.
3. Pair using normal BlueZ agent and `Device.Pair()`/`bluetoothctl` behavior.
4. Connect through BlueZ, without raw HCI.
5. Require `ServicesResolved=yes` and PACS/ASCS/VCS UUIDs.
6. Require WirePlumber/PipeWire Bluetooth device, BAP profile, and playback
   sink.
7. Play generated 48 kHz stereo PCM through standard `pw-play`/`pw-cat`.
8. Require receiver ASCS start, nonzero ISO RX/decode, I2S start, steady frame
   rate, and zero decode/I2S/offload faults.

Gate must fail loudly when host lacks BAP roles or experimental ISO support;
host prerequisites cannot be mistaken for receiver failure.

## Phase 3 — pairing and reconnect lifecycle

Validate clean first pairing, disconnect/reconnect with persisted bond, stream
restart, and post-disconnect advertising. Add a deliberate development pairing
reset command using standard Zephyr bond APIs if needed to create clean test
state without mass erase. No production auto-delete of bonds.

## Phase 4 — compatibility expansion only from evidence

If bare BAP still fails, capture bluetoothd/WirePlumber logs and btmon trace.
Enable CAP Acceptor/CAS, broaden LC3 capabilities, or adjust QoS only when trace
shows exact client requirement. No speculative services or custom host policy.

## Final acceptance

- Stock BlueZ/WirePlumber setup pairs and connects without repo host harness.
- PipeWire exposes receiver as playback sink.
- Standard PCM playback reaches receiver for at least 120 seconds at 100 fps.
- Disconnect/reconnect and second playback pass.
- Receiver reports zero malformed/decode/I2S/offload integrity faults.
- nRF5340 build remains green.

## Non-scope

- A2DP/BR-EDR fallback.
- Phone interoperability.
- Analog audio-quality claims.
- CAP/CAS, TMAS, CSIS, or extra codecs without captured need.
- Replacing stock desktop policy with repository-specific WirePlumber rules.
