# PB-053: actual guest daemon restart and state lifecycle

## Goal and scope

Extend proved valid endpoint/discovery/pairing/ISO transport run with retained
daemon-state restart and explicitly fresh daemon-state restart in same private
guest. Files: `scripts/bluez_guest_public.py`, `scripts/bluez_guest_init.py`,
focused result tests, and PB-053 notes. Do not change host controller/kernel,
vendor/SDK/production, PB-051, fixture contents, frozen limits or other dirty
files. No commit/push/Done until reviewed complete lane and clean gates.

R15 delivered16 exact120-byte valid existing corpus frames. Source/client Acquire
begins first, server/sink Acquire waits public pending, both become active.
Explicit Release precedes fd closure. Data came through real guest ISO stack.
No independent LC3, PCM, physical RF or timing claim.

## Diagnostic classifications

Fresh missing cache/key files are source-level DBG for nonexistent first-run
state, not compiler/runtime LOG_WRN. Retain exact logs. Existing disconnected
status14 has bonding(nil) after successful public Paired/Bonded and explicit
Disconnect, not failed pairing. Invalid-state-qos Disable must retain protocol
context and cannot be called successful wire disable; public lease/idle/close
state assertions remain mandatory. Do not weaken or hide diagnostics. Return
new unexplained warning/failure for Delegator source analysis.

## Exact implementation

1. Public script accepts only `--state fresh|retained` (default fresh). Existing
   guard runs before dbus imports/bus access. Include chosen state in result.
   Initial GetManagedObjects obtains two dynamically identified adapter addresses.
   Fresh state requires no initial Device1 objects reporting Paired or Bonded;
   retain snapshot before discovery. Retained state requires prior guest ledger
   `/opt/pb053/prior.json` addresses equal current two adapter addresses and both
   reciprocal Device1 objects already Paired=True and Bonded=True before any
   discovery/connect operation. No Pair call in retained mode.
2. Keep registration and discovery checks in both modes. After discovery, fresh
   mode calls Pair as currently. Retained mode calls Connect on discovered peer
   and requires both public Connected/Paired/Bonded/ServicesResolved snapshots.
   Keep endpoint configuration, actual16-frame acquisition/delivery and full
   cleanup checks in both modes, resetting callback/FD resources per process.
   Add `state_initialization` required case in every child result. Retained
   success must not be satisfied by a second Pair call or injected bond fixture.
3. Guest init starts/stops owned daemon three times using identical selected
   config/plugin/executable and same private bus/emulator/kernel. Extract existing
   daemon startup and two-adapter/ISO readiness into bounded helpers, preserve all
   existing checks. Unique daemon labels `bluez-fresh1`, `bluez-retained`,
   `bluez-fresh2` get separate logs, never overwrite previous guest log.
4. Run public child fresh first; parse complete required result and save actual
   addresses/peer paths from its successful ledger to prior.json. Stop daemon
   TERM and wait5s (KILL if required, but unexpected forced kill fails criterion).
   Confirm private bus no longer owns org.bluez using NameHasOwner via existing
   private dbus-send. Record stopped PID/exit and NameHasOwner=false. No client
   remains from first child; its GLib loop/FDs/registrations have been cleaned.
5. Record recursive state-file hashes (not raw bond keys) under guest
   `/var/lib/bluetooth`. Require actual nonempty state after first successful pair.
   Restart daemon, require different PID and public readiness. Run public child
   retained; it must observe loaded bonds and perform Connect without Pair and
   actual byte delivery. Record pre/post state identities, not constructor shape.
6. Stop second daemon with same public disappearance check. Rename guest-only
   `/var/lib/bluetooth` to `/var/lib/pb053-retained-bluetooth` (must not already
   exist); create empty `/var/lib/bluetooth`. Preserve prior directory and hashes,
   do not delete or export key files. No host path is involved. Restart third
   daemon; run public fresh child again. It must observe initial unpaired state,
   pair successfully and deliver data. This proves fresh versus retained state,
   not fresh guest kernel/controller resets; label distinction explicitly.
7. Emit successful child stages named exactly `public_fresh1`, `public_retained`,
   and `public_fresh2`, each with `operation=success` and complete `result`.
   Emit `daemon_stopped_fresh1` and `daemon_stopped_retained` with prior PID,
   exit0 and `name_has_owner=false`; emit `state_preserved` with nonempty
   retained hashes and `state_reset` with backup path/hash map.
   Guest ok=true only after all three complete child ledgers, three distinct
   daemon PIDs, public org.bluez disappear/reappear boundaries, preserved state
   hashes and full cleanup. Final host result parser must require named guest
   lifecycle stages and all three public results with correct state labels and
   complete child case set. A naked `ok=true/kernel/controllers` result must no
   longer pass parser. Tests update expected full result fixture and reject
   missing/wrong/duplicated lifecycle stage or child case evidence.
8. Keep finally cleanup for latest alive daemon plus original bus/emulator; print
   every unique daemon log, including stopped previous instances. Do not double
   stop earlier PID or overwrite log. Failure at any stage returns ok=false and
   all existing failure evidence. Short command timeouts15s, public child timeout
   90s per iteration; total host240s still bounds execution. If needed, report
   measured normal duration before proposing any larger budget.

## Verification

```sh
python3 -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-r16
python3 scripts/bluez_host_guest.py run --prepared /tmp/opencode/pb053-guest-prep-r16 --output /tmp/opencode/pb053-guest-state-r16 --timeout 240
git diff --check
```

Use unused suffix if occupied. Stop first unexplained failure/warning and report
exact public stage/logs. Do not copy stored keys between test states, fake
public Paired state, silently call Pair during retained reconnect, remove cases
or treat daemon restart as kernel restart. No acceptance/commit/push yet.
