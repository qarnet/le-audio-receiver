# PB-053: valid host ISO transfer checkpoint

## Goal and files

Add actual source/sink transport acquisition and byte-verified delivery through
the proved private Linux/BlueZ/emulator guest. Files: `scripts/bluez_host_guest.py`,
`scripts/bluez_guest_public.py`, public-result requirements in
`scripts/bluez_guest_init.py`, focused parser tests and PB-053 notes.
No production, SDK, vendor, host adapter/bus, PB-051, downloads, unrelated dirt,
commit/push or Done transition in this checkpoint. Keep prior evidence immutable.

## Grounding

R12 passed source/sink endpoint registration, discoverable LE peer discovery,
both-way Paired/Bonded/Connected/ServicesResolved and two endpoint configuration
callbacks. Both public MediaTransport1 objects have codec6 and correct selected
48kHz/10ms/120-byte configuration. Actual fd acquisition/delivery not yet proved.

Corpus `tests/fixtures/lc3/bsim_48k_10ms_120b_l.lc3` is raw concatenated 128
continuous 120-byte frames, total15360 bytes. README lines34-45,104-106 identify
its SHA256 `c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be`.
Use this existing valid regression corpus only as transport stimulus. It is NOT
independent LC3 reference, decoder acceptance or physical nRF RF evidence.

R12 debug messages have source-checked classifications, not generic waivers:
four `Unable to parse PAC` messages are DBG at `src/shared/bap.c:4951-4954` for
zero-length values of unused opposite roles. Producer `pac_foreach:468-501` adds
count byte only upon first PAC; empty queue read returns empty data at504-570.
Relevant Source/Sink PACs parse correctly and produce actual configurations.
No malformed input test is being authored or moved from PB-051. Disconnect
status14 occurs with bonding(nil), is MGMT_STATUS_DISCONNECTED, after successful
paired/bonded snapshots and explicit Disconnect; HCI status0 confirms teardown.
Retain raw logs and exact explanatory notes, do not call DBG a new authentication
failure or grant blanket parse/warning exceptions. Optional two MSFT setup errors
remain separately source/config-attributed and excluded capability, not hidden.

## Exact changes

1. Host prepare copies the corpus to `/opt/pb053/stimulus.lc3` after verifying
   size and SHA256 above. Manifest records source, size/hash and transport-only
   purpose; guest verifies same size/hash before sends. Do not regenerate bytes.
2. Keep endpoint transport ledger structured by source/sink object. After existing
   pairing/configuration checks, require one configured source path and one sink
   path, distinct. All chosen paths come from callbacks and public objects.
3. Refactor current async `call()` narrowly into begin/wait helpers so two Acquire
   requests can be outstanding together. Begin invokes D-Bus async method with
   current reply/error handlers and timeout10 and returns event/result holder.
   Wait uses current <=11s bound and same trace/error propagation. `call()` is
   begin+wait, preserving all existing behavior. Start Acquire on both public
   MediaTransport1 paths BEFORE waiting either. No blocking callback Acquire.
4. Each Acquire returns UnixFd/read_mtu/write_mtu. Use `.take()` once; own extracted
   fd immediately, then wrap `socket.socket(fileno=fd)` transferring sole ownership.
   If second acquisition fails, close already-owned fd/socket in finally. Require
   source write MTU>=120 and sink read MTU>=120. Keep exact returned MTUs/paths.
   Query both MediaTransport1 State and require active through public properties,
   bounded10s. This is actual transport readiness, not private helper wiring.
5. Send first16 distinct fixture frames from source socket, one frame per ISO
   message. Set socket deadlines3s; socket.send must return exactly120. Receive
   sink `recvmsg(121)` for each frame; require exactly120, no MSG_TRUNC, and bytes
   equal same intended frame, in order. Log per-frame index,length,SHA256 and full
   received hex. Use 10ms interval measured from monotonic deadline, never alter
   fixture or insert headers. Distinct corpus frames detect ordering/duplication.
   If receive/timing fails, preserve exact failed frame and return evidence;
   do not skip initial messages, resynchronize or invent tolerance.
6. Require no extra unread data after16th frame using bounded50ms socket timeout;
   unexpected extra datagram fails. Explicitly call public Release on acquired
   transports before closing owned sockets, matching existing driver
   `bap_central_endpoint.py:748-765`; retain results. Wait public states no longer
   active before disconnect/unregister (<=10s). Close always on error. No fd is
   retained past cleanup or closed twice. Retain lease/acquire/release ledger.
7. Add required `iso_delivery` case to public result/parser/tests, true only after
   all16 received comparisons and ownership cleanup. Existing four checks stay.
   Update scope labels to describe valid host ISO checkpoint, not full PB-053
   acceptance. No codec/PCM/I2S/physical or presentation claim.
8. Run focused tests and actual fresh prep/public roots r13. Record truthful
   outcome via Backlog.md. Stop first unexplained error/warning or missing decision.

## Verification

```sh
python3 -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-r13
python3 scripts/bluez_host_guest.py run --prepared /tmp/opencode/pb053-guest-prep-r13 --output /tmp/opencode/pb053-guest-public-r13 --timeout 240
git diff --check
```

Preserve prior roots; choose unique suffix if necessary. Do not increase timeouts,
weaken comparisons, change controller/kernel configuration or invent protocol
repairs. Escalate exact failure with raw logs. No commit/push/Done yet.
