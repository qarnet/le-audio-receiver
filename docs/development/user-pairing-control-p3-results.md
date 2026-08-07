# P3 results — separate access mode from bond inventory

Accepted: 2026-08-07.  Base commit `e8a120b` (P2 accepted plus reviewed
threshold-rearm fix); handoff commit `f8b7fcd` (`docs: record P3 handoff
— separate access mode from bond inventory`); implementation commit
`89304f7`; coverage migration commit `bc011d6`; acceptance commit (this
document's commit).  Handoff:
`docs/development/user-pairing-control-p3-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P3 refactors the pure `bt_pairing_policy` state so the desired
OPEN/BONDED_ONLY access mode and the persisted bond inventory are fully
independent.  The coupled `bt_pairing_policy_set_bonds()` (mode derived
from count) and `bt_pairing_policy_request_open()` (mode change plus
inventory clear) are removed with no compatibility wrappers and replaced
by explicit mode and inventory operations.  Current `bt_bap.c` call
sites were mechanically adapted to the explicit API with the legacy
feature-off behavior preserved byte-for-byte at runtime, so both
production targets and every BSim pin stay identical while future P4
gets the explicit surface it needs.  No Bluetooth controller or
callback/lifecycle work was added (P4/P5 scope).

## Files and public API

- `src/bt_pairing_policy.h` — decoupled contract: mode and inventory are
  independent state; empty BONDED_ONLY and OPEN-with-bonds are both
  legal.  Kept: `init`, `get_mode`, `get_entry_count`, `pairing_accept`,
  atomic `snapshot`.  Replaced:
  - `bt_pairing_policy_set_bonds()` → `bt_pairing_policy_replace_bonds()`
    (exact inventory replacement, mode preserved, tail slots zeroed;
    `-EINVAL` for nonzero count with NULL addrs, `-ENOMEM` for count
    beyond the eight-entry limit, both fully atomic).
  - `bt_pairing_policy_request_open()` → `bt_pairing_policy_set_mode()`
    (OPEN/BONDED_ONLY only, `-EINVAL` for any other enum value with the
    full policy unchanged, idempotent, inventory untouched) plus
    `bt_pairing_policy_clear_bonds()` (count and all entry storage
    zeroed, mode preserved exactly, idempotent).
  - `mark_bonded` no longer forces BONDED_ONLY: it only updates the
    inventory (add/duplicate idempotent, `-ENOMEM` overflow preserves
    both mode and inventory, `-EINVAL` NULL), so marking never derives or
    mutates the desired mode.
- `src/bt_pairing_policy.c` — all nine operations remain pure and
  spinlock-protected (BT RX callback-context safe), never holding the
  lock across external work; `snapshot` zeroes output entry storage
  before copying active entries so callers never see stale tail data.
- `src/bt_bap.c` — three mechanical adaptations, no Bluetooth behavior
  redesign:
  1. advertising rebuild now calls `replace_bonds(...)` then explicitly
     selects BONDED_ONLY when count > 0 / OPEN when count == 0, logging
     and propagating any unexpected errno through the existing restart
     error path; the atomic snapshot/filter rebuild is unchanged;
  2. `pairing_complete(bonded=true)` calls `mark_bonded` and then
     explicitly sets BONDED_ONLY even when mark returns `-ENOMEM`
     (preserving the legacy full-inventory behavior), retaining the
     current no-HCI-callback rule and warning;
  3. `bt_bap_pairing_reset()` sets OPEN and clears inventory as two
     explicit calls under the existing `pairing_adv_lock`, preserving
     the operation order and all Bluetooth/disconnect/restart behavior.
- `tests/unit/bt_pairing_policy/` — the direct suite grew 12 → 23 tests
  (still compiles the real production source).
- `tests/test-matrix.json` — API outcomes and transition witnesses
  updated to the nine-operation surface; no retired names remain.
- `docs/testing/coverage-matrix.md` — source row updated (implementation
  commit) and "P3 baseline migration" provenance (coverage commit).

## Behavior contract (all four mode × inventory combinations)

| Desired mode | Inventory | Required behavior | Proof |
|---|---|---|---|
| OPEN | empty | accept any peer | `test_accept_open_accepts_any` |
| OPEN | nonempty | accept any peer; preserve known bonds | `test_accept_open_with_bonds_accepts_any`, `test_replace_nonempty_keeps_open` |
| BONDED_ONLY | empty | reject every peer | `test_accept_bonded_only_empty_rejects_all`, `test_replace_zero_keeps_bonded_only` |
| BONDED_ONLY | nonempty | accept only exact snapshot members | `test_accept_bonded_only_members_only`, `test_replace_nonempty_keeps_bonded_only` |

Inventory enumeration/replacement, marking, and clearing never derive or
mutate mode; mode changes never mutate inventory (`test_set_mode_preserves_inventory`,
`test_mark_preserves_open`, `test_mark_preserves_bonded_only`,
`test_clear_preserves_open_idempotent`, `test_clear_preserves_bonded_only_idempotent`).

## Tests (23 direct, same suite)

`tests/unit/bt_pairing_policy` compiles the production
`src/bt_pairing_policy.c` on native_sim.  Every handoff case is covered:
init OPEN+empty; set-mode idempotent incl. legal empty BONDED_ONLY;
set-mode preserves inventory both directions; invalid mode `-EINVAL`
atomic on populated and fresh policies; replace-zero keeps OPEN; replace-
zero keeps BONDED_ONLY; replace-nonempty keeps OPEN (OPEN with preserved
bonds legal); replace-nonempty keeps BONDED_ONLY with exact entries;
replacement removes old inventory, preserves mode, and leaves snapshot
tail slots zeroed (removed bond no longer accepted); replace overflow
`-ENOMEM` atomic; replace nonzero-count NULL `-EINVAL` atomic on both
fresh and populated policies; pairing-accept OPEN/empty, OPEN/nonempty
(arbitrary peers), BONDED_ONLY/empty (rejects all), BONDED_ONLY/members
(exact match only); mark-new and mark-duplicate preserve OPEN and
preserve BONDED_ONLY; mark overflow preserves both mode and inventory;
mark NULL `-EINVAL` atomic; clear preserves OPEN and preserves
BONDED_ONLY with all storage zeroed; clear idempotent; snapshots expose
coherent mode/count/active entries for every combination with zeroed
tail.  Tail-zero assertions compare against a byte-zero address helper
(no Bluetooth library link in the unit suite).

Focused verification (handoff commands): `west build --no-sysbuild -b
native_sim/native/64 -d /tmp/user-pairing-p3 tests/unit/bt_pairing_policy
-p -t run` → **23 PASS / 0 FAIL**; retired-name grep
(`bt_pairing_policy_set_bonds`/`bt_pairing_policy_request_open`) over
`src` and `tests` → **no matches** (only historical development/archive
docs retain evidence); `scripts/test-coverage.sh --report-only --output
/tmp/user-pairing-p3-cov --clean-output` → population 35,
`bt_pairing_policy.c` 79/79 L, 34/34 B, 9/9 F; `python3
scripts/check-test-matrix.py --repo-root . --coverage-json
/tmp/user-pairing-p3-cov/coverage.json` → **0 errors, 0 notes**; `git
diff --check` clean.

## Coverage migration (same 35 files)

Candidate generated on the clean implementation commit `89304f7` via
`scripts/test-coverage.sh --write-baseline /tmp/p3-baseline-candidate.json`
(`--output /tmp/p3-cov-candidate --clean-output`), inspected, and
committed byte-exact as `tests/coverage-baseline.json` (commit
`bc011d6`).  Tool versions unchanged (gcovr 8.4 / gcov (GCC) 14.3.0).
No new production source file, so the population stays **35**; the only
moving file is `src/bt_pairing_policy.c` (**66/66 → 79/79 lines, 20/20 →
34/34 branches, 8/8 → 9/9 functions**, every function 100% executed;
all error paths and mode×inventory combinations are direct-suite
proven).  Every unchanged file remains byte-identical to its committed
record — zero per-file decreases.  Aggregate: lines 4498/4954 →
4511/4967, branches 1911/2688 → 1925/2702, functions 339/339 → 340/340;
the deltas (+13 L, +14 B, +1 F) equal exactly the policy file's new
record.  Zero-hit enforcement clean: 340/340.  No baseline weakening and
no unexplained denominator migration.  Provenance:
`docs/testing/coverage-matrix.md` "P3 baseline migration".

## Canonical gate (G1)

`./scripts/test-all.sh` on clean `bc011d6` → **57 PASS / 0 FAIL /
57 TOTAL**, exit 0 (33 twister + 5 exec-only + 16 Python + coverage +
matrix + BSim Stage 1; log `/tmp/p3-gate.log`).  Coverage child: baseline
enforcement **0 errors** against the migrated baseline.  Matrix child:
**0 errors, 0 notes**.  BSim Stage 1: all 17 scenarios strict-checked;
every existing pin byte-identical (mono 10 ms `0x22AB5C0D`, mono 7.5 ms
`0x01A3EB05`, Mode A 10 ms `0xBAE24F7E`, Mode A 7.5 ms `0x2D95D15C`,
reconnect fresh mono oracle).  `git diff --check` clean.

## Builds and build contract

- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
  only the documented pre-existing NCS v3.3.0 diagnostics
  (PARTITION_MANAGER deprecation, SW Split experimental symbols incl.
  `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice notice, FLPR-image
  `UART_CONSOLE` assigned-but-got) — zero new/actionable warnings from
  P3 and zero warnings from any changed file.
