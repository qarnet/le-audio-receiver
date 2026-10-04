# PB-053: strict emitted lifecycle and content ledger

## Scope and goal

Make real guest emitter and host/public validators agree, reject contradictory or
incomplete reports, retain structured failures. Files: new
`scripts/bluez_host_results.py`, new
`tests/unit/bluez_host_results/test_bluez_host_results.py`, integration edits in
`scripts/bluez_host_guest.py`, `scripts/bluez_guest_init.py`,
`scripts/bluez_guest_public.py`, existing guest tests and PB-053 notes.
No protocol/runtime configuration changes, VM until review, vendor/SDK/host
Bluetooth mutation, PB-051, downloads or unrelated edits. No commit yet.

## Grounded current behavior

R20 real guest passed fresh1/retained/fresh2, exact16x120-byte payloads per phase,
explicit source Release, idle/POLLHUP sink revocation, both fd closes, sequential
peer Disconnect and bounded disconnected observation. Six owned actors stopped0.
Privacy-off redundant setter rejection and stale GATT-cache recovery remain
raw, source-attributed diagnostics, not privacy/ASCS conformance acceptance.

Current parsers merely filter selected stage subsets; ignored extra failed stop,
unknown actor, bool PID, non-null child error, invalid hash and missing data can
falsely pass. Public command raises before failed marker is retained. Fix these
before item acceptance; do not present pure fixture tests as actual VM success.

## Shared API and emitted schema

1. Stdlib-only module exposes `decode_marker(text, prefix, max_bytes)` with exactly
   one line-anchored marker, bounded payload, duplicate JSON-key/nonfinite rejection
   and dict result. Public payload cap1MiB, guest8MiB. No read-all external files
   here; host owner already caps serial32MiB.
2. `validate_public(result, expected_state, stimulus, require_success=True)`.
   Exact fields: schema_version1, state, ok, cases, events, cleanup_errors, error.
   State exactly fresh/retained and matches expected. ok/case values exact bool;
   exact six known case keys; events list of dicts <=4096; cleanup_errors list of
   strings; error None or string. Unknown top fields reject. If ok=true require
   all cases true, empty cleanup_errors and error=None. require_success demands
   ok=true. False result may retain genuinely failed case/cleanup/error evidence.
3. On successful public result independently verify expected stimulus15360 bytes,
   SHA256c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be.
   Events with frame key must be exactly16, ordered indices0..15 (int not bool),
   sent=120, received_len=120, integer flags with no MSG_TRUNC, received_hex exact
   selected fixture frame and SHA256 exact same bytes. Wrong bytes with a recomputed
   self-hash still fail. No failed_frame/unexpected_iso event in success.
4. Success must have exactly two callback-derived transport events mapping
   endpoint `/pb053/source` and `/pb053/sink` to distinct paths, two acquired paths
   matching them, source write_mtu>=120, sink read_mtu>=120, exactly one source
   released event(ok=true,role=source), one sink revoked(state=idle, true eof or
   true pollhup with POLLHUP bit), one closed event(ok=true) each and one inactive
   event each. Ordering acquired -> frames -> source release -> source close ->
   sink revoked -> sink close -> inactive. No duplicate release/close or unknown
   acquired path. No missing EOF/HUP accepted as revocation.
5. Add schema_version1 to actual public emitter. Keep current events/data fields;
   normalization does not alter protocol/assertions. Emit final marker even after
   caught cleanup failure, never after silently lost C exit (already fixed).

## Guest lifecycle validator

6. `validate_guest(result, stimulus)` requires exact top fields schema_version1,
   ok, kernel, controllers, stages, error. Kernel7.1.5; controllers exact int2;
   ok=true and error=None. Stages nonempty list of dicts <=4096, no unknown stage.
   Allowed stage names: kernel, modules, af_alg_ecb_aes, af_alg_cmac_aes,
   bus_readiness, started, readiness, bluetooth_iso, controller_command_start,
   controller_command_reply, controller_ready,
   public_fresh1, daemon_stopped_fresh1, state_preserved, public_retained,
   daemon_stopped_retained, state_reset, public_fresh2, stopped, failure,
   cleanup_failure. Any failure/cleanup_failure rejects successful ledger.
