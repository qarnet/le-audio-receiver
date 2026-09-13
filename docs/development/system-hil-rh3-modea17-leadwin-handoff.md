# RH3 ModeA17 handoff: documented lead-window submission (timer-paced)

> [!WARNING]
> Historical, completed diagnostic plan. Do not execute it or reuse its run ID.
> Its controller-defect interpretation is superseded by the passing mirrored
> controller-clock and 128 MHz source result in
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status: completed one-run historical diagnostic, grounded in Nordic
documentation research performed at the user's direction after the
ModeA16 stop point.

## Research findings (DevZone / Nordic docs, 2026-09-07)

1. **The SDC timestamp-mode submission window is bounded on BOTH
   sides.** The nRF Audio production application documents TX lead-time
   limits (application-specific Kconfig options, nRF Audio docs):
   - `CONFIG_NRF_AUDIO_TX_LEAD_TIME_MIN_US`: "Min allowed lead time to
     send data to controller before it sends the next SDU on air...
     Setting this too low may lead to flushed data. Must be higher than
     HCI_ISO_TX_SDU_ARRIVAL_MARGIN_US plus a margin depending on SoC."
   - `CONFIG_NRF_AUDIO_TX_LEAD_TIME_BASE_US`: "NOTE: The actual value
     is the one above + an SDU interval. Max allowed lead time to send
     data to the controller before it sends the next SDU on air. This
     means data is sent too early/fast, and will flush one SDU."
   A submission too far ahead of its pinned timestamp is flushed, exactly
   like a late one. At the time, this was used to classify ModeA13-16 as
   submitting too far ahead. Their HCI completions and receiver counters did
   not directly prove the corresponding controller flushes. ModeA12's host
   gate submitted roughly one interval ahead and the receiver recorded
   12643/12644 valid SDUs.
2. **DRGN-21293**: the LE Read ISO TX Sync (and the VS variant) returns
   "the SDU synchronization reference of the SDU previously scheduled for
   transmission" - the last assigned event, not the last aired one
   (consistent with the ModeA15 raw-field evidence).
3. **DRGN-21605**: the readback value may be off by 40 us; irrelevant at
   the 10 ms interval scale.
4. **The Nordic-proven submission pattern** (nrf iso_time_sync sample,
   central role, nRF53): first SDU plain; after each iso_sent completion
   read the assigned timestamp; schedule the next submission with a
   delayable work timer at `assigned + interval - margin - 1000 us (IPC
   allowance on nRF53)` - i.e. submit each SDU a fixed ~2-3 ms before
   its pinned event. Nordic's own audio application additionally
   monitors the actual lead time against MIN/TGT/BASE bounds
   (OCT-3754: submitting AT the target limit already causes warnings and
   possible audible artefacts).

## Historical implementation

`hil/source/app/src/hil_source_app.c`, ModeA17:

- Keeps the completion-driven readback chain and the pinned sends
  (`tx_ts_next`, monotonic resync, bookkeeping-before-send with
  rollback) from ModeA14/16.
- Restores a host-side submission gate, now with the two fixes the
  evidence demands:
  1. **Per-readback offset resync**: on EVERY successful readback the
     host-vs-controller offset updates (`offset = host_us - assigned`),
     not once per segment (ModeA12's drift flaw). The gate therefore
     cannot oscillate from a stale offset.
  2. **Lead-window targeting**: a stream's next send is allowed when
     the pinned event is within `HIL_SOURCE_TX_TS_LEAD_TARGET_US`
     (3000 us: the 1000 us controller margin + the 1000 us nRF53 IPC
     allowance the iso_time_sync sample uses, + 1000 us scheduler
     slack) of host-now, converted through the live offset. Waiting
     uses the existing wake path (`hil_app_tx_wait_until`, µs->ms
     conversion); completions, stop, and errors wake it.
- Stale-pin guard (bounded): only a GENUINELY PAST pin (`pin_host <=
  now`) advances by whole intervals to the next future event; the
  skipped events count per stream as the `"pin_adv"` status field
  (re-instated; the parser tolerance is proven). A pin inside the
  margin window is still sent - the controller decides, and the
  per-readback offset resync keeps this rare. A healthy run must keep
  `pin_adv` at zero or single digits.
- The ModeA15 diagnostic fields (`rb_first/min/max/last/cnt`,
  `pin_last`) stay; every run now self-reports whether the window
  discipline held (`pin_last - rb_last` <= 1 interval, `pin_adv` ~ 0).

Constants (`hil_source_app.h`): `HIL_SOURCE_TX_TS_LEAD_TARGET_US 3000`
(submission lead), `HIL_SOURCE_TX_TS_MIN_AHEAD_US 2000` (documented
minimum future distance, kept as reference documentation for the
window semantics).

## Historical software verification

Native source-app Twister: full suite must pass (the gate paces sends
~7 ms apart again, so lifecycle tests take seconds each; the
`testcase.yaml` 300 s timeout already accommodates this). Two pristine
source builds byte-identical; resolved config
`CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`; only the documented
notices. Host runner regression green.

## Historical fixed identity

```text
run ID: rh3-modeb-sdc-leadwin-20260908 (validate unused before invoking)
row:    rh3.fresh_mode_b_48_4_1
```

Receiver: normal current-HEAD build. Source: current tree (SDC rework +
timestamp-mode line + this gate), no fragment.

## Historical prediction

If the documented lead window is the true mechanism, submissions at
~3 ms before each pin sit inside the acceptance window: empty events
collapse to startup transients (`rx_lost` single digits), `rx_valid`
~12644, `plc` near the healthy mono baseline (12), streaming window
~126.4 s, and the status fields prove the discipline (`pin_adv` 0-2,
`pin_last - rb_last` <= 1 interval in every record).

## Historical classification arms

- PASS: fixture validated; commit everything (SDC rework + timestamp
  mode + lead-window gate + tests + docs) as:

    fix(hil): switch fixture to SDC timestamp-mode ISO provisioning

  Report readiness for the full RH3 matrix (`rh3-matrix-<date>-2`,
  `run-rh3-matrix`, 14 child runs, outer timeout 10800000 ms).

- FAIL with `pin_adv` large (window discipline broke): the gate logic is
  wrong on hardware; record, one bounded fix-validation of the gate
  constants with the raw fields as evidence.

- FAIL with discipline held (`pin_adv` ~ 0, `pin_last - rb_last` <= 1)
  and delivery still collapsed: the lead-window interpretation is
  falsified on this configuration; STOP for the next escalation level
  per the user's directive (HCI monitor / btmon capture of the
  host-controller ISO traffic, then J-Link-based observability, then
  controller-config single-variable runs).

## Result documentation

`docs/development/system-hil-rh3-modea17-leadwin-result.md`:
prediction vs outcome, every status record's raw fields, per-slot
summary, limits verdict, FLPR active, QoS, ISO tail, integrity, raw
identity, restoration, stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single run. Status
0/1/130 immutable. Preserve all prior evidence roots.