- Build contract **79/79** (`python3 scripts/check-build-contract.py
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15` → 79 assertions,
  0 failed, BUILD CONTRACT PASSED).
- The three `warning:` lines in the canonical gate log are pre-existing
  in untouched suites (the `audio_stream_session` prj.conf BT buffer
  "assigned value but got ''" pair and the `audio_shell_nrf54`
  fake-fixture enum/int-mismatch) — identical to the P2 gate log, not
  introduced by P3.

## Deviations and notes

- The handoff's tail-zero requirement ("zero unused/tail slots so
  snapshots never retain stale addresses beyond count") is implemented
  in both `replace_bonds` and `clear_bonds` (production storage) and in
  `snapshot` (output storage zeroed before copying active entries).
- `set_mode` and `clear_bonds` results at the three `bt_bap.c` call
  sites: `set_mode` cannot fail for a valid enum value, so the two
  explicit legacy calls discard the result with `(void)`; the
  advertising-rebuild `set_mode` propagates any unexpected errno through
  the existing restart error path exactly as the handoff mandates.
- The direct suite's zero-address helper replaces the Bluetooth-library
  `bt_addr_le_any` extern, which the unit link does not provide.
- No `CONFIG_USER_PAIRING_CONTROL` branches were added to the policy
  (explicit calls at current consumers preserve legacy behavior); no
  P1 Bluetooth operations, callback/lifecycle integration, or shell
  changes (P4/P5); no hardware tests (P8).

## Next-phase grounding

P4 (Bluetooth adapter and callback integration) can consume the explicit
policy surface unchanged: `pairing_mode_ops` selects
`bt_pairing_policy_set_mode()` for NORMAL/BONDING access, preserves
bonds across BONDING entry, calls `clear_bonds()` only after RESET's
successful storage deletion, and the current `bt_bap_pairing_reset()`
legacy path becomes the old transition to be removed.