7. Track ALL starts/stops, never filter away extra actor/failure. Exactly six actor
   labels dbus, monitor, emulator, bluez-fresh1, bluez-retained, bluez-fresh2. Each
   starts once with positive integer PID (bool invalid), nonempty argv list and
   unique PID. Each stopped once with same PID and exact int returncode0, after
   its start. Final successful ledger accounts all six.
8. Required phase ordering exactly public_fresh1 -> daemon_stopped_fresh1 ->
   state_preserved -> public_retained -> daemon_stopped_retained -> state_reset ->
   public_fresh2. Relevant daemon start precedes phase; earlier daemon stop/disappearance
   precedes next start. Three public stages operation=success and validated nested
   result with fresh/retained/fresh. Daemon disappearance record references that
   exact started PID, returncode0 and name_has_owner=false. A stopped audit for
   earlier daemon may be emitted in final cleanup, but disappearance confirmation
   must already precede next start. No stop preceding own start or early next-phase
   completion. Require readiness + ISO success for each daemon phase.
9. controller_ready exists once for retained and fresh2, before corresponding
   start, with two distinct nonnegative integer indices, two distinct valid MAC
   address strings and powered=true. Current prepower emitter has phase,controllers,
   indices,addresses,powered,wall,monotonic. Preserve fields but convert index
   strings to ints in emitted controllers/indices and command events (argv still
   uses strings). Each controller entry index/address/powered matches parallel
   lists. Require finite numeric wall/monotonic values (bool invalid).
   Existing controller_command_start/reply events are real command evidence;
   validate exactly four ordered power/info start-reply pairs per phase (two
   controllers), matching phase,index,operation. Start has nonempty argv list,
   reply has nonempty raw reply string; timestamps ordered and same selectors.
   All pairs precede controller_ready and corresponding daemon start. No arbitrary
   controller selector or ignored command-stage contradiction added.
10. state_preserved.hashes and state_reset.hashes/original_hashes are nonempty
    maps <=1024 with safe relative paths (no slash prefix/.. components) and
    lowercase64hex values. state_reset backup fixed
    `/var/lib/pb053-retained-bluetooth`; original_hashes equals preserved hashes.
    Map after retained may differ due actual cache update, no fake equality claim.

## Integration and failure retention

11. Host parse_result delegates decode+validate_guest using pinned repository
    stimulus. Host raw failed dict remains in run-record; acceptance checks never
    override owned process failure. Tests import shared helpers explicitly.
12. Guest copies shared module into `/opt/pb053/bluez_host_results.py`; prepare
    source_hashes adds `results` hash and required path, host validates exact5-key
    set. Actual guest/public emitted schemas include fields above. All final stopped
    events already appended; keep PID and actual outcomes. No synthetic stopped
    record substituted for real cleanup.
13. Guest public execution separates capture/parse/accept: keep real child exit code
    and marker, parse with require_success=False, append public stage operation
    success or failure with complete result and child_exit_code, THEN fail if child
    exit/non-ok. Never raise before structured result retained. Other short command
    behavior unchanged. Failure marker final error reflects original failed stage.
14. Set final guest error None on success or original failure string. Cleanup errors
    append independently; must not skip remaining children/logs/final marker. Any
    unexpected exit/forced kill marks false. Guarded PID1 behavior/poweroff unchanged.

## Tests and verification

Pure encoded-report tests use authored valid ledger + real checked-in fixture
bytes; label synthetic parser fixture, not VM acceptance. Reject duplicate JSON,
extra failed stop/unknown start, non-dict stage, wrong PID/return types/reuse,
bad ordering, missing readiness/controller-ready, contradictory child error,
invalid state hash/path, wrong/missing/duplicate/reordered frames and recomputed
corrupt payload hash, missing lease close or fake revocation. Serialize actual
guest final-result helper to parser once so emitter/parser schema drift fails.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_results -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
git diff --check
```

No VM/commit until review. Exact public behavior/limits unchanged; only incomplete
evidence that used to pass now rejected. If a required field/design detail is
missing, report exact question instead of inventing architecture. Two differing
failed attempts/unexplained warning/scope expansion mandate stop and evidence.
