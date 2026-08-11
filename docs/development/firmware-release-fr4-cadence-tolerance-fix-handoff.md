# FR4 cadence tolerance fix handoff

Date: 2026-08-10

## Goal

Fix four deterministic timestamp-cadence RESYNC warnings exposed by the first
local nRF5340 strict-mono validation run. Preserve bounded omission inference,
keep event-count ambiguity impossible, and add enough structured diagnostics to
classify any future RESYNC without another blind hardware run.

This handoff is code/tests/docs only. Do not run hardware, flash, alter release
state, push, merge, or change VERSION. Hardware rerun follows Orchestrator
review under the already-approved local hardware-validation scope.

## Grounding

Retained run: `/tmp/opencode/fr4-cadence-local-b6jNTm`.

Row 1 facts:

- central: one-CIS mono, 120-byte SDU, 12000 frames / 120 s / 100 fps;
- receiver: `SDUs=8881 decoded=11993 plc=3112 decode_err=0
  i2s_underrun=0 stream_reset=0 empty_sdu=0`;
- timestamp cadence concealed 3112 controller-side omissions and eliminated
  all 225 prior I2S restarts;
- four cadence RESYNC warnings remained, leaving seven output events
  unconcealed; exact current timestamps and logs are recorded in
  `metadata/row1-failure-analysis.md`;
- existing warning reports current timestamp only, so it cannot distinguish
  duplicate, delivered-position conflict, non-integral delta, or over-bound
  gap.

Installed NCS v3.3.0 source:

- `zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_peripheral_iso.c`,
  `isr_rx()`: SW Split peripheral ISO RX derives timestamp from local
  RTC/radio-timer anchor measurement, then applies nominal ISO-interval
  corrections. It does not receive a perfect mathematical SDU grid from the
  peer.
- `zephyr/subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/ticker.h`: nRF5340
  ticker is 32768 Hz; one tick is 30.517578125 us. Use 32 us as rounded-up
  one-tick plus capture-quantization base tolerance.
- `zephyr/subsys/bluetooth/controller/ll_sw/pdu.h`: one SCA endpoint may be up
  to 500 ppm.
- `zephyr/subsys/bluetooth/controller/ll_sw/ull_peripheral_iso.c`, CIG window
  widening: controller budgets `lll_clock_ppm_local_get() +
  lll_clock_ppm_get(peer_sca)`. Both endpoints can contribute up to 500 ppm,
  so use conservative combined 1000 ppm.
- Timestamp error scales with elapsed event span. Existing fixed 10 us was a
  single-event-style tolerance and is not valid across a gap of multiple ISO
  intervals.

For accepted inference bound `ISO_SEQ_MAX_CONCEAL=8`, largest normal measured
span with one current callback is nine intervals. Worst 10 ms clock budget is
90 us, plus 32 us base = 122 us. This remains far below half-interval ambiguity
(5000 us for 10 ms, 3750 us for 7.5 ms).

## Exact implementation

### Scaled tolerance

In `src/audio_iso_seq.h`, replace fixed
`ISO_TS_DELTA_TOLERANCE_US` contract with:

```c
#define ISO_TS_BASE_TOLERANCE_US 32U
#define ISO_TS_MAX_COMBINED_SCA_PPM 1000U
```

In `audio_iso_cadence_update()`, after nearest `event_count` is known, compute
using 64-bit integer arithmetic:

```text
drift_budget_us = ceil(event_count * interval_us * 1000 / 1000000)
raw_tolerance_us = 32 + drift_budget_us
ambiguity_cap_us = interval_us / 4
tolerance_us = min(raw_tolerance_us, ambiguity_cap_us)
```

Requirements:

- use overflow-safe `uint64_t` intermediates;
- for validated 7500/10000 us intervals cap is nonzero;
- for arbitrary tiny nonzero intervals where `interval_us / 4 == 0`, tolerance
  becomes zero rather than underflowing;
- retain nearest-integer event count and existing `event_count < delivered`
  rejection;
- retain `ISO_SEQ_MAX_CONCEAL` bound and no synthesis for over-bound gap;
- retain backward-timestamp WRAP rebase and zero-interval RESYNC;
- accept non-integral delta only when absolute error is `<= tolerance_us`;
- never use audio drift controller output, floating point, target-specific
  Kconfig, or platform conditionals. This is protocol/controller clock-bound
  tolerance, independent of local audio PCLK drift.

Quarter-interval cap is deliberately stricter than mathematical half-interval
uniqueness. Do not widen to or beyond half interval and do not conceal on a
RESYNC.

### Structured observation

Add pure diagnostic output without adding another global/state owner:

```c
enum audio_iso_cadence_resync_reason {
	AUDIO_ISO_CADENCE_REASON_NONE = 0,
	AUDIO_ISO_CADENCE_REASON_ZERO_INTERVAL,
	AUDIO_ISO_CADENCE_REASON_ZERO_ADVANCE,
	AUDIO_ISO_CADENCE_REASON_DELIVERED_GT_EVENTS,
	AUDIO_ISO_CADENCE_REASON_DELTA_OFF_GRID,
	AUDIO_ISO_CADENCE_REASON_OVER_BOUND,
};

struct audio_iso_cadence_observation {
	uint32_t delta_us;
	uint32_t event_count;
	uint32_t delivered_positions;
	uint32_t error_us;
	uint32_t tolerance_us;
	enum audio_iso_cadence_resync_reason reason;
};
```

