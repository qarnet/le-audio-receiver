# R10 handoff — final integration and documentation closeout

Start commit: `4da2df1` (R9 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before execution; the results document
(`docs/development/refactor-r10-results.md`) must record what actually
happened and become the final authoritative evidence.

## Goal / invariant

Prove the R0–R10 refactor track preserved every accepted behavior of the
T0–T8 baseline and make the resulting architecture, inventory, and
evidence the new authoritative baseline.  R10 is **docs/evidence only**:

- **No production or test behavior change.**
- **No coverage baseline regeneration** (population stays 33; committed
  baseline `54a6b8e` stays byte-identical; tool versions gcovr 8.4 /
  gcov (GCC) 14.3.0).
- **No BSim re-pin** (17 scenarios / 26 runs; scenarios 1–9 twice,
  10–17 once; all pins byte-identical).

Firmware identity: the behavioral baseline is the T8 production code
commit `971e6a4` — not an identical source tree; R0–R9 made structural
production-source changes while preserving behavior (source refactor +
test/documentation), so the flashed code tree's behavior equals
`971e6a4`'s but its source differs.  R10 adds docs-only commits;
builds/flashes use the code tree of the doc-cleanup
commit and the final docs commit.  R8/R9 hardware evidence is
corroboration; R10 requires **fresh full rows** below plus the final G1.

## Doc/architecture truth (update at minimum)

1. **AGENTS.md** — status through R9/R10; canonical gate **55 children**
   = 31 twister + 5 exec + 16 Python + coverage + matrix + BSim;
   coverage population **33** totals 4024/4402 L, 1695/2356 B, 289/289 F;
   build contract **79/79**; BSim 17 scenarios / 26 runs; key files add
   R4/R6/R7/R8/R9 modules (`bt_shell.c`, `flpr_shell.c`,
   `flpr_acceptance_shell.c`, `audio_stream_session.c`,
   `flpr_acceptance.c`, `flpr_control_ack.c`, `src/flpr/acceptance.c`,
   `bap_central_device/security/endpoint/session.py`) and
   `audio_sink` stream_open/close.
2. **README.md** — same inventory/BSim (17 scenarios/26 runs, 31/5/16/55
   children), current `bt_bap`/session/teardown/central module blurbs.
3. **STATUS.md** — current final state with R10 accepted; label stale
   Phase 5/6 "Next actions" tables historical instead of presenting them
   as current.
4. **docs/design.md** — label old T8 claims historical; add current
   R0–R10 architecture summary/diagram/key-ownership section; correct the
   historical nRF5340 timing claim (ISO-ts-based frequency estimation was
   removed; current production is `audio_timing_none.c` no-op with no
   frequency update — phase-only PI from I2S buffer fill) with an
   annotation.
5. **docs/testing/behavior-contract.md** — current baseline provenance:
   migrated through R4/R6/R8; current committed baseline **`54a6b8e` /
   population 33** / totals 4024/4402 L, 1695/2356 B, 289/289 F / gcovr
   8.4 + gcov (GCC) 14.3.0.  Ownership clarifications only; never weaken
   an outcome.
6. **docs/testing/coverage-matrix.md** — remove stale "current 47/26"
   language; current **55 children / 33 files**; retain T8 historical
   attribution.
7. **scripts/bsim-stage1-run.sh** — header comment: 17 scenarios,
   scenarios 1–9 twice, 10–17 once.
8. **docs/testing/pre-refactor-hardware-baseline.md** — remains
   historical; add one header annotation only (never rewrite evidence).
9. **docs/development/refactor-plan.md** — mark R10 ACCEPTED and the
   whole track **COMPLETE** only after all criteria pass.
10. **docs/development/workstation-transfer-status.md** — update to
    COMPLETE after pass.
11. New **docs/development/refactor-r10-results.md** — final
    authoritative evidence.

## Archive (zero active links — verified 2026-08-06 via repo-wide
grep, `git mv` preserves history)

Re-run the reference search immediately before moving.  Move into
`docs/development/archive/`.  Do NOT move: behavior contracts, coverage
matrix, pre-refactor hardware baseline, refactor plan/results, or any
linked evidence.  Minimum safe zero-link set (each listed file has zero
references outside itself):

**Pre-refactor T-track handoffs (superseded by accepted phase results):**
`pre-refactor-testing-t1-review-fix-handoff.md`,
`pre-refactor-testing-t2-handoff.md`,
`pre-refactor-testing-t2-review-fix-handoff.md`,
`pre-refactor-testing-t3-handoff.md`,
`pre-refactor-testing-t3-review-fix-handoff.md`,
`pre-refactor-testing-t4-handoff.md`,
`pre-refactor-testing-t4-review-fix-handoff.md`,
`pre-refactor-testing-t8-handoff.md`.

