# nRF54L15 receiver TX-notify workqueue repair, 2026-09-28

Status: receiver-only configuration repair on the primary uncommitted tree.
Diagnostic one-row evidence supports this choice; clean software gates, the
full physical matrix, and new exact-artifact qualification remain pending.
This document does not accept a release or reclassify arbitrary warnings.
Related product work: PB-035 (source matrix), PB-036 (exact source artifact), PB-038 (remaining qualification).

## Baseline failure and no-reset postmortem

The clean `25a5cbf0e2b2decc4955dd58ce2972e10a26cf35` local artifact
matrix passed six rows, then stopped at pass 1 row 7,
`rh3.fresh_mode_b_48_3_1` (Mode B, 7.5 ms); thirteen rows were skipped.
Result: `/tmp/opencode/hil-runs/nrf54-25a5-artifact-matrix-20260928-r1/`;
first failed child is identified in
`/tmp/opencode/nrf54-25a5-artifact-matrix-20260928-r1/phase2-review.json`.
Receiver console shows I2S DMA started at `00:56:08.165,880` and
`Next buffers not supplied on time` at `00:56:08.435,504`, just 0.269624 s
later. DMA restarted, but after stream disable at `00:58:14.815` the
receiver reported another I2S error, then a 10-second HCI `0x206f`
remove-ISO-data-path timeout and an assert in `hci_core.c:482` on
`sysworkq`. It halted without a receiver stream summary; the row failed at
session end. Source records contain `scored_complete` for 16000 SDUs and
later `terminal fail` during teardown, not a successful row. The first I2S
error preceded the source's eventual unsigned 32-bit timestamp wrap and
cannot be assigned to that late wrap. Full byte-indexed raw console
chronology: `/tmp/opencode/nrf54-25a5-row7-analysis-r1/CHRONOLOGY.md`.

Before any reset or replay, the live receiver still sat in
`arch_system_halt` with `sysworkq` current. No-reset CPUAPP core capture:
`/tmp/opencode/nrf54-25a5-row7-receiver-core-r2/` (private 262144-byte RAM
SHA-256 `6af60e354d330e0e22eeaeee5b5deceea156d008528cbd643f5e1893bc8b10e4`).
At that snapshot `rx_hci_msg.type=SDC_HCI_MSG_TYPE_ISO`; the three-buffer
ISO RX pool had an empty free queue. `iso_conns[0].tx_complete_work` was
queued to `sysworkq` (flags 4), `bt_dev.sent_cmd` was non-null and its
command semaphore count was zero. Saved BT RX workqueue stack words include
`k_work_flush` and `bt_conn_tx_notify`; `sysworkq` saved words include
`bt_hci_cmd_send_sync`, `bt_iso_remove_data_path`, and
`bt_bap_remove_iso_data_path`. These are saved words, not a complete
reconstructed execution trace. Receiver snapshot counters were RX valid
4078, lost 12792, PLC frames 25584. No causal claim about RF delivery is
made from them.

Installed NCS v3.4.1 source grounds the dependency hypothesis:
`zephyr/subsys/bluetooth/host/conn.c:282-355` routes connection TX notify
work to `k_sys_work_q` when `CONFIG_BT_CONN_TX_NOTIFY_WQ=n`, and
`bt_conn_tx_notify(..., true)` submits then flushes that work. In
`zephyr/subsys/bluetooth/host/hci_core.c:415-484`, synchronous HCI send
waits for its command response with a bounded semaphore. Nordic
`nrf/subsys/bluetooth/controller/hci_driver.c:690-709` retains an ISO RX
message when host processing returns `-ENOBUFS`. A wait cycle among these
parts is consistent with the snapshot; this evidence does not establish
every historical loss cause. Historical nRF5340 H40 with a private TX
workqueue had poor delivery under an older source clock fixture, so it is
not current nRF54L15 acceptance or a reason to transplant old trace config.

## Config-only isolation and frozen-row replay

