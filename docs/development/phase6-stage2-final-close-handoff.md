# Phase 6 Stage 2 — final close handoff

## Decisions already resolved

1. Restore `CONFIG_BT_SMP_SC_PAIR_ONLY=y` by removing nRF54 board override.
   NCS v3.3.0 `zephyr/subsys/bluetooth/host/Kconfig` defines this as “disable
   legacy pairing”; it still permits unauthenticated LE Secure Connections
   Just Works with NoInputNoOutput. Wire reason `0x0c` is Numeric Comparison
   Failed, not SC unsupported. Keep `CONFIG_BT_SMP_ENFORCE_MITM=n` in
   `prj.conf`.
2. Use compiled-public dongle `C0:AA:BB:CC:DD:EE`. Apply adapter NINO after
   every power cycle while powered off: `btmgmt io-cap 3`, `btmgmt bondable on`,
   `btmgmt sc on`, then power on. Register BlueZ NoInputNoOutput default agent.
   Clear both bond stores before test.
3. Brief-stall gate uses FLPR consumer stall for 250 ms, not persistent stall:
   `stall_flpr 1`, confirm ACK, sleep 250 ms, `stall_flpr 0`, confirm ACK.
   Expected recovery: at most a few escalating attempts, then ACTIVE;
   100 consecutive successes clear probation. No exhaustion/FALLBACK.

## Mechanical changes

- Delete lines/comment assigning `CONFIG_BT_SMP_SC_PAIR_ONLY=n` from
  `boards/nrf54l15dk_nrf54l15_cpuapp.conf`.
- Update Stage 2 result/security documentation. Remove claims that SW Split LL
  cannot reliably support SC.
- Fix `scripts/bap_central.py` Agent1 signatures to BlueZ 5.86 API:
  `DisplayPinCode(os)` and `DisplayPasskey(ouq)`. Existing definitions are
  swapped/wrong.
- Set `Adapter1.Pairable=true` and verify property before ACL/pairing.
- Replace synchronous `Device.Pair()` in `--peer-addr` path with async D-Bus
  call using `reply_handler`/`error_handler`; iterate GLib context until done so
  Agent1 `RequestAuthorization` can dispatch. Blocking Pair on same process
  starves agent dispatch and causes kernel `User Confirmation Negative Reply`.
- Add peer address type argument to raw helper. Own address remains public;
  receiver peer type is random. Do not conflate own and peer address types.
- For normal discovery path, remove raw-HCI pre-connect. With compiled public
  dongle address, scanning works. Invoke asynchronous `Device.Pair()` while
  Device1 is disconnected; BlueZ must issue MGMT Pair Device before LE
  Connection Complete, establishing `device->bonding` before SMP. BlueZ then
  auto-accepts Just Works `confirm_hint=1` without an Agent1 callback. Only use
  raw ACL path for explicit `--peer-addr` fallback.
- Build both targets and inspect nRF54 resolved config:
  `CONFIG_BT_SMP_SC_PAIR_ONLY=y`, `CONFIG_BT_SMP_ENFORCE_MITM` unset/n.

## Hardware procedure

1. Build/flash current nRF54 CPUAPP+FLPR and current compiled-address dongle.
2. Capture receiver serial before reset. Start btmon text capture.
3. Clear receiver bonds using `bt unpair`; clear BlueZ device using
   `bluetoothctl remove <actual receiver address>`; power-cycle adapter and set
   IO capability/SC.
4. Run Mode A for at least 90 s using normal scan path first. Verify btmon shows
   MGMT Pair Device before LE Connection Complete. If scanning is
   externally unavailable, use `--peer-addr` with public own-address helper.
5. After offload ACTIVE and >=100 successes, send stall-on, verify shell ACK,
   wait 250 ms, send stall-off, verify shell ACK.
6. Continue >=30 s after clear. Query `flpr offload`, ring status, FLPR status,
   audio status/perf before disconnect.

## Acceptance

- SMP trace contains SC and Just Works; no legacy pairing. Expected Linux mgmt
  confirmation has `confirm_hint=1` and positive User Confirmation Reply. A
  changed nonzero g2 value between attempts is normal and is not ECDH-failure
  evidence.
- Central remains 100 fps; no I2S/decode/push/ASRC faults.
- Timeout/full fallback occurs during stall without audio drop.
- Recovery attempts bounded; state returns ACTIVE after clear.
- `probation_cleared` increments after 100 successes; no max exhaustion.
- Success count resumes after recovery; fault/fallback evidence preserved.
- CPUAPP RAM and FLPR RAM reported from correct images.
- Tests/builds pass; commit changes/results, no push/amend/mass erase.
