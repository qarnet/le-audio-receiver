# PB-053: pending transport acquisition ownership

## Goal and scope

Close failure-path fd/remote-owner gap before final lane acceptance. Files: new
`scripts/bluez_guest_acquire.py`, new
`tests/unit/bluez_guest_acquire/test_bluez_guest_acquire.py`, integration in
`scripts/bluez_guest_public.py` and host staging/source pins, existing guest tests,
PB-053 notes. No change to successful protocol/data/lifecycle bounds, vendor/SDK,
host hardware/bus, PB-051 or other dirt. No VM/commit until review.

## Completed analysis

Current begin() discards PendingCall. Source Acquire starts before sink pending
poll. If poll fails, source successful UnixFd reply can exist outside leases list;
bus/process exit eventually closes it but doesn't prove explicit ownership.
Installed `_dbus_bindings.PendingCall.cancel` documentation verified: cancel
ignores reply and reply handler never runs. Thus store handles and release remote
Acquire owner before cancel; never rely only on dropping callback.

Success cleanup already proven R23: source explicit Release/close, sink public
idle+fd EOF/POLLHUP revocation/close; preserve that exact flow. Raw collector and
strict validators passed full18case/48frame run. Failure owns all requests too.

## Exact owner API

Stdlib class `AcquireOwner`, protected by threading.RLock, at most two requests.
Opaque token returned by `register(path, role)`; roles source/sink, paths nonempty
distinct strings. No new registration after cleanup begins.

- `bind(token,pending_call)`: retain object exposing cancel(), even if reply came
  before bind. If cleanup already finished, cancel immediately.
- `received(token,fd,read_mtu,write_mtu)`: takes sole fd ownership immediately;
  fd/MTUs exact nonnegative ints (bool invalid). One reply per token. If closing,
  close newly delivered fd immediately and record late-reply diagnostic. Never
  deposit fd after cleanup. Duplicate reply closes its newly transferred fd,
  records error and cannot overwrite existing owned fd.
- `failed(token,error)`: record failed reply, without changing taken lease.
- `take(token)`: return dict(path,role,fd,read_mtu,write_mtu,socket=None) once,
  transfer fd out of owner to existing leases list. Unready/error/already-taken
  or closing token fails. Owner no longer closes transferred fd.
- `begin_cleanup()`: freeze registration/reply deposit; return snapshot of all
  unclaimed request (token,path,role) records, including pending-without-fd. Do not
  discard stored fds before remote Release attempt. Idempotent.
- `finish_cleanup()`: cancel all retained PendingCalls, close every unclaimed fd,
  set fd fields None before close, collect errors independently; idempotent and
  safe against late reply/reused numeric fd. Return error strings. No locks held
  across injected external cancel or remote D-Bus calls. Expose errors read-only
  copy for parent final failure accounting, not helper-call-count acceptance.

## Public child integration

1. Instantiate owner before callbacks. Extend begin() only with keyword
   `acquire_role=None`; Acquire calls supply source/sink. Create token/register
   BEFORE asynchronous method invocation, so immediate reply has owner.
2. Acquire reply handler validates tuple3, calls UnixFd.take once, transfers fd to
   owner.received immediately, then sets completion event/outcome. On fd parsing
   error close any fd already taken, record exception/event. Store actual
   PendingCall returned by asynchronous invocation with owner.bind. Non-Acquire
   calls retain existing behavior and reply tracing.
3. Existing Acquire wait loop calls wait(request) for reply/error, then owner.take
   token and appends returned lease BEFORE wrapping socket. Remove second
   UnixFd.take. Existing exception cleanup handles raw fd if wrapping fails.
4. At finally start freeze owner, then for each unclaimed request attempt public
   MediaTransport1.Release on same sender/bus, source before sink. Keep each reply/
   error ledger. Because this is failed acquisition path, any Release error is
   recorded, never called accepted lease success. Then finish_cleanup cancels
   pending handles and closes every unclaimed fd; append diagnostics to cleanup
   errors. Existing normal transferred-lease cleanup unchanged.
5. Keep GLib loop alive through remote-release attempts and handle cancellations;
   owned object/bus/loop cleanup remains independently guarded. Late reply cannot
   create unowned local fd after close, even if cancellation races callback.
6. Host stages module alongside public.py; source_hashes adds acquire, exact7-key
   set now host,guest,public,results,monitor,acquire,emulator. Do not change image
   without caller digest; tests source-pin builders adapt explicitly.

## Verification

Use real AF_UNIX sequenced-packet/socketpair descriptors for owner component tests:
receive then take transfers data and owner cleanup leaves taken fd usable;
receive then early cleanup closes and peer observes EOF; late reply after cleanup
closes immediately; duplicate reply doesn't overwrite live first fd; second-owner
failure closes first unclaimed descriptor; repeated finish after fd-number reuse
does not close unrelated new descriptor. Inject cancel exceptions only as pure
failure semantics, still prove every actual fd closed. Do not mock fd boundary or
present cancel-call counts as acceptance. Normal descriptor-close behavior is
observable through peer EOF/fstat/send/recv.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_guest_acquire -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
git diff --check
```

No VM/commit yet. Return precise outcome. Missing owner decision, contradiction,
unexplained warning/two failed variants: stop with evidence, no invented fallback.