Clean `25a5cbf` clone build with external config SHA-256
`559a1ba55d955e7170844e8a662d16f30d7d2410e820c7fd89ebd48451e9c62c`
selected only separate connection TX-notify queue, 1536-byte stack, and
priority 8. Build/config evidence:
`/tmp/opencode/nrf54-txnotify-wq-diagnostic-20260928-r1/review.json`.
Its receiver HEX SHA-256 was
`4a61b4531bc211884a5f211502236961ad5a660112f22205f8b796996c608725`;
FLPR HEX remained
`c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`,
source HEX remained
`805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c`.
No ISO RX count, audio pipeline, source clock, QoS, or tracing change.
Linker free RAM decreased from 3436 to 1612 bytes; no other stack was
shrunk. The SDK marks this workqueue experimental, and the diagnostic
build retained the exact Kconfig message
`warning: Experimental symbol BT_CONN_TX_NOTIFY_WQ is enabled.`. This
visible, reviewed target-specific condition is **not** a zero-warning claim
or a blanket waiver. Compiler warnings, assigned-value warnings, runtime
warnings, and unrelated CMake warnings remain failures.

One replay of the **same** frozen `rh3.fresh_mode_b_48_3_1` row with these
local-build images passed, without a reduced limit or artifact substitution
claim. Immutable result:
`/tmp/opencode/hil-runs/nrf54-txnotify-wq-modeb7p5-20260928-r1/result.json`;
post-run evidence:
`/tmp/opencode/nrf54-txnotify-wq-modeb7p5-analysis-r1/README.md`.
Receiver slot 0: valid 16860, lost 14, PLC 28, no decode error,
I2S underrun, stream reset, or runtime warning. Source: scored 16000,
submitted 16859, callbacks 16859, send failures 0, skips 0, under-lead
0, lead 2835..2908 us. Disable, release, and disconnect completed; source
returned idle. Post-teardown `audio status` is reset and cannot substitute
for the scored receive summary. A **single** post-row read-only shell
capture shows the new `BT CONN TX WQ` used 300/1536 bytes (19%);
the unchanged `bt_tx_processor` used 756/904 (83%). This is a margin
observation, not evidence that its stack caused the prior failure.

The primary repair pins the receiver-only separate workqueue and keeps
`CONFIG_WARN_EXPERIMENTAL=y` so the diagnostic condition stays visible.
Resolved Kconfig/build-contract checks cover this exact configuration, not
the physical receive/teardown outcome. The previously failed matrix
remains immutable and failed; one positive row does not close it.
Primary-tree pristine `fw-build-54l15` passed after this config edit;
full raw build log and verification record are
`/tmp/opencode/nrf54-txnotify-primary-20260928-r1/build.log` and
`verification.json`. The primary receiver HEX matches the diagnostic HEX
above, while the primary ELF hash is
`efd5337aadbcd16bf21fa867974e616fe7332f0882a7451227327084e1aa5c77`
(not the clone-path ELF hash). Primary FLPR HEX is byte-identical to the
baseline; its ELF hash differs across checkout paths. The primary resolved
config retains ISO RX buffers 3, offload enabled, stack 1536, priority 8,
and `WARN_EXPERIMENTAL=y`; no HIL traces are on. Primary linker free RAM is
1612 bytes. The complete build log has only the exact experimental-symbol
warning and the source-checked Zephyr `__ASSERT()` configuration notice;
no compiler, assignment or unrelated CMake warnings. The build-contract
checker passed 73/73 assertions. The focused host test suite passed 55
tests, including public-checker failures for missing/disabled queue,
wrong stack/priority and missing/disabled experimental-warning visibility.
These software checks do not substitute for a physical matrix replay.
Full clean-matrix and remaining repository gates have not run for this repair.
The original `25a5cbf` receiver and source ZIPs retain their archived
hashes and must not be overwritten. Any later exact-artifact qualification
requires **new** images and a new artifact set.
