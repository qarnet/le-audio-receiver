# P4 results — Bluetooth adapter and callback integration

Accepted: 2026-08-07.  Base commit `f430406` (P3 accepted); handoff
commit `d3dc9a4` (`docs: record P4 handoff — Bluetooth adapter and
callback integration`); implementation commit `c347710`; coverage
migration commit `0f954d0`; acceptance commit (this document's commit).
Handoff: `docs/development/user-pairing-control-p4-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P4 wires the accepted `pairing_mode_ops` Bluetooth side into `bt_bap.c`:
one private, Bluetooth-type-free adapter module
(`src/bt_bap_pairing_adapter.c/.h`) owns the applied NORMAL/BONDING/
SUSPENDED access state, the six P1 Bluetooth operation mechanics, and
the callback-event translation gate, while `bt_bap.c` remains the
concrete Zephyr Bluetooth owner (advertising set, `struct bt_conn` refs,
callback registration, controller filter) behind one immutable injected
backend table.  `pairing_mode` remains the sole transition owner; the
adapter applies exactly one requested operation or event per call.  The
production feature stays disabled on both boards
(`CONFIG_USER_PAIRING_CONTROL` is `n` in every production board config),
so all current behavior and every BSim pin stay byte-identical; the
feature-on build was additionally compile-verified on the nRF5340
target via a scratch build with `-DCONFIG_USER_PAIRING_CONTROL=y`.

## Files and public API

- `src/bt_bap_pairing_adapter.h` — project-private adapter contract with
  no Zephyr Bluetooth connection/advertising/address types in any
  signature (opaque `void *ctx` only).  Owns:
  - `struct bt_bap_pairing_policy_snap` (mode + count — the filter
    decision authority, never the entry count);
  - `struct bt_bap_pairing_adv_req` (`filter_connections` — the
    adapter's mode-snapshot decision);
  - `enum bt_bap_pairing_peer_state` (ABSENT/CONNECTED/DISCONNECTING/
    OTHER) and `struct bt_bap_pairing_peer_result` (state + exact errno);
  - `struct bt_bap_pairing_backend` — the immutable 17-slot backend
    table (`adv_locked` runner, six advertising/FAL/enumerate steps,
    two policy mutations, two peer operations, storage delete, five
    pairing-mode notification slots).  The backend owns every acquired
    connection ref and releases it before returning; the adapter never
    stores a peer object.
- `src/bt_bap_pairing_adapter.c` — the singleton adapter:
  `bt_bap_pairing_adapter_init` (0 / -EINVAL with any missing slot,
  atomic / -EALREADY; starts applied SUSPENDED, notifications disabled),
  the six operation mechanics (`set_access_mode`, `advertising_suspend`,
  `advertising_start`, `disconnect_peer`, `delete_all_bonds`,
  `request_security`), the one-way idempotent `notifications_enable` +
  `notifications_enabled` query, and the five callback-translation
  functions (`notify_connected/disconnected/pairing_complete/
  pairing_failed/security_changed`) which gate on the notification
  enable and log unexpected enqueue results (event name/errno) without
  retry, blocking, HCI work, or a second fatal owner; `-ECANCELED` is
  informational.  The `BT_BAP_PAIRING_ADAPTER_TEST` state-reset seam is
  GCOVR-excluded.
- `src/bt_bap.h` — the seven conditional public functions
  (`bt_bap_pairing_set_access_mode`, `_advertising_suspend`,
  `_advertising_start`, `_disconnect_peer`, `_delete_all_bonds`,
  `_request_security`, `bt_bap_pairing_notifications_enable`) declared
  only under `CONFIG_USER_PAIRING_CONTROL`, signatures exactly matching
  the P1 `pairing_mode_ops` slots (ctx accepted and ignored).
- `src/bt_bap.c` — concrete Bluetooth side:
  - the legacy advertising rebuild refactored into shared
    lock-held primitives (`bt_bap_adv_stop_locked`,
    `bt_bap_adv_fal_clear_locked`, `bt_bap_adv_bonds_replace_locked`,
    `bt_bap_adv_filter_locked`, `bt_bap_adv_filter_start_locked`) with
    the OPEN/BONDED_ONLY count derivation isolated in the legacy
    `bt_bap_restart_advertising_locked()` wrapper (feature-off behavior
    byte-identical);
  - the backend implementation (advertising steps under
    `pairing_adv_lock` via the `adv_locked` runner; peer operations via
    `find_live_peer()`/`bt_conn_foreach()` with per-operation owned refs
    and unref before return; `bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY)`
    storage deletion; `pairing_mode_notify_*` enqueues);
  - the seven thin public functions; adapter init after advertising
    creation in `bt_bap_init` (failure propagates as boot failure);
  - callback forwarding: `connected` notify after ref/`default_conn`,
    `disconnected` notify exactly once after teardown + ownership
    cleanup, `pairing_complete` marks inventory first then notifies
    bonded/unbonded honestly with the legacy BONDED_ONLY selection
    retained only while notifications are disabled, `pairing_failed`
    notify, and a new `security_changed` conn callback (success =
    `err == BT_SECURITY_ERR_SUCCESS`; bonded only when success AND
    `bt_le_bond_exists(BT_ID_DEFAULT, bt_conn_get_dst(conn))`).
- Root `CMakeLists.txt` — `zephyr_sources_ifdef(CONFIG_USER_PAIRING_CONTROL
  src/bt_bap_pairing_adapter.c)`.
- `scripts/check-test-matrix.py` — `BT_BAP_PAIRING_ADAPTER_TEST` added
  to the test-only macro set (seam stripped from public-API discovery).
- `tests/unit/bt_bap_pairing_adapter/` — the new direct Twister suite.
- `tests/test-matrix.json` — direct entry for
  `src/bt_bap_pairing_adapter.c` with 35 exact outcomes and 6
  transitions (applied access state + notification gate).
- `docs/testing/coverage-matrix.md` — source row + suite inventory
  (twister 33 → 34; gate children 57 → 58) in the implementation commit,
  and the "P4 baseline migration" provenance below (in the acceptance
  commit — see Deviations).

## Applied access state and operation contracts

`bt_bap_pairing_adapter_init()` starts the atomic applied access state
at SUSPENDED with notifications disabled.  Only the operation callbacks
mutate it:

- NORMAL → policy BONDED_ONLY (empty inventory legal), inventory
  preserved, applied published NORMAL; policy setter failure propagates
  without publication.
- BONDING → policy OPEN, inventory preserved, applied published BONDING.
- SUSPENDED → applied published SUSPENDED, policy mode and inventory
  untouched (duplicate publication after suspend is benign).
- Any other mode → -EINVAL, nothing changes.

`advertising_suspend` (under `pairing_adv_lock`) calls the idempotent
`bt_le_ext_adv_stop` and publishes SUSPENDED only on success.
`advertising_start` (under the lock) rejects applied SUSPENDED with
-EACCES, then runs stop → FAL clear → enumerate+replace → atomic policy
snapshot → params (FILTER_CONN + FAL rebuild when the snapshot mode is
BONDED_ONLY, even with zero entries; OPEN/BONDING leaves filtering off
while retaining the inventory) → start, propagating the first exact
errno.  `disconnect_peer` handles NULL pending (-EINVAL), no-peer
(success, no pending), CONNECTED (disconnect with
`REMOTE_USER_TERM_CONN`; pending on success, exact errno otherwise),
DISCONNECTING (pending, no duplicate command), and other (no pending).
`delete_all_bonds` clears the in-memory inventory only after
`bt_unpair` storage success (desired mode preserved; no disconnect —
the owner guarantees close first).  `request_security` returns
-ENOTCONN for absent/disconnecting peers, else the exact
`bt_conn_set_security(BT_SECURITY_L2)` result.

## Tests (37 direct, new twister suite)

`tests/unit/bt_bap_pairing_adapter` compiles the real production
`src/bt_bap_pairing_adapter.c` against a fake backend with an operation
ledger, per-slot result injection, snapshot control, and the
`BT_BAP_PAIRING_ADAPTER_TEST` seam.  Every handoff case is covered (37
tests): init NULL/-EINVAL atomic/-EALREADY/ops-before-init -EINVAL/
starts-SUSPENDED; NORMAL empty inventory still filters; BONDING
preserves entries and produces unfiltered advertising; SUSPENDED
prevents start (-EACCES, lock-runner only); restart exact order
stop→FAL clear→enumerate/replace→snapshot→params→start; every boundary
failure exact errno and stops later operations; disconnect
no-peer/CONNECTED/DISCONNECTING/error + ref-balance (one owned backend
lookup per operation, no peer pointer crosses the API); delete clears
inventory only after storage success and preserves on failure; security
no-peer/error/success; invalid access atomic; callback notification
gate, exact payloads, duplicate completion forwarding, -ECANCELED and
unexpected-enqueue tolerance with no retry and no inline
transition/HCI action.

Focused verification (handoff commands): the new suite → **37 PASS /
0 FAIL**, zero warnings; `tests/unit/pairing_mode` → **32 PASS /
0 FAIL**; `tests/unit/bt_pairing_policy` → **23 PASS / 0 FAIL**;
feature-on nRF5340 scratch build with `-DCONFIG_USER_PAIRING_CONTROL=y`
→ exit 0 (zero new warnings); `scripts/test-coverage.sh --report-only
--output /tmp/user-pairing-p4-cov --clean-output` → population 36,
`bt_bap_pairing_adapter.c` 139/140 L, 85/106 B, 17/17 F;
`python3 scripts/check-test-matrix.py --repo-root . --coverage-json
/tmp/user-pairing-p4-cov/coverage.json` → **0 errors, 0 notes**;
`git diff --check` clean.

## Coverage migration (35 → 36)

Candidate generated on the clean implementation commit `c347710` via
`scripts/test-coverage.sh --write-baseline /tmp/p4-baseline-candidate.json`
(`--output /tmp/p4-cov-candidate --clean-output`), inspected, and
committed byte-exact as `tests/coverage-baseline.json` (commit
`0f954d0`).  Tool versions unchanged (gcovr 8.4 / gcov (GCC) 14.3.0).
Only `src/bt_bap_pairing_adapter.c` enters the population (35 → 36);
every unchanged file stays at or above its committed record
(programmatic check: zero decreases).  New-file record: **139/140 L,
85/106 B, 17/17 F** (every function 100% executed; the
`BT_BAP_PAIRING_ADAPTER_TEST` seam is GCOVR-excluded and never enters
the numeric population).  Aggregate: lines 4511/4967 → 4650/5107,
branches 1925/2702 → 2010/2808, functions 340/340 → 357/357; the
deltas (+139 L, +106 B, +17 F) equal exactly the new file's record.
Zero-hit enforcement clean: 357/357.  No baseline weakening and no
unexplained denominator migration.  Provenance:
`docs/testing/coverage-matrix.md` "P4 baseline migration".

## Canonical gate (G1)

`./scripts/test-all.sh` on clean `0f954d0` → **58 PASS / 0 FAIL /
58 TOTAL**, exit 0 (34 twister + 5 exec-only + 16 Python + coverage +
matrix + BSim Stage 1; log `/tmp/p4-gate.log`).  Coverage child: baseline
enforcement **0 errors** against the migrated baseline.  Matrix child:
**0 errors, 0 notes**.  BSim Stage 1: all 17 scenarios strict-checked;
every existing pin byte-identical (mono 10 ms `0x22AB5C0D`, mono 7.5 ms
`0x01A3EB05`, Mode A/B 10 ms `0xBAE24F7E`, Mode A/B 7.5 ms
`0x2D95D15C`/`0xFF82CADB`, invalid-SDU resume, one-CIS-loss, reconnect
fresh mono oracle).  `git diff --check` clean.

## Builds and build contract

- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
  only the documented pre-existing NCS v3.3.0 diagnostics
  (PARTITION_MANAGER deprecation, SW Split experimental symbols incl.
  `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice notice, `__ASSERT()`
  informational, FLPR-image `UART_CONSOLE` assigned-but-got) — zero
  new/actionable warnings from P4 and zero warnings from any changed
  file.
