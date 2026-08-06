# P3 handoff — separate access mode from bond inventory

Base commit: `e8a120b` (P2 accepted plus reviewed threshold-rearm fix;
worktree clean).

Implement P3 from `docs/development/user-pairing-control-plan.md`. Refactor the
pure `bt_pairing_policy` state so desired OPEN/BONDED_ONLY access and persisted
bond inventory are independent. Update current `bt_bap.c` call sites only as
needed to consume the explicit policy API while preserving feature-off behavior.
Do not add P1 Bluetooth operations or callback/lifecycle integration; those are
P4/P5.

## Goal

Policy must represent all four combinations:

| Desired mode | Inventory | Required behavior |
|---|---|---|
| OPEN | empty | accept any peer |
| OPEN | nonempty | accept any peer; preserve known bonds |
| BONDED_ONLY | empty | reject every peer |
| BONDED_ONLY | nonempty | accept only exact snapshot members |

Bond enumeration/replacement, marking, and clearing must never derive or mutate
desired mode. Mode changes must never mutate inventory.

## Grounding

- Current coupling lives in `src/bt_pairing_policy.c`:
  `bt_pairing_policy_set_bonds()` derives mode from count,
  `bt_pairing_policy_mark_bonded()` forces BONDED_ONLY, and
  `bt_pairing_policy_request_open()` clears inventory.
- Current production consumers are only `src/bt_bap.c`:
  pairing completion around lines 1077–1091, advertising rebuild around
  1157–1209, and reset around 1305 onward. Repository grep and code graph found
  no other production callers.
- Existing direct suite is
  `tests/unit/bt_pairing_policy/src/test_bt_pairing_policy.c`; it compiles the
  real production source.
- Feature remains disabled in production configs. P3 must preserve current
  nRF5340/nRF54L15 behavior and all BSim pins while making future P4 explicit.

## In scope

- Break coupled policy APIs into explicit mode and inventory operations.
- Expand direct policy tests and matrix witnesses.
- Mechanically adapt existing `bt_bap.c` callers with no transition-owner or
  Bluetooth behavior redesign.
- Migrate coverage baseline if `src/bt_pairing_policy.c` denominator or counts
  change.
- P3 results/status docs.

## Out of scope

- `pairing_mode_ops` Bluetooth implementation.
- Suspended advertising, security requests, disconnect callback routing, or
  new pairing completion behavior (P4).
- Main/lifecycle/shell changes (P5).
- Input/LED or board DT/Kconfig changes (P2/P6).
- Hardware tests.
- Capacity-policy changes beyond existing eight-entry limit.

## Exact policy API

Keep types, struct layout concept, entry limit, `get_mode`, `get_entry_count`,
`pairing_accept`, and atomic `snapshot`. Replace coupled mutators with:

```c
void bt_pairing_policy_init(struct bt_pairing_policy *policy);

int bt_pairing_policy_set_mode(struct bt_pairing_policy *policy,
			       enum bt_pairing_policy_mode mode);

int bt_pairing_policy_replace_bonds(struct bt_pairing_policy *policy,
				    const bt_addr_le_t *addrs,
				    size_t count);

int bt_pairing_policy_mark_bonded(struct bt_pairing_policy *policy,
				  const bt_addr_le_t *addr);

void bt_pairing_policy_clear_bonds(struct bt_pairing_policy *policy);
```

Remove `bt_pairing_policy_set_bonds()` and
`bt_pairing_policy_request_open()` from source/header and all callers/tests.
Do not retain compatibility wrappers: they preserve the coupling P3 exists to
remove.

### Initialization

`init` remains OPEN + empty. This preserves feature-off behavior. P5 explicitly
selects BONDED_ONLY before initial NORMAL advertising when feature is enabled.

### Mode setter

- Accept only OPEN and BONDED_ONLY.
- Return 0 on success.
- Return `-EINVAL` for any other enum value and leave full policy unchanged.
- Change only mode; preserve count and every entry.
- Idempotent.

### Inventory replacement

- `count == 0` accepts `addrs == NULL`, clears inventory, preserves mode.
- `count > 0 && addrs == NULL` returns `-EINVAL`, fully atomic/no change.
- `count > BT_PAIRING_POLICY_MAX_ENTRIES` returns `-ENOMEM`, fully atomic/no
  change.
- On success, replace inventory exactly, preserve mode, and zero unused/tail
  slots so snapshots never retain stale addresses beyond count.
- Duplicate input addresses may remain duplicate; deduplication is not a P3
  capacity-policy change.

### Mark bonded

- Add absent address when capacity allows; duplicate is idempotent.
- Preserve mode for success, duplicate, and overflow.
- Full + absent returns `-ENOMEM`, inventory unchanged.
- NULL address returns `-EINVAL`, full policy unchanged.
- No HCI/controller work.

