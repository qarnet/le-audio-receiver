# Pairing-mode filter — historical implementation handoff (COMPLETE / ACCEPTED)

Status: **COMPLETE / ACCEPTED (2026-08-04).**  This document is the
historical implementation handoff for the production pairing-mode filter
landed during Phase T8 (hardware baseline freeze).  It records what was
implemented, the accepted behavior, the evidence, and the future physical
button limitation.  It is written after the fact from the accepted,
committed behavior — it is a status/implementation record, not a
pre-implementation plan.  Do not treat it as a request to change code.

Implementation commit chain (oldest → newest):

```
8fd7bb0  feat: bonded-only controller filter with production pairing reset
6578a9c  chore: refresh coverage baseline for pairing-policy module
19bec75  fix: defer pairing-reset restart while a link is tearing down
46100a9  fix: preserve-bond reconnect via BlueZ Connect, no raw-HCI helper
a40f75e  fix: snapshot pairing-policy state, serialize reset under adv lock
4488f53  fix: fresh BlueZ reconnect when device already connected
```

Final accepted production code commit: `971e6a4` (the exact final code the
T8 hardware matrix and final software gate ran on).

## Accepted behavior contract

Production module: `src/bt_pairing_policy.{c,h}` (`bt_pairing_policy.c`).
The mode derives from persisted bonds at boot:

- **OPEN** — no connection filtering; any peer may pair/connect.  Selected
  when zero bonds exist (fresh boot / after reset).
- **BONDED_ONLY** — only persisted bonds may establish connections.
  Selected when one or more bonds exist.  Successful bonded pairing marks
  BONDED_ONLY as the desired state.

Controller-level enforcement (the primary mechanism):

- `CONFIG_BT_FILTER_ACCEPT_LIST=y` (resolved build contract `[5340-028]` /
  `[54l15-034]`).  At each advertising restart the controller filter accept
  list (FAL) is rebuilt from the bonded entries only
  (`src/bt_bap.c` pairing-policy integration: `bt_le_filter_accept_list_clear()`
  → snapshot via `bt_pairing_policy_snapshot()` → `bt_le_filter_accept_list_add()`
  per bonded entry, all serialized under the advertising lock).  Unbonded
  peers are not on the FAL, so the controller filters their connect
  requests before they reach the host.
- The snapshot is taken atomically under the policy lock so the mode and
  entries can never be observed torn during the rebuild (`a40f75e`).

Defense in depth (host-level):

- The `pairing_accept` callback rejects any unbonded peer with
  `LOG_WRN("Pairing rejected (BONDED_ONLY): unbonded peer …")` +
  `BT_SECURITY_ERR_PAIR_NOT_ALLOWED`.  The callback issues no HCI commands —
  pure policy (`src/bt_bap.c` pairing-policy integration).

Production pairing reset API (used by the shell and available to any future
caller):

- `bt_pairing_policy_request_open()` — desired state to OPEN, snapshot
  cleared.
- Shell command `bt unpair` (`src/audio_shell.c` `cmd_bt_unpair`) prints
  `Pairing mode reset: bonds cleared; open pairing enabled.` and runs the
  production reset: clear all persisted bonds (`bt_unpair(BT_ID_DEFAULT,
  BT_ADDR_LE_ANY)` — also disconnects any active link), set OPEN mode, and
  restart advertising so the FAL is cleared and fresh pairing is possible
  (`src/bt_bap.c` pairing reset path).
- `a40f75e` snapshots the policy state and serializes the reset under the
  advertising lock; `19bec75` defers the pairing-reset restart while a link
  is tearing down so the restart cannot race the disconnect.

## BlueZ preserve-bond reconnect

`scripts/bap_central.py --preserve-bond`:

- Keeps the existing BlueZ `Device1` record (with its bond) instead of
  removing it; `Pair()` is skipped when the bond is already present.
- Reconnects via BlueZ `Device1.Connect()` — no raw-HCI helper on this path
  (`46100a9`).  `4488f53` handles the already-connected case with a fresh
  BlueZ reconnect.

This is the central-side counterpart of the receiver's BONDED_ONLY mode:
the bonded receiver accepts the bonded central (it is on the FAL), and the
central preserves the bond across sessions, so reconnect streams without a
receiver reset.

## Tests and hardware evidence

- `tests/unit/bt_pairing_policy/` — twister suite compiling the real
  production `src/bt_pairing_policy.c`: OPEN/BONDED_ONLY derivation,
  set-bonds atomicity (including overflow → `-ENOMEM` with desired state
  still BONDED_ONLY), pairing-accept decisions, mark-bonded, request-open
  reset, snapshot consistency.  Coverage (committed baseline `1a5842d`):
  `bt_pairing_policy 66/66 L, 20/20 B, 8/8 F` (lines/branches/functions).
- Gate child: `python: bap_central_policy` (Python unit suite covering the
  `bap_central.py` pairing-policy / preserve-bond decision paths).
- Hardware: "Pairing filter phase" in
  `docs/testing/pre-refactor-hardware-baseline.md` — the unbonded Intel-BT
  peer `64:49:7D:E3:53:40` connects and auth-fail cycles in OPEN-mode
  sessions and is absent (filtered) from BONDED_ONLY sessions; the
  preserve-bond reconnect ran 12000 frames @ 100.0 fps with zero
  underruns/faults.
- Final software gate on the exact final code: **47 PASS / 0 FAIL /
  47 TOTAL** (see `docs/testing/pre-refactor-hardware-baseline.md`).

## Future physical button — GPIO deferred, no pin assigned

A future physical pairing-reset button is planned to call the **same
production reset API** already implemented here (the
`bt_pairing_policy_request_open()` / `bt unpair` reset path above), so no
new reset logic would be needed.  The button's GPIO wiring is **deferred**
and deliberately **not** specified in this document: no pin, no port, no
GPIO configuration, and no devicetree overlay are assigned or documented
here.  Do not invent one.  When the button is implemented, the wiring must
be a separate, reviewed change with its own evidence (pinctrl overlap
verification per AGENTS.md), and this document must not be cited as a pin
source.

## Out of scope

- No change to production code is requested or implied by this document.
- No new Bluetooth modes, no threshold weakening, no test bypasses.
- No physical button GPIO design (deferred, see above).
