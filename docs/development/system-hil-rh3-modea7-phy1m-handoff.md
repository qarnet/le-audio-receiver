# RH3-ModeA7 PHY isolation handoff

Status: approved one-run hardware diagnostic. Tests the PHY variable for
the Mode B delivery collapse: 2M (failing everywhere) vs 1M.

## Grounding

Exonerated so far (each runner-validated or build-proven): FLPR offload
(ModeA2), central layout policy (base config already low-latency),
receiver ISO RX pool depth (ModeA5: RX=6 delivered 109 vs 113), subevent
duty (ModeA6: RTN=1/nse=2 delivered 130, same signature), scored-content
onset (ModeA4: progressive degradation, 30 ms lead, Mode A slot 1 dead
from seq 0).

Consistent facts: mono 10 ms 2M/120B delivers perfectly; Mode B 2M/240B
dies progressively in ~2 s to permanent ~100 LOST/s with `crc_error~0`
and `rx_unreceived > tx_last_subevent`; the peripheral's radio keeps
hearing subevents but never validates payloads.

Hypothesis: if 1M PHY recovers delivery, the defect is 2M long-PDU
receive-window interop between the SW-split central radio and the SDC
peripheral. If 1M fails with the same signature, PDU length per se is
further isolated (240B payload handling) independent of modulation rate.

## Exact changes (source firmware, diagnostic-gated, default-preserving)

Same pattern as ModeA6:

1. `hil/source/app/Kconfig`: add

```kconfig
config HIL_SOURCE_QOS_PHY
	int "HIL source CIS PHY (1 or 2)"
	default 2
	range 1 2
	help
	  PHY used in the source BAP QoS presets. Default 2 (2M) matches the
	  original frozen presets. Diagnostic-only knob for the receiver
	  transport investigation; normal builds must not set it.
```

2. `hil/source/app/src/hil_source_bap.c`: switch the two Mode B presets
   (10 ms and 7.5 ms; the mono presets stay as-is only if they are
   distinct definitions - CHECK: if the mono presets share the same
   macro lines, gate them identically so behavior stays consistent)
   from `BT_BAP_QOS_CFG_UNFRAMED(...)` to
   `BT_BAP_QOS_CFG(_interval, BT_BAP_QOS_CFG_FRAMING_UNFRAMED,
   PHY_SELECTOR, _sdu, _rtn, _latency, _pd)` where PHY_SELECTOR maps
   the Kconfig int:

```c
#define HIL_SOURCE_QOS_PHY_SELECTOR \
	((CONFIG_HIL_SOURCE_QOS_PHY == 1) ? BT_BAP_QOS_CFG_1M : BT_BAP_QOS_CFG_2M)
```

   (Verify `BT_BAP_QOS_CFG_1M` exists in installed bap.h; use the exact
   installed enum name. Change no other QoS field.)

3. Fragment `tests/hil/source-phy1m.conf`:

```text
CONFIG_HIL_SOURCE_QOS_PHY=1
```

4. Native source suites must stay green for the default build
   (RTN/PHY default presets unchanged); adjust any hardcoded-PHY
   assertion the same way as ModeA6's RTN handling (assert against the
   Kconfig-driven macro; no weakening).

## Build proof

- Diagnostic source build with BOTH the ModeA6 RTN fragment? NO - RTN
  stays at default 5 for this run: fragment is ONLY `source-phy1m.conf`.
- Prove resolved source app config `CONFIG_HIL_SOURCE_QOS_PHY=1` (and
  `HIL_SOURCE_QOS_RTN=5`).
- Record source app hash (diagnostic), CPUNET unchanged
  (`4e4b82f5...`), double-build determinism proof.
- Receiver: NORMAL current-HEAD build, record hashes.

## One hardware run

```text
run ID: rh3-modeb-phy1m-20260904 (validated unused)
row:    rh3.fresh_mode_b_48_4_1
```

Normal runner invocation, outer timeout 3600000 ms, status immutable,
never rerun.

Mandatory on-air sanity: receiver ISO tail must show `c_phy=1`
(enum value for 1M - verify the receiver's iso-info `central.phy`
reporting maps 1M as `1` and confirm against the receiver log's QoS line
`phy 0x01`). If `c_phy` is still 2, the run is misconfigured - STOP and
report instead of classifying.

## Classification

Runner-validated verdict only:

- PASS under frozen limits: 2M long-PDU interop implicated; next phase
  documents the fixture-level decision (e.g. source QoS 1M for the HIL
  fixture while filing the 2M interop question with Nordic) and the full
  matrix attempt at the accepted fixture QoS.
- FAIL same signature: modulation-rate exonerated; locus narrows to
  240-byte PDU handling itself; next phase is a PDU-size ladder
  (mono-at-240B would need a new preset; likely instead a
  GATT/unicast-variant experiment) - stop for review at that point, do
  not design it in this phase.
- Other boundary: record, classify, stop.

## Result documentation and commit

`docs/development/system-hil-rh3-modea7-phy1m-result.md` (canonical),
resume-state update. Commit exactly:

```text
docs(hil): record PHY isolation verdict
```

Include Kconfig, preset change, fragment, handoff, result, resume-state.
No attribution footers, no push.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global
`__ASSERT()`). No NCS patches, no receiver firmware changes, no
rows/limits/matrix changes, no evidence mutation, no retry. Default
build (2M) must be behavior-identical.