- Build contract **79/79** (`python3 scripts/check-build-contract.py
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` → 79 assertions,
  0 failed, BUILD CONTRACT PASSED).
- The `warning:` lines inside the canonical gate log are pre-existing
  native_sim test-entropy notices and expected negative-path output
  from deliberately-failing-injection Python suites — identical to the
  P3 gate log, not introduced by P4.

## Deviations and notes

- **No amend of the coverage commit** (policy: never amend): the P3
  precedent placed the "P3 baseline migration" provenance section in
  the coverage commit, but the P4 baseline commit `0f954d0` was already
  created before the provenance text was drafted.  The provenance
  section therefore lives in this acceptance commit alongside the
  results/STATUS updates instead of the coverage commit; the committed
  baseline JSON itself is byte-exact from the candidate.
- The adapter's rejected start/suspend paths run inside the
  `adv_locked` runner (the handoff's "lock pairing_adv_lock; reject
  applied SUSPENDED" ordering), so a rejected operation records only the
  lock-runner calls — pinned by the direct tests
  (`expect_lock_runner_only`).
- The `security_changed` callback and the pairing backend are compiled
  only under `CONFIG_USER_PAIRING_CONTROL`; feature-off builds keep the
  exact legacy `BT_CONN_CB_DEFINE` (connected/disconnected only), so no
  feature-off behavior or BSim pin changes.
- No lifecycle/shell/main/board enablement (P5/P6), no advertising
  payload differentiation, no audio/BAP stream lifecycle changes, no
  hardware tests (P8) — all per the handoff scope.

## Next-phase grounding

P5 (lifecycle and shell integration) can consume the seven `bt_bap.h`
functions unchanged as the Bluetooth half of `struct pairing_mode_ops`
(the other two slots — `user_pairing_io_led_set` and the cold reboot —
already exist from P1/P2), call `bt_bap_pairing_notifications_enable()`
only after a successful `pairing_mode_init()`, route the main-loop
advertising restart through `pairing_mode_start()` (removing the
legacy count derivation in `bt_bap_restart_advertising_locked()`), and
route `bt unpair` through `pairing_mode_request_reset_sync()`.  P6
enables `CONFIG_USER_PAIRING_CONTROL` in the nRF54L15 board conf only;
P8 runs the hardware acceptance matrix.