### Clear inventory

- Set count to zero and zero all entry storage.
- Preserve mode exactly; idempotent.

### Decisions and snapshots

- OPEN always accepts any peer regardless of inventory.
- BONDED_ONLY with zero entries rejects every peer.
- BONDED_ONLY accepts only exact identity matches in inventory.
- `snapshot` copies mode + count + active entries under one spinlock hold.
  Zero output entry storage before copying active entries so caller never sees
  stale tail data.
- Add NULL guards only where specified above. Do not widen scope by redesigning
  every existing getter/snapshot pointer contract.

All operations remain pure and spinlock-protected, safe from BT callback
context. Never hold lock across external work (none should exist here).

## Mechanical `bt_bap.c` adaptation

Preserve current feature-off behavior exactly until P4:

1. Advertising rebuild / bond enumeration:
   - call `bt_pairing_policy_replace_bonds(...)`;
   - then explicitly select BONDED_ONLY when count > 0, OPEN when count == 0;
   - propagate/log either unexpected errno through existing restart error path;
   - snapshot/filter behavior remains unchanged.
2. Existing `pairing_complete(..., bonded=true)`:
   - call `mark_bonded`;
   - explicitly set BONDED_ONLY even when mark returns `-ENOMEM`, preserving
     current behavior where full inventory still changes desired mode;
   - retain current no-HCI callback rule and warning behavior.
3. Existing `bt_bap_pairing_reset()`:
   - under existing `pairing_adv_lock`, set mode OPEN and clear inventory as
     two explicit calls;
   - preserve current operation order and all Bluetooth/disconnect/restart
     behavior. P4 later replaces this old transition path.

Do not add `CONFIG_USER_PAIRING_CONTROL` branches in policy. P3 explicit calls
at current consumers preserve legacy behavior without conditional policy logic.

## Direct test plan

Update `tests/unit/bt_pairing_policy/src/test_bt_pairing_policy.c`, compiling
the real source. Prove public outcomes:

- init = OPEN + empty;
- set OPEN/BONDED_ONLY idempotently without inventory mutation;
- invalid mode returns `-EINVAL` atomically;
- replace zero keeps OPEN; replace zero keeps BONDED_ONLY (empty
  BONDED_ONLY legal);
- replace nonempty keeps OPEN (OPEN with preserved bonds legal);
- replace nonempty keeps BONDED_ONLY;
- replacement removes old inventory and snapshot tail is zero;
- replacement overflow and nonzero-count NULL input are atomic;
- OPEN + empty and OPEN + nonempty accept arbitrary peers;
- BONDED_ONLY + empty rejects every peer;
- BONDED_ONLY + entries accepts exact members and rejects others;
- mark new/duplicate preserves OPEN and preserves BONDED_ONLY;
- mark overflow preserves both mode and inventory;
- mark NULL returns `-EINVAL` atomically;
- clear bonds preserves OPEN and preserves BONDED_ONLY;
- clear is idempotent;
- snapshots expose coherent mode/count/active entries for every combination.

Tests assert observable public API/snapshot behavior, not private lock or field
identity. Update `tests/test-matrix.json` API outcomes and transition witnesses;
remove retired API names everywhere.

## Coverage and inventory

- No new production source file: numeric population remains 35.
- No new Twister suite: 33 Twister / canonical 57 children remains fixed.
- `src/bt_pairing_policy.c` functions/denominators will change. Follow coverage
  migration protocol in a separate commit after direct tests and matrix pass.
- Every production function must execute; no unchanged-file ratio decrease and
  no unexplained baseline weakening.

## Verification

Focused:

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/user-pairing-p3 tests/unit/bt_pairing_policy -p -t run
grep -R "bt_pairing_policy_set_bonds\|bt_pairing_policy_request_open" \
  src tests --exclude-dir=build
scripts/test-coverage.sh --report-only \
  --output /tmp/user-pairing-p3-cov --clean-output
python3 scripts/check-test-matrix.py --repo-root . \
  --coverage-json /tmp/user-pairing-p3-cov/coverage.json
git diff --check
```

The retired-name grep must return no matches in active source/tests (historical
development/archive docs may retain evidence).

After coverage migration, run canonical gate: expect 57/57, population 35,
existing BSim pins byte-identical. Build all three targets and run build
contract 79/79; zero new/actionable warnings.

## Commit shape

1. P3 handoff.
2. Policy separation + direct tests + mechanical `bt_bap.c` adaptation +
   matrix/inventory docs.
3. Coverage migration, if required.
4. P3 results/acceptance.

Inspect status/diff/log before commits; stage only intended files. No push, PR,
amend, force-push, destructive action, or attribution footer.