**BZ desktop-track handoffs (superseded by accepted BZ1–BZ4 results):**
`bluez-wireplumber-phase2-handoff.md`,
`bluez-wireplumber-phase2-diagnostic-handoff.md`,
`bluez-wireplumber-phase2-final-review-handoff.md`,
`bluez-wireplumber-phase2-frame-duration-fix-handoff.md`,
`bluez-wireplumber-phase2-systemwide-handoff.md`,
`bluez-wireplumber-phase3-handoff.md`,
`bluez-wireplumber-phase3-spa-proof-fix-handoff.md`,
`bluez-wireplumber-phase3-strict-host-fix-handoff.md`,
`pairing-mode-filter-handoff.md`.

**Refactor handoffs R0–R2 (accepted phases; plan-of-record inline
acceptance + results docs are the authority):**
`refactor-r0-handoff.md`, `refactor-r1-handoff.md`,
`refactor-r2-handoff.md`.

**Phase 6 intermediate stage results (superseded by
`phase6-stage5-results.md`, the consolidated Phase 6 authority):**
`phase6-stage2-results.md`, `phase6-stage3a-results.md`,
`phase6-stage3b-results.md`,
`phase6-stage3-hardware-acceptance-results.md`,
`phase6-stage4a-reset-order-results.md`.

Files checked and KEPT because they have active links: all refactor
results docs; refactor handoffs R3–R9 (linked from their results docs);
`pre-refactor-testing-plan.md`; `pre-refactor-testing-t0/t1/t5/t5-review/
t6/t6-review/t7-stage1/t7-stage2/t7-evidence-fix-handoff.md`;
`phase2-stock-desktop-gate-results.md`; `phase3-results.md`;
`bluez-wireplumber-interoperability-plan.md`;
`bluez-wireplumber-phase1-handoff.md`;
`bluez-wireplumber-phase2-duration-fault-fix-handoff.md`;
`bluez-wireplumber-phase2-review-cleanup-handoff.md`;
`bluez-wireplumber-phase2-strict-evidence-handoff.md`;
`bluez-wireplumber-phase3-final-review-handoff.md`;
`fw-flash-dongle-probe-fix-handoff.md`; `xiao-rf-switch-fix-handoff.md`
(linked from `v0.0.1-baseline.md`); `bsim-stage0/1-results.md`;
`workstation-transfer-status.md`; all phase 4/5 results docs; phase 6
stage 0/1/4a-runtime/4b/5 results docs.

If any kept doc is found to link an archived file after the final
reference re-run, fix the link in the same cleanup commit (never move
linked evidence).

## Final G1 — canonical, on the clean exact commit

Run from a clean commit after the doc/architecture cleanup commit:

```bash
time ./scripts/test-all.sh > /tmp/r10-gate1.log 2>&1
# => 55 PASS / 0 FAIL / 55 TOTAL; coverage pop 33 baseline 0 errors;
# matrix 0 errors; BSim 17 scenarios/26 runs, existing pins byte-identical
./scripts/test-coverage.sh --output /tmp/r10-coverage --clean-output
# explicit baseline enforcement: 0 errors
fw-build-5340 && fw-build-54l15 && fw-build-dongle
# warnings scanned; only the documented NCS v3.3.0 diagnostics allowed
python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
# => 79/79
git diff --check
```

If the final results docs are committed after this gate, re-run the
canonical `test-all.sh` on the exact final commit (R10 requires a
final-commit G1).  Builds need not re-run for a docs-only commit if the
first gate's builds are clearly recorded with the code/tree identity that
the docs-only commit does not change — but the canonical gate MUST re-run.

## Full R10 hardware — nRF54L15 (planned repeated flash/reset authorized;
no destructive recovery / mass erase)

Evidence: `/tmp/r10-hw/` with `MANIFEST.md` + `SHA256SUMS`.  Capture
console BEFORE every reset (`scripts/read_acm.py`).  Dynamic
`nrf-probes`; record raw DPIDR / AP IDR map / FICR PART / VARIANT
evidence.  Verify hci0 dongle address `C0:AA:BB:CC:DD:EE` and settings
`powered le secure-conn cis-central`.  Reflash both receivers and the
dongle as needed with repo tools only.  Production configs only.  No
audibility claim.

