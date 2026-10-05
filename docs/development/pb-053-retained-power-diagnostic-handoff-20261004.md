# PB-053: retained-controller readiness diagnostic

## Goal, scope and non-scope

Test a source/binary-grounded fixture repair for retained daemon startup, with
timestamped guest-only HCI/MGMT evidence. Files: `scripts/bluez_host_guest.py`,
`scripts/bluez_guest_init.py`, focused guest parser fixtures and PB-053 notes.
No changes to public child behavior or assertions, no vendor/kernel/SDK patch,
host controller/bus mutation, downloads, PB-051 inputs, commit or acceptance.
This is diagnostic evidence, not final safe/strict host-lane acceptance.

## Completed technical analysis

R17 HCI shows successful Disconnect of CIS0100 and ACL0001, followed by three
successful LE Extended Create Connection commands from hci0. Public Disconnect
failure is reconnect after actual teardown, not controller rejection.

Pinned BlueZ `src/adapter.c:5902-5908` removes device from daemon connect_list
after failed AddDevice. Later public Disconnect skips kernel removal because
daemon believes device absent. Installed kernel7.1.5 module was inspected through
anonymous in-memory ELF disassembly, no source/module changes:

- `hci_cmd_sync_queue` at text5f049 tests hdev+60 bit2, clear returns -100.
- `add_device` calls hci_conn_params_set at32f68, queues at32fb6; negative result
  completes MGMT0033 Failed03 and frees pending at32fe8/32ff5 without params rollback.
- Bit2 corresponds to HCI_RUNNING in available linux7.3 source (name is source
  analogy; byte/branch/order are actual7.1.5 binary proof).
- Compressed actual module SHA256
  `dfc0f5099eeefddd5ad265d8f1a4e62405ac1234c5bf4400e75e9ab2ba3ed7f2`.

Hypothesis: daemon restart powers adapters down, then loads retained autoconnect
records before power-on, leaving kernel params enabled after queue rejection.
Powering these same guest controllers before loading daemon should avoid that
failure and let public Disconnect remove the kernel list normally. This is NOT
proved causation yet: must observe actual AddDevice statuses and teardown.
No bond/kernel/controller reset or rule relaxation is allowed.

Verified binaries already in staged BlueZ root:
`/nix/store/8l7syi03wm19x41yvarswrqzz7jq404r-bluez-5.87/bin/btmgmt` and `btmon`.
`btmgmt --help` lists --index and --timeout; command power on/off and info.
`btmon --help` lists -T date, -M mgmt, -P no-pager, -c color, -C columns.
Do not execute management commands on host. One interactive host help command
timed out during lookup; no controller mutation was requested, process closed.
Never repeat interactive host command; all actual mgmt goes inside guarded guest.

## Exact implementation

1. Add checked btmgmt/btmon absolute paths to required preparation artifacts and
   runtime JSON. Same installed BlueZ root closure already present. Keep current
   independently anchored schema2 runner, safe snapshots and run_owned owner.
2. Guest after modules/bus and BEFORE emulator starts: start owned monitor labeled
   `monitor`, argv `[runtime.btmon, '-T', '-M', '-P', '-c', 'never', '-C', '160']`.
   stdout/stderr exclusive `/tmp/pb053-monitor.log`, retained with all other logs.
   Monitor only sees private guest controllers. Keep monitor alive through all
   three daemon phases and terminate it before emulator so final events captured.
   Adapt finally cleanup ordering explicitly: daemon instances, emulator, monitor,
   then bus is acceptable if monitor remains until emulator closes. Pinned
   `monitor/main.c:36-43,315` handles SIGTERM through mainloop_quit, so require
   normal exit0 for monitor too; SIGKILL/abnormal exit remains failed cleanup.
3. Fix existing emitter bug: append every final stopped event to stages, including
   PID. Previously events printed but missing from final marker. Host validation
   expected process set now includes monitor; all six processes must exit0.
   Tests validate actual emitted stop record, not manually patching omitted fields.
4. Fresh1 success prior.json also records two actual Adapter1 paths from child
   adapter event. Before retained and fresh2 daemon start, require exactly those
   two guest paths matching `/org/bluez/hci[0-9]+`, current sysfs guest HCI devices
   still present. Derive indices from paths, never fixed host HCI indices.
   Invoke guest btmgmt `--timeout 10 --index INDEX power on` on each, followed by
   `--timeout 10 --index INDEX info`. Require normal exit and current-settings
   tokens include powered; retain full raw replies and events with both wall and
   monotonic timestamps. Verify Address from info equals prior public address.
   No clear/remove/unpair/reset, no pre-disconnect Powered-off replacement.
5. Emit `controller_ready` stage for each prepowered phase with phase name,
   indices, addresses and powered=true. This declares precise selected readiness
   contract. Preserve baseline r16/r17; do not relabel them passing.
6. Keep all public fresh/retained/fresh2 and Connected=false10s assertions unchanged.
   Run once into fresh preparation r18 and run r18 using returned manifest digest.
   Retain complete monitor/BlueZ/emulator logs. No assertions weakened if fails.
   On success only call it diagnostic repair witness, pending strict accounting,
   pending-FD cleanup, output bounds and cancellation acceptance.

## Verification

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-r18
python3 scripts/bluez_host_guest.py run --prepared /tmp/opencode/pb053-guest-prep-r18 --output /tmp/opencode/pb053-guest-state-r18 --timeout 240 --manifest-sha256 DIGEST_RETURNED_BY_PREPARE
git diff --check
```

Use unused suffix if occupied. Preserve raw failure result/owned process record.
Stop first unexplained warning/failure and return actual AddDevice and subsequent
HCI/control-state evidence. Do not change library, timeout, keys, public behavior,
or acceptance based on guess. No commit or push until further review handoff.
