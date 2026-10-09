# RH3-40 TX-notify workqueue result

## Status and scope

One diagnostic H40 physical execution completed and passed. This is one bounded
RH3 diagnostic result, not RH3 acceptance and not a production change. The
passing runner row does not establish audio health, a root cause, production
safety, or production adoption.

## Immutable evidence

The immutable H40 evidence root is:

```text
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq/
```

The external JUnit file is:

```text
/tmp/opencode/hil-runs/rh3-20260826-40-sdc-iso-tx-notify-wq.junit.xml
```

`environment.json` records runner command status `0`. `result.json` records
`outcome=passed`, `first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`. The external JUnit contains one test with zero failures,
skipped tests, or errors. Read-only `SHA256SUMS` verification passed `27/27`
retained evidence files.

Relevant immutable records are `result.json`, `environment.json`, `images.json`,
`sdc-hci-remove-iso-path-trace.json`, `receiver-post-stop-status.txt`, and
`SHA256SUMS` within the evidence root. Raw console logs are not copied into
this repository.

## Exact image and configuration identities

H40 used only the diagnostic trace fragment
`tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf`. Its private
TX-notify workqueue settings were:

```text
CONFIG_BT_CONN_TX_NOTIFY_WQ=y
CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE=1536
CONFIG_WARN_EXPERIMENTAL=n
```

These settings were trace-only and are not a production configuration. The H40
CPUAPP RAM margin was 104 bytes.

Exact image SHA-256 values were:

- Receiver CPUAPP: `c487f021a0ff29076e0a1b312a6826b4f2007623098f22c34fa19358f1bedb52`
- Receiver FLPR: `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
- Source app: `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
- Source CPUNET: `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

## Observed execution

The executed row was `rh3.fresh_mode_b_48_3_1`, a fresh Mode B `48_3_1`
execution with one stream. The source final status was a pass with these exact
counters:

```text
verdict=pass sub=16859 sc=16000 sf=0 cb=16859 out=0
```

The active source lifecycle reached `state=streaming` with the source active
and connected. Its active stream snapshot was `seq=20 sub=20 sc=0 sf=0 cb=20
out=0`; the active FLPR offload snapshot retained `submit=0 success=0`.
The source then stopped through teardown and reached the passing terminal
status.

The receiver summary retained these high-loss values:

```text
rx_valid=23 rx_lost=19463 decoded=38972 plc=38926
```

It also retained `rx_no_ts=12`, `rx_error=0`, `decode_err=0`,
`i2s_underrun=0`, `stream_reset=0`, and `empty_sdu=0`. The ISO link-quality
tail reported `rx_unreceived=18884` and `crc_error=1`. These loss values are
recorded plainly. No cause is inferred from them.

Post-stop diagnostic snapshots had no listed audio, I2S, FLPR, or handshake faults:

- Audio counters were decoded `0`, PLC `0`, decode errors `0`, I2S underruns
  `0`, stream resets `0`, empty SDUs `0`, and push failures `0`.
- The FLPR state was `STOPPED`, with `submit=0 success=0 fallback=0 busy=0`
  and zero recorded faults, recovery attempts, or probation counters.
- FLPR handshake was Ready, ACKed, and Healthy, with zero length, version,
  unknown, send, RX-lost, duplicate, out-of-order, and missed errors.

The `48_3_1` 7.5 ms row has the 360-frame input shape. Existing nRF54L15
behavior therefore uses CPUAPP ASRC fallback, so zero FLPR submits are expected
here and are not an H40 offload fault.

The trace retained one zero-outstanding ISO RX lifetime `disable` snapshot:
`capacity=3`, `outstanding=0`, `high_water=3`, `allocations=19486`,
`final_unrefs=19486`, `callbacks_active=0`, and `callbacks_total=19486`. It
retained successful command and SDC completion records, and no `unavailable`
snapshot. Trace `parser_errors=[]` and `validation_errors=[]`.

## Bounded H39 comparison

H39 observed an `unavailable` snapshot with `outstanding=3`,
`tx_notify_flush_semaphore_pend_thread_marked_pending=1`, and
`tx_notify_flush_semaphore_give_entered=0`. H40 instead completed the row with
no `unavailable` snapshot and retained a zero-outstanding `disable` snapshot.

This is an observation-only comparison. It does not prove workqueue ownership,
cause, a general repair, workload safety, or production safety.

## Restoration and stop point

Local normal nRF54L15 build output was restored after H40. The restored normal
CPUAPP hash was
`e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`; FLPR
and source output retained the H40 values listed above. The local restoration
build did not flash either board, so it does not prove that hardware itself was
reflashed to the normal image.

H40 must not be retried. Any later hardware or production phase requires a new
reviewed plan.

## Historical RH3-40 planning observations

Observations recorded by the RH3-40 planning handoffs that predate this result
and are not repeated in the sections above (provenance: Git rev `a94f010`, for
example `git show a94f010:docs/development/system-hil-rh3-40-tx-notify-workqueue-handoff.md`).
All are NCS v3.3.0-historical
diagnostic facts, not current v3.4.1 production behavior, acceptance, or
approval to replay any configuration.

### Source-derived circular-wait hypothesis

The RH3-40 planning handoff derived this bounded circular-wait hypothesis from
installed NCS v3.3.0 source, including a distinction later evidence preserved:

```text
sysworkq state-transition work
  -> waits for 0x206f Command Complete
  -> Command Complete waits behind retained incoming ISO HCI message
  -> retained ISO message waits for an ISO RX buffer
  -> BT RX WQ bt_conn_recv() waits for tx_complete_work on sysworkq
