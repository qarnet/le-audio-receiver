# BabbleSim Stage 1 review fixes

## Restore sink-only architecture

- Remove `CONFIG_REGISTER_SRC_PAC` from production `bt_bap.c`, test Kconfig and
  prj.conf. Receiver remains sink-only exactly like hardware.
- Client config exactly:
  `ASE_SNK_COUNT=1`, `ASE_SRC_COUNT=0`, `GROUP_STREAM_COUNT=1`,
  `ISO_MAX_CHAN=1`.
- Simplify custom client to sink-only remote discovery/group: one remote sink,
  one TX stream, one pair with `tx_param`, no source discovery/config/stream,
  no tolerance for empty source-direction traffic.

## Receiver oracle

- Zero-energy decoded block is immediate FAIL; no tolerated error counter.
- At PASS require production `audio_stats`: decode_errors=0, plc_frames=0,
  total_frames>=100; malformed=0; pushes_after_stop=0.
- Replace XOR frame CRC with ordered FNV-1a (or CRC chained with prior CRC and
  frame index) over all sample bytes so 100 identical sine blocks cannot cancel.
  Require final hash nonzero and not initial seed. Report energy min/max and hash.

## Strict runners/docs

- Official smoke propagates nonzero upstream result and Stage0 remains PARTIAL.
- Stage1 runner actually redirects receiver/client/PHY stdout+stderr to printed
  `/tmp` paths, sets timeout, records each exit separately, and fails if expected
  PASS markers/counters are absent even when process exits zero.
- Fix trailing whitespace/diff check. Commit omitted Stage0 review handoff.

## Gate

Rebuild both dual-core binaries. Run Stage1 twice with unique IDs. Require:

- receiver/client/PHY each exit zero;
- client TX >=100; receiver pushes>=100;
- errors/decode_errors/PLC/malformed/after_stop all zero;
- nonzero ordered PCM hash and positive energy;
- sink-only PACS/ASCS (no source PAC/ASE); one CIS.

Update STATUS/design/results only after pass. Commit fixes/tests/docs. No NCS
source/component revision/download/package/push/hardware/security change.
