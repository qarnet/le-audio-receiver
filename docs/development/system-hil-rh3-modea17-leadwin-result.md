# RH3 ModeA17/18 lead-window and TX-sync diagnostic results

> [!WARNING]
> Historical diagnostic record. Its SDC-defect and host-exhaustion conclusion
> is superseded by the passing controller-clock source fix-validation in
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).
> Preserve the run facts and evidence roots, but do not use this document to
> support a DevZone defect report.

Status: two completed one-run diagnostics of the former source scheduler.
ModeA17 (`rh3-modeb-sdc-leadwin-20260908`) implemented
the documented Nordic lead-window pattern (per-readback offset resync,
3000 us lead-target submission, genuinely-past-pin guard): the window
discipline held perfectly by its own instrumentation (`pin_adv=1`,
`pin_last - rb_last = 10000 us` = exactly one interval in every status
record) and delivery collapsed FURTHER (`rx_valid=3` of 12644,
`plc=38616`). ModeA18 (`rh3-modeb-sdc-airdiag-20260908`) added an
HCI LE_Read_ISO_TX_Sync poll to the status record.

MEASUREMENT SEMANTICS CORRECTION (2026-09-08, validation research):
per DRGN-21293 the LE Read ISO TX Sync `TX_Time_Stamp` is "the SDU
synchronization reference of the SDU previously SCHEDULED for
transmission" - schedule semantics, not proof of airing. The `air_*`
fields therefore record one successful schedule-reference poll, NOT an
on-air count. This result doc's earlier wording overstated them. The
receiver counters remain the delivery evidence for these two historical
runs: `rx_valid=3` of 12644 submitted with `rx_lost=19308` and
controller-side `rx_unreceived` near-total, with `crc_error=0`. Those
counters prove that these source builds delivered almost nothing; they
do not prove an SDC defect. The current controller-clock source passes
the same Mode B shape and Mode A, so the old host-exhaustion conclusion
is withdrawn.

## The seven-run chain (ModeA12-18)

| Run | Host submission pattern | Pins on grid | TX Sync poll | rx_valid |
| --- | --- | --- | --- | ---: |
| ModeA12 | host-clock gate (biased offset), guard fired 2040x | drifted ahead | n/a | 12643 |
| ModeA13 | free-run, send-driven readback | yes | n/a | 167 |
| ModeA14 | free-run, completion-driven readback | yes (GAP=2 int) | n/a | 167 |
| ModeA15 | free-run (diagnostic) | yes (GAP=2 int) | n/a | 167 |
| ModeA16 | free-run, 4-event grid bound | yes (GAP<=2 int) | n/a | 168 |
| ModeA17 | 3 ms lead-window, per-readback resync | yes (GAP=1 int, pin_adv=1) | n/a | 3 |
| ModeA18 | same + TX_Sync poll | yes (GAP=1 int, pin_adv=1) | schedule ref frozen at first event (schedule semantics per DRGN-21293) | 3 |

Established facts, all from runner-retained evidence:

1. The VS readback returns the controller's assigned (scheduled) event
   grid; 9999.05 us mean advance, no wrap (ModeA15).
2. Every pinned SDU was accepted (HCI-level completion for all 12644,
   sf=0 in every run).
3. The controller assigned every pinned SDU to the event we pinned
   (`pin_last - rb_last` = the expected 1-2 intervals in every
   configuration).
4. The receiver received only the first ~1.7 s of audio under every
   pins-based submission pattern except ModeA12's degenerate
   host-clock pacing (which delivered 12643 with a
   2040-times-advanced pin schedule); the receiver's ISO counters
   (`rx_valid`, `rx_lost`, `rx_unreceived`, zero CRC errors) are the
   delivery evidence for those runs.
5. The ModeA18 LE Read ISO TX Sync fields contain one successful poll
   whose returned schedule reference remained at the first event. One
   poll is not an SDU count and does not discriminate controller airing.

The former conclusion that the remaining mechanism had to live inside
the SoftDevice Controller was not supported. Later work replaced the
source scheduler with a mirrored controller-clock gate, encoded before
that gate, and raised CPUAPP to 128 MHz. That source passes the same
BAP 48_4_1 Mode B shape and a two-CIS Mode A row through the same SDC
central. See the superseding result linked above.

## ModeA17 run details

Run ID `rh3-modeb-sdc-leadwin-20260908`, row `rh3.fresh_mode_b_48_4_1`,
evidence root `/tmp/opencode/hil-runs/rh3-modeb-sdc-leadwin-20260908/`.
Source CPUAPP `3faeee5e17735e299be19e3adb7ac2765f6174d5b9613518cde87522a308d85f`
(lead-window gate build), CPUNET unchanged `19ffe5d4...`, receiver
CPUAPP `58301eee9fc6ead8fd281e64b633dd7a6823a0c81ebc5b17539e0f93dd95d588`
(normal current-HEAD), FLPR `45ab8d15...`. Terminal pass with
sub=12644; receiver `rx_valid=3`, `plc=38616`, `rx_lost=19301`,
`plc = 2 x rx_lost` preserved. Status fields at the final record:
`rb_cnt=12643`, `pin_last - rb_last = 10000`, `pin_adv=1`. The
lead-window implementation held its own measured window but produced
the worst delivery of this historical scheduler chain. Later passing
evidence shows this did not falsify the documented SDC model.

## ModeA18 run details

Run ID `rh3-modeb-sdc-airdiag-20260908`, row `rh3.fresh_mode_b_48_4_1`,
evidence root `/tmp/opencode/hil-runs/rh3-modeb-sdc-airdiag-20260908/`.
Source CPUAPP `d5e986181f2c079e15753430effec3d7e73ce872d71df009ea5a34bb72354733`
(air-poll build; same behavior as ModeA17 plus the diagnostic), CPUNET
unchanged, receiver CPUAPP `58301eee...`, FLPR `45ab8d15...`. Terminal
pass with sub=12644; receiver `rx_valid=3`, `plc=38616`, and
`rx_lost=19308`. Active snapshot and final record both show one
successful TX Sync poll returning the first event's schedule reference
while `rb_cnt` advanced across the whole stream. That poll does not
count aired SDUs and does not corroborate the delivery count. Native
Twister 70/70 x3, byte-identical double builds, host regression 257
passed, only the documented notices.

## Superseding stop point

Do not post the former DevZone defect draft. The current source fixture
passes direct Mode B and Mode A rows with zero skipped controller events,
complete source counters, receiver delivery inside the frozen limits,
and clean teardown. HCI LE Read ISO TX Sync remains schedule telemetry,
not air-side ground truth.

Preserve both ModeA17/18 evidence roots and do not rerun those IDs. They
remain valid records of the old scheduler's behavior. The next acceptance
step is the fixed 14-child `run-rh3-matrix`, not further SDC-defect escalation.