```

Its grounding points: `zephyr/subsys/bluetooth/audio/ascs.c:513-605` runs ASE
state transitions from `state_transition_work_handler()` and
`ascs_ep_set_state()` schedules that work (ascs.c:702-710);
`zephyr/kernel/work.c:1132-1141` implements `k_work_schedule()` on
`&k_sys_work_q`; on streaming exit `ascs.c:408-428` calls
`bt_bap_remove_iso_data_path()`, `bap_iso.c:215-235` calls
`bt_iso_remove_data_path()`, and `iso.c:346-379` sends synchronous HCI
opcode `0x206f` with `bt_hci_cmd_send_sync()`; `hci_core.c:461-508` waits on a
stack-local command semaphore, and with `CONFIG_BT_TX_PROCESSOR_THREAD=y` the
sysworkq special path (474-501) is not selected and line 505 blocks the
state-transition work; with `CONFIG_BT_RECV_WORKQ_BT=y`,
`CONFIG_BT_CONN_TX=y`, and `BT_CONN_TX_NOTIFY_WQ` unset, `conn.c:283-290`
selects `&k_sys_work_q` and `conn.c:340-355` submits then blocks in
`k_work_flush()`; `kernel/work.c:458-488` waits on the stack-local flush
semaphore only when the target work is queued or running;
`nrf/subsys/bluetooth/controller/hci_driver.c:522-548,690-713` retains an ISO
HCI message after `BT_BUF_ISO_IN` allocation failure and does not fetch the
later cached command-complete message until an ISO RX buffer frees;
`hci_internal.c:1859-1875` returns the cached command complete before asking
SDC for another message; and `host/buf.c:62-67,145-151` re-signals that
retained HCI driver work only when an ISO RX buffer is freed. Command Complete
itself uses `sync_evt_pool` (`buf.c:154-180`), so the block is head-of-line
ordering in the SDC adapter, not command-complete allocation from the ISO pool.

The handoff also recorded that upstream Zephyr PR #79258 describes
`CONFIG_BT_CONN_TX_NOTIFY_WQ` as improving Bluetooth independence from
`sysworkq` while recording callback-context and testing considerations. This
hypothesis is an observation-level experiment rationale only; it was not
confirmed by a repaired row and does not prove workqueue ownership, cause, a
general repair, or production safety.

### 408-byte noinit overflow before the 1536 stack reduction

The first H40 trace build failed before final CPUAPP linking:

```text
zephyr/zephyr_pre0.elf section `noinit' will not fit in region `RAM'
region `RAM' overflowed by 408 bytes
```

