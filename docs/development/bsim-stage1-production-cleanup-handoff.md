# BabbleSim Stage 1 production cleanup

## Remove test concepts from production stats

- Revert `startup_zero`/`startup_plc` fields and functions from
  `src/audio_stats.h/.c`.
- Sink stub owns local startup counters. On each startup-zero push call existing
  `audio_stats_get()` and snapshot `plc_frames`; final invariants use local
  counters plus existing production stats only.
- Real target public stats/API return exactly to pre-BSim shape.

## Client capacity

- Set `CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT=0`.
- Keep `ASE_SNK_COUNT=2` because this Zephyr version requires any nonzero client
  capacity >1. Runtime custom client still stores/discovers/configures one remote
  sink and one TX stream. `BT_AUDIO_TX` derives from SNK capacity, so no source
  capacity is needed.
- If build/runtime fails, return exact assertion/API error; do not re-enable
  source count without grounded source evidence.

## Gate and close

Run Stage1 twice. Require prior deterministic values (or document grounded new
ones), all strict invariants/exits. Run both production builds and relevant
audio_stats unit tests. Mark BabbleSim accepted regular local gate while official
upstream smoke remains PARTIAL. Scope stops here: reconnect/Mode A/B/error
injection duplicate hardware coverage and add low value under unmodeled I2S/FLPR.

Update design/STATUS/AGENTS current status and add concise run command. Commit
cleanup/results. No downloads/packages/NCS edits/push/hardware/security.
