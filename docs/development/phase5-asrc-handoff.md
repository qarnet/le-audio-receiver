# Phase 5 — cpuapp fixed-point ASRC handoff

## Goal

Replace nRF54L15 production nearest-neighbor + SAMPLE_ADJUST correction with a
continuous, stateful, cross-block fixed-point stereo ASRC on cpuapp. Preserve
nRF5340 APLL behavior exactly. Keep SAMPLE_ADJUST temporarily selectable for
A/B comparison until hardware acceptance.

## Required design

- Pure C `audio_asrc` module. Stereo signed-16 input/output, no heap, no float.
- Continuous source phase and previous-frame history across calls. Linear
  interpolation uses common phase for L/R and 64-bit intermediate arithmetic.
- Define `source_step = input_rate / physical_output_rate *
  (1 + correction_ppm / 1000000)`. Positive correction consumes source faster;
  negative consumes slower. Existing controller sign unchanged.
- Use Q32.32 unless implementation proves equivalent format better. Derive step
  with overflow-safe 64-bit arithmetic and explicit ppm/rate validation.
- API must report input consumed and output produced or otherwise prove no input
  loss/duplication at arbitrary chunk boundaries. Output-capacity failure must
  be explicit and leave canaries/state safe.
- nRF54 ASRC replaces both block-local fixed nearest conversion and discrete
  SAMPLE_ADJUST. nRF5340 remains identity conversion + APLL.
- ASRC consumes controller ppm through explicit data-path API. Do not label ASRC
  a physical clock actuator. Keep temporary legacy SAMPLE_ADJUST build option.
- Honor `sample_count` passed to `audio_sink_push`; reject odd/invalid counts.
- Existing slab currently holds 481 stereo frames. Prove compile-time and
  runtime capacity for 480 input frames, 48k→47,619 base rate, configured
  ±2,000 ppm. Increase block capacity only with exact static-RAM impact and both
  builds passing.
- Silence prefill uses separate physical-output frame remainder and never
  advances ASRC source phase/history.
- Reset ASRC and prefill scheduler on stop/disconnect/reconnect.
- Add ASRC path to performance metrics: count/total/max/deadline, output frame
  range, capacity failures/clipping if applicable.

## Tests

Add `tests/unit/asrc/` with deterministic executable tests:

1. invalid rates/pointers/counts/ppm;
2. identity deterministic behavior and reset;
3. 48k→47,619 long-run output total within one frame of ideal;
4. +2,000 ppm produces fewer frames, -2,000 produces more;
5. measured local-fast sign chain leads to negative correction and more output;
6. phase/history continuity across 480-frame and irregular chunks;
7. chunking invariance;
8. boundary interpolation;
9. stereo isolation/common phase;
10. constant/ramp/full-scale/alternating/impulse cases;
11. abrupt ppm changes preserve continuity;
12. capacity error does not overwrite canaries or corrupt state;
13. configured worst-case fits production capacity;
14. deterministic 60,000-block run;
15. compare output against host/high-precision reference with explicit numeric
    tolerance and no subjective claims.

Keep rate-converter and SAMPLE_ADJUST tests while comparison path exists. Run
all existing unit suites plus new ASRC/perf suite.

## Integration and configuration

- Add Kconfig resampler choice independent from physical actuator.
- nRF5340 default: identity/fixed path + APLL; no performance or behavior
  regression.
- nRF54L15 production default: linear ASRC data path; APLL unavailable; legacy
  SAMPLE_ADJUST remains selectable only through explicit comparison overlay.
- CMake compiles only selected modules.
- Update shell/status to report active resampler, correction ppm, ASRC frames,
  capacity failures, and perf.
- Build a dedicated test-only one-ASE stereo configuration for true Mode B
  without changing normal two-ASE Mode A product capability. Verify PACS codec
  capability and central negotiation from actual logs.

## Verification before commit

- All unit binaries execute and pass.
- `fw-build-5340`, `fw-build-54l15` pass; no new warnings.
- Inspect resolved configs and static RAM/flash deltas.
- Hardware after software review: nRF54 Mode A 10-minute and true Mode B
  10-minute autonomous central streams, zero faults/underruns/repeats/capacity
  failures, callback max below 10 ms with useful margin, slab bounded.
- nRF5340 build required; hardware stream if board connected. Never substitute
  user-operated central.
- External analyzer optional; never emit binary sigrok data through tool output.

## Out of scope

- No FLPR, BabbleSim, package installation, direct RADIO access, release/push,
  destructive recovery, or analog-quality claim.

## Return contract

Commit implementation + handoff after software gates. Return files/API,
algorithm invariants, exact test/build/hardware results, memory deltas, commit
hash/message, warnings, blockers, and deviations. Do not push or amend.