| Row | Command | Expected receiver counters |
|-----|---------|----------------------------|
| L1 fresh Mode A 120 s | `bap_central.py --duration 120` after `bt unpair` | ~12000 central frames @100 fps; decode_err/i2s_underrun/stream_reset=0; offload submit==success, fallback=0, faults 0 |
| L2 fresh Mode B 120 s | `bap_central.py --stereo --duration 120` after `bt unpair` | same; stereo_b single-ASE 240-byte SDU |
| L3 bonded reconnect Mode A 120 s | `bap_central.py --peer-addr --preserve-bond --duration 120` | same |
| L4 FLPR hang Mode A 180 s | `flpr_hang_gate.py --duration 180` | 16/16 checks; ACK; one recovery; epoch change; probation cleared; faults 0 |
| L5 FLPR hang Mode B 180 s | `flpr_hang_gate.py --duration 180 --stereo` | 16/16; same |
| L6 BZ3 full | `bluez-wireplumber-phase3-gate.py --receiver "LE Audio Receiver" --serial /dev/ttyACM0 --duration 30 --log-dir /tmp/r10-hw/bz3 --stage full` | 3/3 fresh/bonded/third playback; zero faults.  Reset attachment/service state exactly per its docs; desktop BZ3 owns the controller — detach the custom central between modes |
| L7 pairing reset + BONDED_ONLY | `bt unpair` on receiver console (prove log "bonds cleared; open pairing enabled"); fresh OPEN pair; then preserve-bond reconnect with FAL active | pairing filter state/log; BONDED_ONLY FAL active; unbonded peer rejected.  If no second physical peer, prove filter state/log with existing central/dongle identity and honestly record the inability to generate an external attempt (best-available public evidence) |
| L8 stall gate + diagnostics | `flpr ring stall_flpr_ms ...` via `flpr_stall_gate.py`; then `flpr status`, `flpr ring status`, `flpr stress 5`, `flpr ring test 50` | all pass |
| L9 clean boot | reset + capture boot log | required lines (`BLE ready`, `settings_load() OK`, `Advertising as "LE Audio Receiver"`); zero actionable warnings |

## Full R10 hardware — E83 (nRF5340)

| Row | Command | Expected receiver evidence |
|-----|---------|----------------------------|
| E1 fresh discovery Mode A 120 s | `bap_central.py --duration 120` (discovery, no `--peer-addr`) after `bt unpair` | ~12000 frames @100 fps; zero ISO gap / i2s warning / decode / underrun / reset |
| E2 fresh Mode B 120 s | `bap_central.py --stereo --duration 120` | same |
| E3 bonded Mode B 120 s | `bap_central.py --stereo --peer-addr --preserve-bond --duration 120` | same |
| E4 bonded Mode B 60 s APLL | `bap_central.py --stereo --peer-addr --preserve-bond --duration 60` + midstream `audio status` | Drift ACTIVE ppm −500, identity resampler, zero errors |
| E5 all-log zero scan | full session logs | zero `ISO seq gap` / discontinuity / `i2s_nrfx: Next buffers` / `Cannot write` / decode / underrun / reset lines |

If RF degradation recurs, use the already-proven safe ritual: reflash/
reset dongle + E83, clean btattach/BlueZ, fresh bonds, wait for a clean
window.  Do not weaken rows; report a blocker only after exhaustive safe
retries.

## Pairing / BZ3 safety

No security-policy change.  Use `bt unpair` (receiver console) /
`bluetoothctl remove` (host) for fresh rows; preserve bonds for bonded
rows.  Never mass erase.  Custom central and desktop BZ3 must not
contend: cleanly detach/reconfigure between modes.

## Evidence matrix — reusable vs fresh

Cite R8/R9 evidence as corroboration; R10 requires the fresh rows above
and the final G1.  Current code is unchanged since R9; docs-only commits
preserve firmware identity.  Record the exact commit/code tree used for
builds/flashes and the final docs commit relation.

## Verification order

1. Commit this handoff.
2. Doc/architecture cleanup + archive (single logical commit; exact
   moved list in the commit message body and in the results doc).
3. Final G1 on the clean cleanup commit (canonical + coverage
   enforcement + builds 3/3 + build contract 79/79 + `git diff --check`).
4. Full R10 hardware matrix (L1–L9, E1–E5, BZ3, pairing).
5. Write `refactor-r10-results.md`; update plan (R10 ACCEPTED, track
   COMPLETE), STATUS, AGENTS, README, workstation transfer; commit
   acceptance.
6. Re-run canonical `test-all.sh` on the exact final commit.
7. No push/PR/amend/force/attribution.

## Commits (logical, new commits only — no amend)

- **Handoff**: this document.
- **Docs/archive architecture cleanup**: AGENTS/README/STATUS/design/
  behavior-contract/coverage-matrix/bsim-header/pre-refactor-baseline-
  annotation + archive moves.
- **Final acceptance/results**: results doc + plan/STATUS/AGENTS/README/
  workstation-transfer COMPLETE marks.

If evidence corrections are needed, land them as new commits.

## Non-scope

Source/test behavior changes; coverage baseline refresh; BSim re-pin;
360-frame FLPR offload; mixed-duration feature; security-policy
tightening; release/version bump; push/PR; destructive hardware actions
(mass erase / recovery).

## Escalation

Escalate only a genuine blocker: a complete hardware row unavailable
after safe diagnosis/retries; BZ3 environment unavailable; an unexpected
final G1/build/hash/coverage failure; a docs conflict requiring behavior
change; a destructive action needed.  Preserve state/evidence and ask one
precise question; never mark complete with a partial matrix.
