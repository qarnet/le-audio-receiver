# Phase 6 Stage 2 — final close handoff

## Decisions already resolved

1. Restore `CONFIG_BT_SMP_SC_PAIR_ONLY=y` by removing nRF54 board override.
   NCS v3.3.0 `zephyr/subsys/bluetooth/host/Kconfig` defines this as “disable
   legacy pairing”; it still permits unauthenticated LE Secure Connections
   Just Works with NoInputNoOutput. Wire reason `0x0c` is Numeric Comparison
   Failed, not SC unsupported. Keep `CONFIG_BT_SMP_ENFORCE_MITM=n` in
   `prj.conf`.
2. Use compiled-public dongle `C0:AA:BB:CC:DD:EE`. Apply adapter NINO after
   every power cycle: `btmgmt io-cap 3`, `btmgmt sc on`. Register BlueZ
   NoInputNoOutput default agent. Clear both bond stores before test.
3. Brief-stall gate uses FLPR consumer stall for 250 ms, not persistent stall:
   `stall_flpr 1`, confirm ACK, sleep 250 ms, `stall_flpr 0`, confirm ACK.
   Expected recovery: at most a few escalating attempts, then ACTIVE;
   100 consecutive successes clear probation. No exhaustion/FALLBACK.

## Mechanical changes

- Delete lines/comment assigning `CONFIG_BT_SMP_SC_PAIR_ONLY=n` from
  `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.
- Update Stage 2 result/security documentation. Remove claims that SW Split LL
  cannot reliably support SC.
- Build both targets and inspect nRF54 resolved config:
  `CONFIG_BT_SMP_SC_PAIR_ONLY=y`, `CONFIG_BT_SMP_ENFORCE_MITM` unset/n.

## Hardware procedure

1. Build/flash current nRF54 CPUAPP+FLPR and current compiled-address dongle.
2. Capture receiver serial before reset. Start btmon text capture.
3. Clear receiver bonds using `bt unpair`; clear BlueZ device using
   `bluetoothctl remove <actual receiver address>`; power-cycle adapter and set
   IO capability/SC.
4. Run Mode A for at least 90 s using normal scan path first. If scanning is
   externally unavailable, use `--peer-addr` with public own-address helper.
5. After offload ACTIVE and >=100 successes, send stall-on, verify shell ACK,
   wait 250 ms, send stall-off, verify shell ACK.
6. Continue >=30 s after clear. Query `flpr offload`, ring status, FLPR status,
   audio status/perf before disconnect.

## Acceptance

- SMP trace contains SC and Just Works; no legacy pairing.
- Central remains 100 fps; no I2S/decode/push/ASRC faults.
- Timeout/full fallback occurs during stall without audio drop.
- Recovery attempts bounded; state returns ACTIVE after clear.
- `probation_cleared` increments after 100 successes; no max exhaustion.
- Success count resumes after recovery; fault/fallback evidence preserved.
- CPUAPP RAM and FLPR RAM reported from correct images.
- Tests/builds pass; commit changes/results, no push/amend/mass erase.
