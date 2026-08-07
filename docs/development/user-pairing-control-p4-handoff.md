# P4 handoff — Bluetooth adapter and callback integration

Base commit: `f430406` (P3 accepted; worktree clean).

Implement P4 from `docs/development/user-pairing-control-plan.md`. Wire the
accepted `pairing_mode_ops` Bluetooth side into `bt_bap.c`, represent explicit
NORMAL/BONDING/SUSPENDED access, and translate Bluetooth callbacks into queued
P1 notifications. Keep lifecycle, user I/O initialization, shell routing, and
production feature enablement for P5/P6.

## Fixed scope

In scope:

- six Bluetooth-backed P1 operations in `bt_bap.c`;
- explicit applied access state and advertising suspension;
- connection, disconnect, pairing, and security callback forwarding;
- exact bond inventory/policy behavior from P3;
- direct adapter tests with fake Bluetooth boundary plus existing BSim/build
  regression gates;
- coverage migration and P4 results.

Out of scope:

- `main.c`, `app_lifecycle`, `bt_shell`, or user I/O boot wiring (P5);
- XIAO aliases/config enablement (P6);
- production hardware tests (P8);
- advertising payload differentiation;
- audio/BAP stream lifecycle changes.

Production board configs remain feature-off, preserving current behavior and
BSim hashes.

## Grounded SDK behavior

NCS v3.3.0 evidence:

- `struct bt_conn_cb.security_changed` is
  `void (*)(struct bt_conn *, bt_security_t, enum bt_security_err)` in
  `zephyr/include/zephyr/bluetooth/conn.h`.
- `bt_conn_set_security(conn, BT_SECURITY_L2)` starts pairing when needed and
  succeeds without work when already secure enough.
- `bt_le_bond_exists(BT_ID_DEFAULT, addr)` is the bonded reconnect check.
- `bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY)` returns 0 with zero bonds, deletes
  every stored bond, and disconnects bonded peers, but does not disconnect a
  connected unbonded peer. P1 disconnects first, so delete operation must not
  duplicate connection ownership.
- `bt_le_ext_adv_stop()` returns 0 when already stopped.
- `bt_conn_foreach()` refs during callback only; `find_live_peer()` already
  takes its own ref. `bt_conn_disconnect()` returns 0 in DISCONNECTING and
  `-ENOTCONN` after DISCONNECTED.
- auth/security callbacks run in Bluetooth RX processing context: keep short,
  do pure policy updates plus P1 enqueue only; no advertising/filter rebuild,
  blocking, or shell work.

## Public surface

Extend `src/bt_bap.h` only when `CONFIG_USER_PAIRING_CONTROL`:

```c
int bt_bap_pairing_set_access_mode(enum pairing_access_mode mode, void *ctx);
int bt_bap_pairing_advertising_suspend(void *ctx);
int bt_bap_pairing_advertising_start(void *ctx);
int bt_bap_pairing_disconnect_peer(bool *pending, void *ctx);
int bt_bap_pairing_delete_all_bonds(void *ctx);
int bt_bap_pairing_request_security(void *ctx);
void bt_bap_pairing_notifications_enable(void);
```

Signatures exactly match P1 operation slots. `ctx` is ignored; accept NULL and
non-NULL because P1 has one shared context. P5 builds the full ops table with
these six callbacks plus `user_pairing_io_led_set` and cold reboot.

`bt_bap_pairing_notifications_enable()` is one-way and idempotent. P5 calls it
only after successful `pairing_mode_init()`. Before enable, callbacks preserve
legacy behavior and never call P1 (avoids `-EINVAL` before controller init).

## Applied access state

Add private atomic applied access state, initialized SUSPENDED in `bt_bap_init`
after advertising creation. Only operation callbacks mutate it.

- NORMAL: set P3 policy mode BONDED_ONLY; preserve inventory; publish NORMAL.
- BONDING: set P3 policy mode OPEN; preserve inventory; publish BONDING.
- SUSPENDED: publish SUSPENDED; preserve policy mode and inventory.
- Invalid mode: `-EINVAL`, no change.

Policy setter happens before access-state publication. Any failure propagates.

## Advertising operations

Refactor current locked advertising rebuild into shared primitives without
changing feature-off behavior.

### P1 suspend

- lock `pairing_adv_lock`;
- call idempotent `bt_le_ext_adv_stop(adv)`;
- only on success publish applied SUSPENDED;
- unlock and return exact errno.

P1 then calls set-access SUSPENDED; duplicate publication is benign. No filter
or inventory mutation during suspend.

### P1 start

- lock `pairing_adv_lock`;
- reject applied SUSPENDED with `-EACCES` (P1 must set NORMAL/BONDING first);
- idempotently stop advertising;
- clear controller FAL;
- enumerate persisted bonds and call `bt_pairing_policy_replace_bonds()`;
- do **not** derive desired mode from count;
- snapshot mode/inventory atomically;
- NORMAL/BONDED_ONLY sets `BT_LE_ADV_OPT_FILTER_CONN` even with zero entries;
- BONDING/OPEN leaves connection filtering off while retaining inventory;
- update parameters and start advertising;
- propagate first exact errno, never report false success.

Keep legacy `bt_bap_restart_advertising()` behavior while feature is not wired:
it may continue deriving OPEN/BONDED_ONLY from bond count. Isolate derivation in
legacy wrapper; P1 operation must never derive mode. P5 removes legacy main-loop
ownership when enabled.

## Connection operations

Reuse `find_live_peer()`/`bt_conn_foreach()` to obtain one owned ref; do not
read `default_conn` from controller work without synchronization.

### Disconnect

