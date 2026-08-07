# P7 handoff — software and build acceptance

Base commit: `c6b338b` (P6 accepted plus review fix; worktree clean).

Close P7 from `docs/development/user-pairing-control-plan.md`. No feature code,
API, DT, Kconfig, or baseline change is expected. Independently rerun and record
the complete software/build acceptance on exact production code now that P1–P6
are integrated and nRF54L15 is feature-enabled.

## Required evidence

1. Focused direct suites, real production sources:
   - pairing_mode: 37/37;
   - user_pairing_io: 21/21;
   - bt_pairing_policy: 23/23;
   - bt_bap_pairing_adapter: 37/37;
   - bt_shell_pairing: 5/5;
   - app_lifecycle: 13/13;
   - build-contract Python: 51 tests.
2. Explicit coverage report-only and baseline enforcement:
   - population 36;
   - every production function in numeric population hit;
   - zero unchanged-file decrease and zero baseline weakening.
3. Matrix checker: zero errors/notes.
4. Canonical `./scripts/test-all.sh`: 59/59; record exact child composition and
   complete BSim Stage 1 pins/scenario count.
5. Fresh `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` from exact commit.
6. Build contract: 95/95 against fresh artifacts.
7. nRF54 resolved config/DT/map checks:
   - full pairing stack and INPUT enabled;
   - exact P0.00/P2.00 aliases/flags/debounce;
   - inherited controls removed/disabled;
   - RAM used/free and heap/workqueue config;
   - expected full-stack symbols linked.
8. nRF5340 feature-off resolved proof.
9. Warning diff/classification: zero new/actionable warnings under repository
   policy; preserve exact raw diagnostics.

## Scope

Allowed edits: P7 handoff/results, STATUS, and evidence-only active docs when a
verified count is stale. Do not edit production code/config/DT, test logic,
baseline JSON, or expected BSim pins merely to pass acceptance. Any failure or
drift keeps P7 open and must be escalated/fixed through a separate reviewed
handoff.

No hardware, flash, serial, reset, push, PR, amend, force-push, destructive
operation, or attribution footer.

## Commit shape

1. P7 handoff.
2. P7 results/acceptance docs after all evidence passes.
