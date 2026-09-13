# RH3-ModeA8 TX pacing-regime isolation handoff

Status: approved one-run hardware diagnostic. Tests whether the scored-onset
delivery death is triggered by the source host's TX submission pacing regime
or by scored payload content.

## Grounding

Layered evidence picture:

- Layer (a), RF/duty margin during preamble: 2M delivers 76-90% of the
  144-SDU preamble (113/109/130 across rx6/rtn1/control runs); 1M delivers
  the FULL preamble (144/144, ModeA7). Loss fraction scales with
  modulation/duty - classic air-interface margin.
- Layer (b), permanent death at scored onset: EVERY failing run dies at or
  within 30 ms of the first scored SDU, at BOTH PHYs, at BOTH RX pool
  depths, at BOTH RTN values, with FLPR on and off. The peripheral then
  flags ~100% of subsequent payloads LOST forever with `crc_error~0` while
  the central transmits essentially every event
  (`tx_last_subevent ~= full count`).

Two candidate triggers for layer (b):

1. scored payload content (LC3-encoded carriers vs encoded silence);
2. the source host's TX submission regime, which changes exactly at the
   scored boundary: the preamble is pre-queued (burst, deep queue), while
   the scored phase runs the `sem_tx_wake` completion-pacing loop capped at
   `HIL_SOURCE_TX_OUTSTANDING_TARGET == 3` - a fundamentally different
   submission timing profile.

Hypothesis: if pacing regime is the trigger, deepening the scored-phase
queue (target 6) restores delivery despite identical scored content. If
content is the trigger, target 6 changes nothing.

Why 6: the source controller pool is 6 ISO TX buffers
(`CONFIG_BT_CTLR_ISO_TX_BUFFERS=6`); a target of 6 lets the host keep the
full controller queue primed - the same early, queue-full regime as the
preamble.

## Exact changes

1. `hil/source/app/Kconfig`: add

```kconfig
config HIL_SOURCE_TX_OUTSTANDING_TARGET
	int "HIL source TX outstanding target per stream"
	default 3
	range 1 6
	help
	  Per-stream in-flight SDU cap for the completion-paced TX loop.
	  Default 3 matches the original source-fixture backpressure design.
	  Diagnostic-only knob for the receiver transport investigation;
	  normal builds must not set it.
```

2. `hil/source/app/src/hil_source_app.h`: change the literal macro to

```c
#ifndef CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET
#define HIL_SOURCE_TX_OUTSTANDING_TARGET 3U
#else
#define HIL_SOURCE_TX_OUTSTANDING_TARGET ((uint32_t)CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET)
#endif
```

   (Check how Kconfig reaches this header: if the app already includes
   generated autoconf via its build, the `#ifndef` fallback keeps native
   test builds - which may not run Kconfig fragments - identical. If a
   cleaner pattern exists in the repo for Kconfig-driven macros in app
   headers, use it and report.)

3. Fragment `tests/hil/source-txout6.conf`:

```text
CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=6
```

4. Native test handling: `tests/unit/hil_source_app/src/test_hil_source_app.c`
   line ~1702 asserts `HIL_SOURCE_TX_OUTSTANDING_TARGET == 3U` and uses the
   macro elsewhere. The native suite runs the default config, so the
   default-path macro (3U) must keep the suite green unchanged. If any test
   cannot compile under the fragment, note it; the fragment is only used in
   the hardware build, not the native suite.

## Build proof

- Diagnostic source build with ONLY `source-txout6.conf` (PHY back to 2M
  default, RTN default 5).
- Prove resolved config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=6`,
  `CONFIG_HIL_SOURCE_QOS_PHY=2`, `CONFIG_HIL_SOURCE_QOS_RTN=5`.
- Record source app hash; CPUNET unchanged `4e4b82f5...`; double-build
  determinism proof.
- Receiver: NORMAL current-HEAD build; record hashes.

## One hardware run

```text
run ID: rh3-modeb-txout6-20260904 (validate unused first)
row:    rh3.fresh_mode_b_48_4_1
```

Normal runner invocation, outer timeout 3600000 ms, status immutable,
never rerun.

Sanity: the receiver QoS log must show `rtn 5`, `phy 0x02`, `sdu 240`
(2M/RTN5 baseline restored); the source active status during scored must
show `out=6` or the target depth while streaming (per-stream `out` field
in the source active status record) - confirm the pacing regime actually
deepened. If `out` never reaches near 6, report it as a misconfiguration
suspect instead of classifying.

## Classification

Runner-validated verdict only:

- PASS under frozen limits: TX pacing regime implicated; next phase is
  the fixture decision (raise the target permanently in the source
  fixture with native-test updates) plus full-matrix attempt at the
  accepted fixture config.
- FAIL same signature (death at/near scored onset): pacing regime
  exonerated; scored content is the remaining trigger candidate; stop for
  review - the next phase (content analysis of LC3-encoded scored frames)
  needs new design.
- Other boundary: record, classify, stop.

## Result documentation and commit

`docs/development/system-hil-rh3-modea8-txout6-result.md` (canonical),
resume-state update. Commit exactly:

```text
docs(hil): record TX pacing-regime isolation verdict
```

Include Kconfig, header change, fragment, handoff, result, resume-state.
No attribution footers, no push.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global
`__ASSERT()`). No NCS patches, no receiver firmware changes, no
rows/limits/matrix changes, no evidence mutation, no retry. Default
build (target 3) must be behavior-identical.