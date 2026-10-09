# PB-035 XIAO source matrix: TX stack repair diagnostic

## Failure and diagnosis

The first three 120-second 10 ms mono, Mode A, and Mode B rows passed in
`/tmp/opencode/hil-runs/pb035-xiao-matrix-20260924-r1`. The later preserved
Mode B row failed start ACK; source UART reported `USAGE FAULT` and
`Stack overflow (context area not valid)`. Keep that matrix failure intact.

Private fault evidence at
`/tmp/opencode/pb035-source-fault-core-20260924-r1/attempt2/gdb.stdout`
shows `_kernel.current=bt_tx_processor_workq`, with PSP and PSPLIM both
`0x20013510`, the base of `bt_tx_processor_stack`. Its thread
`stack_info.size=904` is the aligned 900-byte configuration. NCS v3.3.0
`zephyr/subsys/bluetooth/host/Kconfig` defines the independent
`BT_TX_PROCESSOR_STACK_SIZE` for pending HCI command, ACL, and ISO sends;
`nrf/Kconfig.nrf` defaults it to 900 on nRF54L15. The existing 4096-byte
Bluetooth RX and system workqueue stacks do not size that TX thread.
Evidence supports a source TX stack exhaustion, not an SDC/radio defect.
The raw core is private and may contain bonds; do not publish it.

## Local change and verification

The XIAO standalone source board configuration now sets
`CONFIG_BT_TX_PROCESSOR_STACK_SIZE=2048`. TX priority, thread enablement,
stack guards, source schedule, transport limits, receiver, and SDK stay
unchanged. `nix develop -c fw-build-hil-source-54l15` completed with exit 0;
fresh log: `/tmp/opencode/pb035-source-txstack-build-20260924-r1.log`.
Resolved `build/hil-source-nrf54l15/zephyr/.config` has
`CONFIG_BT_TX_PROCESSOR_STACK_SIZE=2048` and the TX thread enabled. No
compiler, Kconfig-assignment, or other build diagnostics; only the documented
informational `__ASSERT()` CMake notice and the Nix dirty-tree message.
Built `build/hil-source-nrf54l15/zephyr/zephyr.hex` SHA-256:
`51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59`.
Built `build/hil-source-nrf54l15/zephyr/zephyr.elf` SHA-256:
`c43b9228a651d7b49e8805f315fbe1183cb9310a42e7ec2554c6a1ae52640e83`.

Previously tested source CPUAPP HEX SHA-256:
`bd0dbcad985bac0ecf180e0e21e28f8dcff87493ed768bd43e510ecf45689192`.
Receiver CPUAPP/FLPR hashes in `docs/development/pb-019-hci-resume-results.md`
are respectively
`eb3607ef67cbabf7e5fb471eb70500b1fa8cab5032e30bcb4eb4ec5c10923633`
and `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Do not infer a static probe-to-role mapping from this record.

## Pending physical acceptance

This build/configuration change alone is not acceptance. Smallest public-boundary
regression: rerun unchanged physical `rh3.preserved_mode_b_48_4_1` after a
prior fresh pair with the repaired source image, then the full two-pass,
20-row matrix. Parent owns physical reruns, live identity checks, and
 immutable run evidence. No physical verification of the repaired image yet.

## Source-only legacy retirement (pending matrix completion)

Removed nRF5340 source-only controller time implementation
`hil/source/app/src/hil_source_controller_time_nrf53_app.c`, DK board config,
and both CPUNET controller overlays. Source CMake now rejects non-nRF54L15
CPUAPP and compiles only `hil_source_controller_time_nrf54.c`; sysbuild
has no network-core child. Source entry point no longer sets the nRF5340
128 MHz divider. Generic source protocol, signal, state, output, BAP, and
their tests remain. Build aliases retain the nRF54L15 DK overlay and the
single-image `build/hil-source-nrf54l15` output. Source flash helper forces
CMSIS-DAP even if the host exports `OPENOCD_INTERFACE=jlink`.

Host fake-executable tests and static source config contracts are not physical
radio or runtime acceptance. This retirement did not rebuild, flash, or alter
the running matrix image files. After the current matrix completes, rebuild
the final source image and rerun the required physical matrix with fresh
identity checks and retained evidence before claiming acceptance.

## 2026-09-25 follow-up: fixed-image matrix passed

`/tmp/opencode/hil-runs/pb035-xiao-matrix-20260924-r2` passed 20/20 physical
children with no failures, cancellations or cleanup failures, including the
previously failing preserved Mode B row, 7.5 ms, reconnect, hang and stall
under unchanged limits. Source CPUAPP
`51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59`
and receiver CPUAPP
`9427913c9595f976cf1644d1ed857b6dc37ad0197fefa29dd4efa35d9e2427bd`
/ FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
match the later diagnostic final builds. Matrix imported earlier runner code
while subsequent defaults/schema/docs changed; do not claim final clean-commit
integration. Final runner smoke
`/tmp/opencode/hil-runs/pb035-final-runner-smoke-20260925-r1` passed with six
guarded identity checks, restored standalone source and receiver images; service
exit 0 and empty cgroup. Prior r1 failure stays intact. See
`docs/development/nrf54l15-migration-verification-results-20261001.md` for the recorded clean-gate boundary (the 2026-09-25 continuation snapshot this line cited was retired at Git rev `a94f010`).
