# PB-053: public lease and disconnect cleanup ordering

## Goal and files

Repair fixture-generated duplicate teardown procedures without weakening resource
or public-state checks. Files: `scripts/bluez_guest_public.py`, focused guest
tests, PB-053 notes. No other production/vendor/kernel/SDK or host Bluetooth
changes. No commit yet; full lane accounting/cancellation still open.

## Source-grounded decisions

R19 now passes all three fresh/retained/fresh child flows and actual disconnect
quiescence after prepowered retained-controller readiness. It proves repairing
AddDevice startup prevents stale kernel autoconnect. Silent bus-close exit was
fixed by set_exit_on_disconnect(False). Preserve all previous failing evidence.

Current fixture releases sink before source. Pinned BlueZ
`src/shared/bap.c:2192-2202` server Release disables locally, then client Release
sends another Disable into QoS, producing Invalid ASE State. Correct client-first
ordering triggers actual valid wire Disable. `transport.c:1801-1823,2324-2354`
revokes server-side owner when peer state enters QoS. Therefore second server
Release need not be legal: caller must prove revocation through State and fd,
not accept NotAuthorized as success.

Current simultaneous Device1 Disconnects create duplicate MGMT disconnection
warning. `device.c:2372-2386` disables each untrusted peer autoconnect first;
`device_request_disconnect:2117-2120` replies immediately if already disconnected.
After valid source Disconnect and observed both disconnected, reciprocal
Disconnect safely suppresses its remaining policy without a second ACL command.

## Exact changes

1. Each acquired lease stores role source/sink. Success cleanup releases SOURCE
   first while its fd still owned, require normal D-Bus Release reply. Close its
   local socket afterward, with existing exactly-once ledger.
2. Wait sink public MediaTransport1 State becomes idle (<=10s). Verify sink fd
   has EOF or POLLHUP, not merely no data or arbitrary error. `select.poll` can
   prove POLLHUP without unsupported Bluetooth address decoding. If readable,
   existing receive_packet with bounded deadline may verify b'' EOF; unexpected
   nonempty packet fails. POLLERR alone is not successful revocation. Record
   `revoked` transport path, State, hangup/EOF evidence; close sink local socket.
   Do not call sink Release after proved remote revocation. If only sink lease
   exists on an earlier failure, attempt its ordinary explicit Release and close,
   preserve failed result. Never accept missing lease as successful delivery.
3. iso_delivery true only after all16 strict byte checks, source explicit Release,
   sink verified revocation and both local closes succeeded. Every acquired path
   has exactly one explicit-release OR proved-revocation and exactly one close.
   All failure paths still try all owned resources and retain errors.
4. Cleanup Device1 sequentially: call source-side peer Disconnect; read BOTH peer
   snapshots unconditionally each iteration and require Connected=false on both
   within10s. Then call reciprocal Disconnect while already disconnected to
   disable its policy; require normal reply and verify both remain disconnected.
   Observe both false for0.5s with bounded polling and original10s overall budget.
   Keep advertisement registered through this check, not a stimulus-removal fix.
   Unregister advertisement/endpoints/agent only after cleanup state proof.
5. Preserve normal caller limits, bonds, callback loop and all other cases. Do not
   power off to satisfy disconnect, clear kernel list/keys, fake parent states,
   accept NotAuthorized as lease release, or classify every protocol error benign.

## Remaining known readiness diagnostic

Do not suppress raw privacy errors in this phase. Source/monitor r19 prove exactly
four redundant mode-off SET_PRIVACY requests rejected only because controllers
already powered; Privacy flag stayed off before/after. `adapter.c:10774-10775`
issues setter unconditionally;4384-4392 logs actual btd_error. This is not privacy
functionality acceptance. Later explicit contract must bind mode/current state and
classify exact observed diagnostics; privacy-on rejection cannot pass. Keep raw
ASE stale-cache/ServiceChanged recovery trace too, no unsupported CCC success claim.

## Verification

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
git diff --check
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-r20
```

Return prepare's literal manifest_sha256; Delegator supplies exact run command.
Do not run VM until that command, no hash invention or old-root reuse. Add pure
ownership/EOF-versus-unread-data test through real AF_UNIX sequenced-packet fd if
factoring receiver closure predicate, not mocked helper-call counts. Stop missing
decision/first unexplained failure, preserve state. No commit/push/Done yet.
