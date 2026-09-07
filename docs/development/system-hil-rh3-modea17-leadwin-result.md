# RH3 ModeA17/18 lead-window and air-side diagnostic results

Status: two completed one-run diagnostics that close the host-side
investigation with per-SDU ground truth. ModeA17
(`rh3-modeb-sdc-leadwin-20260908`) implemented the documented Nordic
lead-window pattern (per-readback offset resync, 3000 us lead-target
submission, genuinely-past-pin guard): the window discipline held
perfectly by its own instrumentation (`pin_adv=1`,
`pin_last - rb_last = 10000 us` = exactly one interval in every status
record) and delivery collapsed FURTHER (`rx_valid=3` of 12644,
`plc=38616`). ModeA18 (`rh3-modeb-sdc-airdiag-20260908`) added the
air-side ground truth through HCI LE_Read_ISO_TX_Sync
(`bt_bap_stream_get_tx_sync`): the controller reports the LAST AIRED
SDU as the FIRST event of the stream (`air_last` froze at the first
event's timestamp, `air_cnt=1`) while the assigned-schedule readback
advanced across all 12643 scheduled SDUs. The controller itself
confirms: after the first ISO event, NOTHING went on air, while every
pinned SDU was accepted, scheduled, and HCI-completed. Not acceptance;
the host-side design space is exhausted with direct evidence at every
step.

## The seven-run chain (ModeA12-18)

| Run | Host submission pattern | Pins on grid | Air (TX Sync) | rx_valid |
| --- | --- | --- | --- | ---: |
| ModeA12 | host-clock gate (biased offset), guard fired 2040x | drifted ahead | n/a | 12643 |
| ModeA13 | free-run, send-driven readback | yes | n/a | 167 |
| ModeA14 | free-run, completion-driven readback | yes (GAP=2 int) | n/a | 167 |
| ModeA15 | free-run (diagnostic) | yes (GAP=2 int) | n/a | 167 |
| ModeA16 | free-run, 4-event grid bound | yes (GAP<=2 int) | n/a | 168 |
| ModeA17 | 3 ms lead-window, per-readback resync | yes (GAP=1 int, pin_adv=1) | n/a | 3 |
| ModeA18 | same + air-side poll | yes (GAP=1 int, pin_adv=1) | **first event only, air_cnt=1** | 3 |

Established facts, all from runner-retained evidence:

1. The VS readback returns the controller's assigned (scheduled) event
   grid; 9999.05 us mean advance, no wrap (ModeA15).
2. Every pinned SDU was accepted (HCI-level completion for all 12644,
   sf=0 in every run).
3. The controller assigned every pinned SDU to the event we pinned
   (`pin_last - rb_last` = the expected 1-2 intervals in every
   configuration).
4. The controller AIRED only the first event under every pins-based
   submission pattern except ModeA12's degenerate host-clock pacing
   (which delivered 12643 with a 2040-times-advanced pin schedule).
5. The LE_Read_ISO_TX_Sync air-side truth (ModeA18): after the first
   event nothing transmitted, for the entire 2-minute stream, while
   scheduling continued.

The remaining mechanism lives inside the SoftDevice Controller's
central ISO TX pipeline for timestamp-provisioned SDUs on this
configuration (Zephyr host over IPC on nRF5340, SDC multirole
`19ffe5d4...` build, BAP 48_4_1 10 ms Mode B, bn=1/nse=3/FT=16, 2M,
RTN 5). No host-side chain within the documented SDC semantics
(timestamp mode, one pin per event, lead-windowed or free
submission) recovers it.

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
lead-window interpretation of the nRF Audio TX lead-time Kconfigs is
falsified: textbook-perfect submission discipline produced the worst
delivery of the chain.

## ModeA18 run details

Run ID `rh3-modeb-sdc-airdiag-20260908`, row `rh3.fresh_mode_b_48_4_1`,
evidence root `/tmp/opencode/hil-runs/rh3-modeb-sdc-airdiag-20260908/`.
Source CPUAPP `d5e986181f2c079e15753430effec3d7e73ce872d71df009ea5a34bb72354733`
(air-poll build; same behavior as ModeA17 plus the diagnostic), CPUNET
unchanged, receiver CPUAPP `58301eee...`, FLPR `45ab8d15...`. Terminal
pass with sub=12644; receiver `rx_valid=3`, `plc=38616`,
`rx_lost=19308`. Status fields: active snapshot
`air_last=19658572, air_cnt=1`; final record identical
(`air_last` frozen at the first event, `air_cnt=1`): the controller
aired exactly one SDU. Native Twister 70/70 x3, byte-identical double
builds, host regression 257 passed, only the documented notices.

## Stop point and escalation

The user-directed escalation path (research, then UART-level
observability, then J-Link, then single-variable isolation) has been
followed to the boundary of what the host can observe:

1. Nordic documentation and DevZone knowledge: the three provisioning
   modes, the lead-time bounds, DRGN-21293/21605 semantics - all
   incorporated and tested.
2. UART-level: the HIL1 status record now carries assigned-schedule
   AND air-side per-stream evidence; the source and receiver consoles
   are fully captured every run.
3. The next observability level is the HCI wire itself
   (`CONFIG_BT_DEBUG_MONITOR_UART`/btmon over a second UART, or
   J-Link RTT/monitor on the app core), and/or the DevZone question
   with this complete seven-run chain.

The recommended next action: post the chain to DevZone (the question
now has a crisp, controller-confirmed statement: "SDC central on
nRF5340 hci_ipc accepts and schedules timestamp-pinned ISO SDUs for
the whole stream but airs only the first event, per its own
LE_Read_ISO_TX_Sync"), while preparing the btmon capture as the
parallel confirmation. Everything stays uncommitted pending the user's
decision. Preserve both evidence roots; do not rerun these IDs.