Extend existing update API with final optional pointer:

```c
enum audio_iso_cadence_result audio_iso_cadence_update(
	struct audio_iso_cadence *st, bool has_ts, uint32_t ts,
	uint32_t interval_us, uint32_t *omitted,
	struct audio_iso_cadence_observation *observation);
```

Initialize non-NULL observation to all zeros / `REASON_NONE` on every call.
Populate representable saturated `uint32_t` values for computed delta, event
count, delivered positions, error, and tolerance. Set exact reason only for
RESYNC. FIRST/NO_TS/CONTIG/GAP/WRAP keep `REASON_NONE`.

No new getter or logging dependency in pure module. Update every production
and test call site. Existing public outcome enum and test-matrix API outcome
ledger remain valid because function result set is unchanged.

### Warning evidence

In `src/audio_stream_session.c`, create one local observation for mono/Mode B
cadence update. On RESYNC, extend existing single warning to include:

- reason numeric enum;
- current timestamp;
- `delta_us`;
- interval;
- estimated event count;
- delivered positions;
- error;
- accepted tolerance;
- cumulative resync count.

Keep one warning per RESYNC. Keep cadence GAP at DEBUG only. Do not add INFO
traffic, new stats fields, shell output, or string allocation. Update comments.

## Tests

### Pure `iso_seq` suite

Update all cadence calls for optional observation and add/assert:

1. observation zeroed on NULL/FIRST/NO_TS/CONTIG/GAP/WRAP;
2. zero interval -> exact reason `ZERO_INTERVAL`;
3. duplicate -> `ZERO_ADVANCE` with delta/event/error/tolerance evidence;
4. delivered positions above events -> `DELIVERED_GT_EVENTS`;
5. off-grid delta -> `DELTA_OFF_GRID` with exact error and scaled tolerance;
6. over-bound gap -> `OVER_BOUND`;
7. 10 ms one-event scaled boundary: 32 us base + 10 us combined-SCA ceiling =
   42 us; +42 accepted, +43 RESYNC;
8. 10 ms nine-event boundary: 32 + 90 = 122 us; +122 accepted at exact
   concealment bound, +123 RESYNC;
9. 7.5 ms nine-event boundary: `ceil(67500*1000/1e6)=68`, tolerance 100 us;
   +100 accepted, +101 RESYNC;
10. quarter-interval cap for a long delivered/no-TS span and a tiny-interval
    edge, with no half-interval ambiguity or arithmetic underflow;
11. existing bound, wrap, missing-TS, counters, and sequence tests unchanged.

Use named calculation helpers in tests where useful; do not copy opaque magic
numbers without showing formula.

### Session suite

Update API call through production source. Existing session cadence tests must
remain green. Add or adapt one RESYNC test so fake log plumbing is not needed
but public output still proves current SDU decodes once and no PLC is
synthesized for off-grid RESYNC under new scaled boundary.

### Current docs

Update:

- `Kconfig` help to describe clock/span-scaled bounded cadence tolerance, not
  fixed 10 us;
- `docs/testing/behavior-contract.md` CODEC-015 exact tolerance and RESYNC
  evidence;
- `docs/testing/coverage-matrix.md` test descriptions/counts if counts change;
- `docs/development/firmware-release-fr4-cadence-hardware-handoff.md` only if
  its expected warning semantics need exact new diagnostic fields. Preserve
  zero-RESYNC acceptance criterion.

Do not rewrite retained `/tmp` evidence or claim hardware success.

## Scope

Expected files:

- `docs/development/firmware-release-fr4-cadence-tolerance-fix-handoff.md`
- `src/audio_iso_seq.h`
- `src/audio_iso_seq.c`
- `src/audio_stream_session.c`
- `tests/unit/iso_seq/src/test_iso_seq.c`
- `tests/unit/audio_stream_session/src/test_audio_stream_session.c`
- `Kconfig`
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- optionally current cadence hardware handoff wording only.

Do not touch central scripts/QoS, Mode A behavior, I2S, drift/ASRC, stats/shell
schema, test-matrix outcomes unless checker proves signature text is encoded,
coverage baseline, VERSION, workflow, release/tag state, AGENTS.md, historical
results, or hardware.

## Verification and commit

Run:

```bash
NIX_HARDENING_ENABLE="" west twister -T tests/unit/iso_seq \
  -p native_sim/native/64 --inline-logs
NIX_HARDENING_ENABLE="" west twister -T tests/unit/audio_stream_session \
  -p native_sim/native/64 --inline-logs
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
fw-build-5340
fw-build-54l15
```

Treat every new/actionable warning as failure. Inspect status/full diff/log.
Commit only complete green work:

```text
fix: scale ISO cadence timestamp tolerance
```

Do not amend. Require clean worktree, then run full canonical gate:

```bash
./scripts/test-all.sh
```

Expected total stays 65, coverage population 36, build contract 95/95, BSim
hashes unchanged. Do not push, merge, run hardware, flash, or touch remote
state.

Return files, exact formula/API, focused counts, builds/warnings, full gate,
coverage/contract/BSim, commit/status, deviations/blockers, and exact hardware
rerun entry point.
