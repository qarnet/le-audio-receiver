# Phase 2 fix handoff — accept valid 7.5 ms LC3 configuration

## Goal

Fix receiver rejection of stock PipeWire's valid 7.5 ms LC3 configuration,
restore strict nonzero-audio acceptance, and pass 30 s + 120 s stock desktop
streams.

## Root cause

Installed NCS v3.3.0 defines:

- `BT_AUDIO_CODEC_CFG_DURATION_7_5 = 0x00`
- `BT_AUDIO_CODEC_CFG_DURATION_10 = 0x01`

`bt_audio_codec_cfg_get_frame_dur()` returns enum value or negative errno.
`src/bt_bap.c` uses `if (ret <= 0)`, so valid 7.5 ms returns 0, logs
`frame dur not set`, leaves decoder unset, then every received ISO SDU logs
`LC3 decoder not ready`. PipeWire LC3 plugin links liblc3 and has encoder
symbols; “host has no LC3 encoder” claim is false.

Authoritative installed references:

- `~/ncs/v3.3.0/zephyr/include/zephyr/bluetooth/assigned_numbers.h`
- `~/ncs/v3.3.0/zephyr/include/zephyr/bluetooth/audio/audio.h`
- `~/ncs/v3.3.0/zephyr/subsys/bluetooth/audio/codec.c`
- `~/ncs/v3.3.0/zephyr/tests/bluetooth/audio/codec/src/main.c`
- `~/ncs/v3.3.0/nrf/applications/nrf5340_audio/src/bluetooth/bt_stream/le_audio.c`

## In scope

1. In `src/bt_bap.c`, use `< 0` for frequency and frame-duration getter error
   checks. Validate frequency/duration conversion helper results `< 0` too.
2. Validate `bt_audio_codec_cfg_get_frame_blocks_per_sdu()` negative errors
   before decoder setup.
3. Ensure ASCS callback returns true negative errno on invalid config and never
   returns enum zero after setting rejection response.
4. Add focused regression coverage for valid 7.5 ms and getter/conversion error
   handling using smallest practical seam. Hardware evidence alone is not
   enough for zero-enum regression.
5. Remove gate weakening: zero received/decoded frames is fatal. Make
   `frame dur not set`, `LC3 decoder not ready`, malformed/decode/I2S/offload
   warnings/faults fatal. Add unit tests proving zero frames and these patterns
   fail.
6. Derive expected frame rate from negotiated frame duration: about 133.3 fps
   for 7.5 ms, 100 fps for 10 ms. Do not hardcode 100 fps for stock client.
7. Correct all Phase 2 docs and prior commit claims. State commit `645df95` was
   rejected during orchestrator review and superseded; do not rewrite/amend it.
8. Run full local gate and both production builds.
9. Build/flash nRF54L15 normally, capture serial before reset, run stock
   WirePlumber `main-systemwide` profile, then strict 30 s and 120 s streams.
10. Require nonzero valid ISO SDUs, initialized decoder, nonzero decoded/rendered
    audio, negotiated-rate consistency, I2S running, and zero warnings/faults.
11. Restore normal WirePlumber service and verify no stray processes.

## Out of scope

CAP/CAS, codec capability expansion, QoS changes, host config/rules, custom
endpoint, raw-HCI, `bap_central.py`, direct ISO, package/NCS edits, mass erase,
nRF5340 hardware flash, or analog audibility claim.

## Verification

```bash
python3 -m unittest scripts/test_bluez_wireplumber_gate.py
bash scripts/test-all.sh
fw-build-5340
fw-build-54l15
git diff --check
```

Then strict stock 30 s + 120 s hardware gates. Preserve raw logs and exact
counters.

## Delivery

Create new correction commit only after all strict criteria pass. Do not amend
`645df95`. No push, merge, or PR update. Return exact files, tests, builds,
hardware counters, warnings, commit, and service restoration.
