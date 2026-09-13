# RH3-ModeA6 RTN/duty isolation handoff

Status: approved one-run hardware diagnostic. Tests the last unsampled
structural variable for the delivery collapse: per-CIS retransmission
number (RTN), i.e. CIS subevent count/duty.

## Grounding

Exonerated so far (each with runner-validated or build-proven evidence):
FLPR offload (ModeA2: fails with offload disabled); central layout policy
(base hci_ipc config already low-latency; no run spent); receiver host ISO
RX pool depth (ModeA5: RX=6 delivered 109 vs 113, identical signature);
scored-content onset (ModeA4: progressive 23%->46%->100% LOST over ~2 s,
30 ms lead before scored boundary, Mode A slot 1 dead from sequence 0).

Remaining locus: air-interface timing duty between the SW-split nRF5340
central and the SDC nRF54L15 peripheral. Duty structure facts: mono passes
with `nse=6` at `c_max_pdu=120`, single CIS, `cig_sync_us=5304`. Mode B
fails with `nse=6` at `c_max_pdu=240`, `cig_sync_us=8184`. Mode A fails
with two CIS at `c_max_pdu=120`, `cis_sync_us=5304/2652`. The failing
shapes are exactly those with larger per-event on-air time. A
sync/timing-window mechanism is consistent with: progressive loss
build-up, permanent `rx_unreceived` growth at ~100 events/s with
`crc_error~0-1`, and `rx_unreceived` exceeding `tx_last_subevent`.

Hypothesis: if the collapse is event-duty/timing-driven, halving the
subevent count (RTN 5 -> 1, `nse` 6 -> 2) recovers delivery. If it fails
with the same signature, long-PDU/dual-CIS airtime itself is isolated
further and the next phase would move to PDU-size or PHY ladder tests.

## Exact changes

### Source firmware (diagnostic-gated, default-preserving)

1. `hil/source/app/Kconfig`: add

```kconfig
config HIL_SOURCE_QOS_RTN
	int "HIL source CIS retransmission number (RTN)"
	default 5
	range 0 5
	help
	  RTN used in the source BAP QoS presets. Default 5 matches the
	  original frozen presets (nse=6). Diagnostic-only knob: the
	  receiver transport investigation varies it in fragments; normal
	  builds must not set it.
```

2. `hil/source/app/src/hil_source_bap.c`: replace the two literal `5u`
   RTN arguments in `BT_BAP_QOS_CFG_UNFRAMED(...)` with
   `CONFIG_HIL_SOURCE_QOS_RTN` (cast as the macro expects, e.g.
   `(uint8_t)CONFIG_HIL_SOURCE_QOS_RTN` - verify the macro arg type and
   match existing preset style; do not change any other QoS field).

3. Fragment `tests/hil/source-rtn1.conf`:

```text
CONFIG_HIL_SOURCE_QOS_RTN=1
```

Apply to the SOURCE build only:
`fw-build-hil-source` must consume it - check how fw-build-hil-source
passes EXTRA_CONF (mirror the receiver build helper pattern; if the
helper has no EXTRA_CONF support, use the documented
`-DEXTRA_CONF_FILE=...` pass-through exactly as fw-build-54l15 does;
do not modify the helper if the pass-through already works).

4. Native source tests: the QoS preset tests (tests/unit/hil_source_* and
   any Twister suite asserting `.qos.rtn == 5`) must keep passing for the
   default build; if a test hardcodes RTN 5, it must read the Kconfig like
   the firmware does (via the same macro/constant) - do not weaken the
   assertion. If any native test cannot see Kconfig defaults, assert
   against the default constant explicitly.

## Build proof

- Source diagnostic build with the fragment; prove resolved source app
  config `CONFIG_HIL_SOURCE_QOS_RTN=1` and that the preset macro consumes
  it (grep the generated config + confirm in code; record source app and
  CPUNET hashes - both change for the app core; CPUNET unchanged).
- Receiver: NORMAL build (current HEAD, no fragment).
- Double-build determinism proof for the diagnostic source app image.

## One hardware run

```text
run ID: rh3-modeb-rtn1-20260904 (validate unused first)
row:    rh3.fresh_mode_b_48_4_1
```

Normal runner invocation (no special flags). Outer timeout 3600000 ms;
status 0/1/130 immutable; never rerun.

## Classification

Runner-validated verdict only:

- PASS under frozen limits: event duty/subevent count implicated; next
  phase is the production/fixture QoS decision (document RTN tradeoffs)
  plus full matrix attempt with the accepted fixture QoS.
- FAIL with same signature: RTN/duty exonerated at this shape; next phase
  designs a PDU-size or PHY ladder (e.g. forced 1M PHY row) to isolate
  long-PDU airtime interop.
- Other boundary: record, classify, stop for review.

Also record the receiver ISO tail `nse` for this run: it MUST drop from 6
to 2 (sanity proof the QoS change took effect on air); if it does not,
the run is misconfigured - stop and report instead of classifying.

## Result documentation and commit

`docs/development/system-hil-rh3-modea6-rtn1-result.md` (canonical),
resume-state update. Commit exactly:

```text
docs(hil): record RTN/duty isolation verdict
```

Include Kconfig, preset change, fragment, handoff, result, resume-state.
No attribution footers, no push.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global
`__ASSERT()`). No NCS patches, no receiver firmware changes, no
rows/limits/matrix changes, no evidence mutation, no retry. Default
build behavior (RTN 5) must be byte-equivalent in behavior; the Kconfig
default must not change the normal image's QoS.