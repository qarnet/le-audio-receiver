# Phase 6 Stage 5 — measured optimization and Phase 6 close

## Decisions

- Keep ICMsg over VEVIF. Measured production max: FLPR ASRC 1.052 ms, RTT
  2.148 ms against 8 ms deadline (5.852 ms headroom). Raw VEVIF complexity has
  no measured need.
- Do not adopt experimental HPF. No SRAM/contention/deadline evidence justifies
  it.
- Keep four-slot 8 KiB rings and 8 ms deadline; both have proven fault margin.
- Remove dead Stage 2 `audio_offload_submit()` identity API and nRF54
  `g_scratch_output[960]` (1,920 B). Live production uses ASRC API only.

## Mechanical cleanup

- Delete identity submit declaration, nRF54 implementation, nRF5340 stub, old
  scratch buffer/asserts, and identity-only offload comments/counters/tests.
- Migrate shared lifecycle/recovery unit coverage that called identity submit to
  `audio_offload_process_asrc()` mocks. Preserve Stage 2 identity validation in
  `flpr_audio_process` and ring suites; do not remove identity ring mode/test
  commands.
- Update module/header docs from “identity loopback” to current FLPR ASRC with
  cpuapp ASRC fallback.
- Remove stale SAMPLE_ADJUST/current-path statements from AGENTS/STATUS/design
  only where contradicted by accepted implementation. Preserve history docs.
- Mark Phase 6 Stages 0–5 complete in `docs/design.md` and `STATUS.md`, citing
  Stage 3/4 acceptance files. State actuator set remains APLL/NONE.

## Verification

- Full unit gate: ASRC, processor, ring, ring manager, protocol, offload ASRC,
  runtime, handshake, decode/lifecycle.
- Both pristine builds. Record nRF54 CPUAPP flash/RAM before and after; require
  scratch symbol absent and at least ~1,920 B static RAM recovery unless linker
  accounting proves different. nRF5340 behavior/build unchanged.
- FLPR no FPU/audio heap allocation.
- nRF54 production Mode A 120 s + true Mode B 120 s, 100 fps, zero faults.
- Mode A 180 s with one hang, recovery/probation gates remain clean.
- Run `git diff --check`; reconcile docs against executable commands/config.

Commit code/tests/docs/results. No raw VEVIF, HPF, BabbleSim yet, security
changes, push, release, mass erase, install, or analog claim.
