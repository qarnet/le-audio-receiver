# Phase 1 handoff — PACS availability semantics ✅ EXECUTED (2026-07-30)

## Goal

Make receiver truthful and usable during stock BlueZ/WirePlumber discovery by
keeping advertised sink contexts available after ACL connection.

## In scope

- Remove connection-time transition of Sink Available Audio Contexts to
  `BT_AUDIO_CONTEXT_TYPE_NONE` in `src/bt_bap.c`.
- Remove now-redundant disconnect-time restoration and stale ownership comments.
- Preserve initial supported/available context registration before advertising.
- Add focused regression coverage that fails if ACL connection again clears all
  contexts. Prefer behavior-level test through existing BSim client/PACS
  discovery when NCS API exposes contexts; otherwise add smallest testable
  helper or explicit source-level assertion script. Do not create large
  abstraction for one call.
- Update `STATUS.md`, `docs/design.md`, and BlueZ interoperability plan with
  corrected context lifecycle and reopened desktop acceptance state.
- Keep CAP/CAS disabled. Keep advertising payload, LC3 capability, pairing,
  QoS, ASRC, FLPR, and custom lab harness behavior unchanged.

## Out of scope

- Host WirePlumber rules or config changes.
- Raw-HCI/custom endpoint changes.
- Flashing or hardware connection testing; Phase 2 owns that after review.
- New codecs/services, CAP/CAS, pairing reset, or bond deletion.

## Current defect

`src/bt_bap.c:640-655` sets available sink contexts to `NONE` immediately after
ACL connect, before stock desktop policy reads PACS. `src/bt_bap.c:694-695`
restores contexts only on disconnect. ACL existence is not ASE ownership.

## Invariants

- Media remains in `AVAILABLE_SINK_CONTEXT` during connection and stream setup.
- PACS/ASCS remain encrypted standard services.
- nRF5340 APLL and nRF54L15 FLPR/CPUAPP ASRC paths unchanged.
- No warnings normalized or ignored.
- No test-only counters/APIs enter production code.

## Verification

Run:

```bash
bash scripts/test-all.sh
fw-build-5340
fw-build-54l15
git diff --check
```

Inspect build logs for actionable warnings. Existing documented NCS/DT
diagnostics are allowed only with recorded reason.

## Delivery

Inspect status/diff/log, stage only scoped files, and commit completed Phase 1.
No push, merge, PR creation/update, hardware reset, flashing, mass erase, or
package changes. Return files, behavior, tests, exact commit, warnings,
deviations, and blockers.
