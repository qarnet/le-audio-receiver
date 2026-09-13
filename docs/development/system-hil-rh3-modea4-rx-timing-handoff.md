# RH3-ModeA4 receiver delivery-death timing diagnostic handoff

Status: REVISED phase after ModeA3's layout-policy hypothesis was killed
pre-build by free evidence (the hci_ipc central base config already sets
`CONFIG_BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY=y`; the SW-split source has
always run compact layout; overlay line reverted, no hardware spent). This
phase adds one bounded receiver-side timing instrument and runs it once on
Mode B.

## Evidence summary driving this design (all runner-validated or from
immutable evidence)

- Mono 10 ms: perfect delivery (12644/12644).
- Mode B 10 ms (2 runs: modeb-control, older controls): `rx_valid=113`,
  scored payload delivered ~0; source terminal `sub=12644 sc=12000 cb=12644
  out=0 first_errno=0` (source delivered everything, callbacks fired for
  every SDU); receiver `rx_unreceived=13288 > tx_last_subevent=11376`;
  `crc_error=0`; FLPR healthy; CPU alive (PCLK diags every 5 s to the end).
- Mode A 10 ms (2 runs, one offload-disabled): slot 0 ~130 valid, slot 1
  ZERO valid from the start; same clean-controller signature.
- Mode B 7.5 ms (H40/H42): rx_valid 23-24.
- Mode B 10 ms preamble = 144 silence SDUs; delivered 113 ≈ preamble minus
  losses. Mode A preamble = 144 per CIS; slot 0 got ~130. The delivered
  counts match the SILENCE-preamble portion; scored-carrier onset
  coincides with delivery death. 7.5 ms delivered only 24 of 192 preamble
  SDUs, so a pure "preamble survives" rule is too simple: there the death
  begins even earlier.
- The source signal contract: preamble is frame-aligned silence, scored
  phase is deterministic 120 ms binary amplitude envelopes with ramps,
  tail is encoded silence. Same encoder, same fixed SDU lengths
  throughout; only PCM content differs.

## Open question this run answers

WHERE and WHEN does receiver-side valid delivery die, with per-SDU
resolution?

- If valid SDUs arrive through the preamble and stop at scored onset:
  failure is correlated with scored-payload content or the scored-phase
  source behavior (pacing/timing change at phase switch), not with
  elapsed time.
- If valid SDUs stop at a fixed time offset regardless of phase: failure
  is time/state-triggered (e.g. first drift correction, first GRTC
  reschedule, first FLPR submit) and content-independent.
- If valid SDUs trickle sporadically after onset: intermittent window
  loss, pointing at subevent timing.

## Instrument (receiver, diagnostic-only, config-gated)

Add one default-off Kconfig `HIL_RX_TIMING_TRACE` (pattern:
`HIL_BAP_ENABLE_TRACE`):

```kconfig
config HIL_RX_TIMING_TRACE
	bool "HIL receiver RX delivery timing trace"
	default n
	depends on SOC_NRF54L15 && BT && BT_ISO && LOG
	help
	  Temporary nRF54L15 HIL-only diagnostic. Logs each ISO RX callback's
	  disposition and timing for the first N callbacks and then one
	  summary line per second of wall time: counts of valid/lost/error
	  callbacks and the receiver-clock timestamp of the last valid SDU.
	  Keep disabled in normal builds. It does not establish a root cause
	  or a repair.
```

Implementation in `src/bt_bap.c` `stream_recv` (and only there), gated by
`#if defined(CONFIG_HIL_RX_TIMING_TRACE)`:

1. Per-callback (bounded): first 200 callbacks each log
   `HILRX cb=<n> slot=<idx> flags=0x<xx> ts=<info->ts> seq=<seq_num>` at
   INF.
2. Continuous (every callback, but emitted once per second): a static
   accumulator logs `HILRX t=<k_uptime_get_32>/1000>s valid=<v>
   lost=<l> err=<e> nots=<nt> last_valid_ts=<ts>` - one line per wall
   second, so a 140 s run emits ~140 lines total.
3. No state mutation, no work submission, no allocation; read-only
   instrumentation inside the existing callback before the gate check
   (log ALL callbacks including gate-closed ones, since the death happens
   before any gate change).
4. Keep the existing gate-independent counter behavior untouched.

The H42 marker (`HIL_BAP_ENABLE_TRACE`) stays OFF for this run (enable trace
only if its log rate would collide; it would not, but keep the run minimal).

Fragment `tests/hil/receiver-rx-timing-trace.conf`:

```text
CONFIG_HIL_RX_TIMING_TRACE=y
CONFIG_WARN_EXPERIMENTAL=y
```

## Host parsing

No runner/parser changes: the `HILRX` lines are unprefixed log lines; the
warning scan (`receiver.py` `WARNING_PATTERNS`) must be verified not to
match them (no LOG_WRN/err/fault/assert words - confirm by reading the
pattern list; `flags=0x..` and `ts=` are safe). The lines live in
`receiver-console.bin` evidence for offline analysis; the result doc
extracts them read-only.

## Build proof

- Diagnostic receiver build with the fragment; prove resolved config lines
  (`CONFIG_HIL_RX_TIMING_TRACE=y`, normal offload enabled, traces unset,
  `CONFIG_BT_ISO_RX_BUF_COUNT=3`).
- Record HEAD, hashes (HEAD-dependent CPUAPP; double-build determinism
  proof for the flashed diagnostic image).
- Normal build restoration afterwards; prove
  `CONFIG_HIL_RX_TIMING_TRACE` unset and record normal hashes.

## One hardware run

```text
run ID: rh3-modeb-rxtiming-20260904 (validate unused first)
row:    rh3.fresh_mode_b_48_4_1
```

Normal row execution (no special runner flags; limits still enforced - the
row is expected to FAIL on limits; that is acceptable, the timing evidence is
the point; capture the failure evidence exactly as the runner retains it).

## Read-only analysis (in the result doc)

Extract from `receiver-console.bin`:

- last valid-SDU receiver-clock timestamp vs streaming-start timestamp;
- valid count during preamble window (first 144 SDUs after CIS start) vs
  after;
- whether death aligns with scored onset (source monotonic boundary between
  preamble and scored is derivable from the source HELLO/status records and
  the known 144-SDU preamble contract) or with a fixed offset;
- LOST-callback rate before/after death (from the per-second lines).

State which of the three hypothesis branches the data supports. No
root-cause claim.

## Result documentation and commit

`docs/development/system-hil-rh3-modea4-rxtiming-result.md` (canonical),
resume-state update, Kconfig + fragment + handoff committed. Commit
exactly:

```text
docs(hil): record RX delivery-death timing diagnostic
```

No attribution footers, no push.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global
`__ASSERT()`). No NCS patches, no production behavior changes, no
rows/limits/matrix changes, no evidence mutation, no retry. The instrument
must compile out completely when the config is off.