- `pending` NULL returns `-EINVAL`.
- Initialize `*pending=false`.
- No live peer: success.
- CONNECTED: call `bt_conn_disconnect(...REMOTE_USER_TERM_CONN)`; on success
  set pending true.
- DISCONNECTING: no duplicate command; set pending true and return 0.
- Any other state: no pending, success.
- Info/disconnect failure propagates exact errno; always unref.

### Security

- Find live CONNECTED peer; no peer returns `-ENOTCONN`.
- Call `bt_conn_set_security(peer, BT_SECURITY_L2)` and return exact result.
- DISCONNECTING/non-connected returns `-ENOTCONN`; always unref.

### Delete bonds

- Call `bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY)`.
- Only after success call `bt_pairing_policy_clear_bonds()`.
- Preserve desired mode (P1 remains SUSPENDED until reset feedback ends).
- Return exact errno. Do not disconnect here; P1 guarantees close first.

## Callback forwarding

When notifications are enabled:

- successful `connected`: after storing/refing connection, call
  `pairing_mode_notify_connected()`; callback performs no security request
  directly;
- matching `disconnected`: after teardown and connection ownership cleanup,
  call `pairing_mode_notify_disconnected()` exactly once;
- bonded `pairing_complete`: mark inventory first, then notify
  `pairing_mode_notify_pairing_complete(true)`; unbonded completion notifies
  false; do not set policy mode here when enabled;
- `pairing_failed`: notify `pairing_mode_notify_pairing_failed()`;
- `security_changed`: success means `err == BT_SECURITY_ERR_SUCCESS`; bonded is
  true only when success and `bt_le_bond_exists(BT_ID_DEFAULT,
  bt_conn_get_dst(conn))`; notify both booleans.

P1 enqueue returns 0 or `-ECANCELED` after fatal. Log unexpected returns with
event name/errno; never retry, block, perform HCI work, or create another fatal
owner. `-ECANCELED` gets debug/informational handling, not duplicate recovery.

Pairing and successful security completion both may arrive. P1 mode/phase guard
makes second completion a no-op; callback adapter forwards both honestly.

Before notification enable, retain exact legacy callback behavior:
pairing-complete marks inventory and selects BONDED_ONLY. After enable, mode is
owned only by P1 operations. Feature-off builds must remain behavior-identical.

## Legacy reset

Keep `bt_bap_pairing_reset()` temporarily for feature-off shell/build
compatibility. Do not call it from new operations. P5 routes shell through
`pairing_mode_request_reset_sync()` and removes/retire this transition path.

## Test architecture

Do not attempt to unit-link all audio/BAP code. Extract new operation mechanics,
applied-access state, and callback-event translation into
`src/bt_bap_pairing_adapter.c/.h`. This module receives one private backend
table supplied by `bt_bap.c`; backend slots perform advertising/FAL/bond
enumeration, peer disconnect/security, storage deletion, policy mutation and
snapshot, and P1 notification calls. Adapter owns no visible transition state:
it only applies one requested operation/event, while `pairing_mode` remains sole
transition owner. `bt_bap.c` remains concrete Zephyr Bluetooth owner and keeps
all `struct bt_conn`, advertising-set, callback registration, and ref lifetime
details. Keep adapter header project-private and free of Zephyr Bluetooth
connection/advertising types; opaque peer/backend context pointers are allowed.
Public production API remains seven `bt_bap.h` functions above, implemented as
thin calls into singleton adapter state.

Backend return contracts must carry enough typed data for adapter decisions:
bond inventory and atomic policy snapshot, whether peer is absent/CONNECTED/
DISCONNECTING/other, and exact errno. Backend owns every acquired peer ref and
must release it before returning; adapter never stores opaque peer pointers.
Initialization installs one immutable backend/context and starts applied access
at SUSPENDED. Second initialization returns `-EALREADY`; invalid/missing backend
slots return `-EINVAL` atomically. `bt_bap_init()` performs this initialization
after advertising creation; failure propagates as boot/BAP init failure.

Add `tests/unit/bt_bap_pairing_adapter`, compiling real production adapter with
fake backend functions. Prove:

- NORMAL empty inventory still produces filtered advertising;
- BONDING preserves entries and produces unfiltered advertising;
- SUSPENDED prevents start;
- restart operation order stop→FAL clear→enumerate/replace→params→start;
- every boundary failure returns exact errno and stops later operations;
- disconnect no-peer/CONNECTED/DISCONNECTING/error behavior and ref balance;
- delete success clears inventory only after storage success; failure preserves;
- security no-peer/error/success behavior;
- invalid access atomic;
- callback notification gate, exact payloads, duplicate completion forwarding,
  unexpected enqueue errors, and no inline transition/HCI action.

Do not copy production decision logic into tests and do not expose Bluetooth
types through adapter API.

Update matrix/coverage docs. New production file raises population 35→36 and
requires baseline migration. New Twister suite raises 33→34 and canonical
57→58.

## Verification

Run focused new adapter tests, P1 policy/controller regressions, then:

```bash
scripts/test-coverage.sh --report-only --output /tmp/user-pairing-p4-cov --clean-output
python3 scripts/check-test-matrix.py --repo-root . \
  --coverage-json /tmp/user-pairing-p4-cov/coverage.json
git diff --check
```

After any coverage migration run canonical gate, all three builds, and build
contract. Require all tests pass, every new function executed, no unchanged-file
coverage decrease, BSim pins byte-identical, and zero new/actionable warnings.

## Commit shape

1. P4 handoff.
2. Bluetooth adapter/callback implementation + direct tests + matrix docs.
3. Coverage migration if needed.
4. P4 results/acceptance.

No production feature enablement, hardware actions, push, PR, amend,
force-push, destructive operation, or attribution footer.