Its pre-link map (`build/nrf54l15/le-audio-receiver/zephyr/zephyr_pre0.map`)
proved `RAM limit 0x20028000`, failed `_image_ram_end 0x20028198` (overflow
0x198 = 408 bytes), the `conn_tx_workq` control object at 0x128 bytes, and the
`conn.c` private noinit stack at 0x800 bytes (2048). Installed NCS v3.3.0
defines the private queue stack through
`CONFIG_BT_CONN_TX_NOTIFY_WQ_STACK_SIZE` (`zephyr/subsys/bluetooth/host/Kconfig:169-187`,
`zephyr/subsys/bluetooth/host/conn.c:4652-4669`), which has a Kconfig prompt
when `BT_CONN_TX_NOTIFY_WQ=y`. Reducing only that trace-only queue stack from
2048 to 1536 removes 512 bytes; with all other resolved H40 settings unchanged,
the projected image end was `0x20027f98`, 104 bytes inside RAM. `1536` was the
largest simple reduction that restored fit. It remains a diagnostic-only
experimental stack size; it is not a stack-health claim, does not authorize
production adoption of `BT_CONN_TX_NOTIFY_WQ`, and must not be copied to normal
board or application configuration.

### Trace fragment hash and trace-image link size

The executed H40 trace fragment
`tests/hil/receiver-sdc-remove-iso-path-tx-notify-wq.conf` at the version
recorded for this run, SHA-256
`b8b3c47a58cd6f71714999eedac83ec27e6e114c0ec242aeeda8cc44537aa32a`. The H40
trace CPUAPP linked at 163736 / 163840 bytes with the 104-byte RAM margin
recorded above.

### Disassembly annotation does not bind data-address literals

The RH3-40 link-proof-correction handoff required a fragile match:

```bash
"$toolchain_objdump" -d --disassemble=bt_conn_tx_notify "$trace_elf" | rg 'conn_tx_workq'
```

It produced no text. This does not disprove the configuration: ARM linked
disassembly does not necessarily annotate a data-address literal with its local
symbol name, even when the compile-time `#if` branch selected it. The proof
must instead use all three grounded facts: the resolved `.config` has
`CONFIG_BT_CONN_TX_NOTIFY_WQ=y`; exact installed `conn.c:283-290` selects
`&conn_tx_workq` under that compile-time condition (the `&k_sys_work_q` branch
is excluded); and the linked ELF `nm` contains the private queue object, its
stack, and `bt_conn_tx_workq_init`, with `conn.c:4652-4669` initializing and
starting that exact queue. Disassemble the private queue initializer, where
call symbols are stable, not the data-address selection inside
`bt_conn_tx_notify()`.

### Trace-fragment warning-suppression boundary

The RH3-40 planning handoff permitted `CONFIG_WARN_EXPERIMENTAL=n` only
inside the H40 trace fragment, with the recorded reason of preventing the
deliberate, upstream-marked experimental Kconfig notice from violating
repository warning policy, and required the normal restoration to prove
`CONFIG_WARN_EXPERIMENTAL=y` with `BT_CONN_TX_NOTIFY_WQ` disabled. The 1536
stack, the `WARN_EXPERIMENTAL=n` value, and the 408-byte overflow repair are
dated NCS v3.3.0 trace-fragment facts. They are not current production policy;
the current nRF54L15 repair is the separate, later receiver-only configuration
recorded in
[nrf54l15-tx-notify-workqueue-results-20260928.md](nrf54l15-tx-notify-workqueue-results-20260928.md)
with `CONFIG_WARN_EXPERIMENTAL=y` kept visible.
