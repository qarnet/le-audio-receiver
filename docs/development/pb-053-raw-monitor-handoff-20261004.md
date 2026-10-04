# PB-053: memory-safe raw guest HCI/MGMT collector

## Goal and scope

Replace optional live semantic btmon decoding with bounded raw packet collection.
R21 btmon observer died SIGSEGV (-11), preventing fresh2. DUT stack did not fail;
monitor's C semantic decoder is not required to execute selected public cases.
Do not ignore its crash, restart it silently or drop raw traffic. Preserve R21
failure and use safe collector boundary instead. No product/firmware/controller
change or acceptance weakening. Files: new `scripts/bluez_guest_monitor.py`, new
`tests/unit/bluez_guest_monitor/test_bluez_guest_monitor.py`, integration in
host/guest scripts and shared results/tests, PB-053 notes. No commit yet.

## Grounding

Linux monitor ABI from local source `include/net/bluetooth/hci_sock.h:39,43`:
HCI_DEV_NONE=65535, HCI_CHANNEL_MONITOR=2. `hci_mon.h:24-29`: packed little-endian
uint16 opcode,index,length, six-byte header. Types2/3 HCI command/event,16/17
MGMT control command/event,18/19 ISO TX/RX. Retain every opcode, not a filtered
subset. Installed Python exposes AF_BLUETOOTH31 and BTPROTO_HCI1, but not monitor
constants; use verified literal constants with cited ABI comments.
`hci_sock.c:1574-1632` uses datagram receive, MSG_TRUNC and timestamp ancillary
data on monitor channel. Unlike ISO protocol8, HCI protocol1 address decoding is
supported. No active host HCI socket may be opened; guarded guest only.

## Collector API and fixed CLI

1. Stdlib-only import, no side effects. Main guard requires exact pb053_guest=1
   in /proc/cmdline and /opt/pb053/runtime.json before socket operation.
   No arbitrary guest command/port/file/host RPC flags.
2. Main opens socket(AF_BLUETOOTH,SOCK_RAW,BTPROTO_HCI), binds (65535,2).
   Set SO_RCVBUF to2MiB. Enable Linux SO_TIMESTAMPNS35; ancillary size256. May
   enable SO_RXQ_OVFL40, record any reported drop counter but label absence
   unknown (do not claim completeness from missing ancillary counter).
   Read-only monitor channel; no HCI send or controller selection operation.
3. Print/flush one `PB053_MONITOR_READY ` JSON schema1 after successful bind.
   SIGTERM/SIGINT handlers set stop flag only; select/read loop notices<=100ms.
   Normal requested stop closes socket, emits one terminal result and exits0;
   unexpected EOF, error, truncated datagram/control, invalid six-byte length,
   log cap or observed nonzero drop counter emits failed result/exit1.
4. For each recvmsg(65541,256), validate at least6 bytes and header length equals
   actual payload. Do NOT decode PAC/codec/GATT/vendor payload. Emit one JSON line
   exact fields `packet` (index0 onward), `wall_ns`, `monotonic_ns`,
   `kernel_timestamp_ns` (int or null), `opcode`, `index`, `raw_hex`, `flags`,
   `reported_drops` (int or null). Raw hex includes six-byte header and all payload.
   Decode timestamp ancillary35 as native x86_64 timespec two signed64 integers;
   drop ancillary40 as uint32. Metadata is observation time, not RF/presentation.
5. Stream SHA256 over each datagram prefixed with its four-byte little-endian
   length. Count packets and raw bytes. No packet-history list in collector.
   Cap ALL emitted stdout to4MiB; reserve terminal-result space so exceedance
   still emits failed terminal record. Never truncate then claim success. Use
   bounded reads/record rendering. Main collector owns no child process.
6. Terminal `PB053_MONITOR_RESULT ` JSON exact schema_version1,ok,packets,bytes,
   sha256,error,stop_signal,reported_drops. Success requires requested SIGTERM or
   SIGINT, no error/drops. Null drop counter means unavailable observability, not
   zero-loss proof. Retain raw payloads privately (ephemeral bond keys may appear).

## Integration

7. Host preparation copies monitor module into /opt/pb053/monitor.py and adds
   source_hashes.monitor, current exact hash. Host validates new6-source key set.
   Runtime no longer requires btmon binary as execution prerequisite; btmgmt
   remains for verified guest controller readiness. Stock btmon source/version
   diagnostic history remains documented, not used live or mutated.
8. Guest starts actor monitor as `[runtime.python,'-u','/opt/pb053/monitor.py']`
   at same point before emulator. Wait <=5s for READY marker and actor alive;
   decode bounded initial log line, never call reader against host controller.
   Keep same actor lifetime and normal TERM exit0 requirement.
9. Add `validate_capture(text)` to shared results: bound4MiB, exact READY/result
   cardinality, duplicate-key/nonfinite rejection, packet seq continuity, header/
   hex/flags/timestamp shapes, no truncation, recompute raw bytes/hash and compare
   terminal metadata. Any reported nonzero drops fail. Terminal ok=true with
   error=None and requested signal15 or2. Exact record fields, no unknown rows.
   This validates retained observed traffic, not impossible absent-drop guarantees.
10. Guest finally after monitor stopped, validate actual exclusive monitor log,
    append `traffic_capture` stage with packets,bytes,sha256,reported_drops and
    actual opcode counts. On validation failure emit cleanup_failure and ok=false.
    Full raw JSON packet log printed unchanged through owned serial evidence.
11. Shared guest validator allows/requires one traffic_capture after monitor stop;
    valid hashes/counts, at least48 ISO TX(op18) and48 ISO RX(op19) for full3x16
    case run, positive command/event observations. No captured bytes stand in for
    missing public cases; all existing strict phase/content/lease checks remain.

## Tests and verification

Actual AF_UNIX datagram socket boundary may feed authored monitor-form datagrams
to pure `collect(sock, output, stop_predicate, ...)` helper. No host HCI socket.
Prove exact raw retention, timestamps/checksum, malformed/truncated headers,
quota failure and stop/EOF handling. Encoded capture validator rejects missing/
duplicate rows/results, changed packet with recomputed own hash still reject
only when header/sequence/count or external expected digest contradicts (raw
collector is not payload golden oracle). All tests labeled component/parser
checks, not VM claims. Existing direct-host invocation refuses before HCI open.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_guest_monitor -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_results -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
git diff --check
```

No VM/commit until review. Return exact implementation/test outcomes. If socket
ABI/ancillary/record decision unresolved, ask precise question before guessing.
No codec-decoder fallback, skipped monitor, vendor patch or warning suppression.
