# Phase T3 review-fix handoff

## Goal

Remove re-initialization regression introduced by T3 state clearing. Preserve
active DMA ownership on accidental repeated `audio_sink_init()`.

## Scope

- `src/audio_i2s.c`
- `src/audio_sink.h`
- both T3 suites/shared tests
- `docs/testing/t3-audio-i2s-tests.md`
- `docs/testing/behavior-contract.md`
- `STATUS.md`
- this handoff

No T4, hardware, push, merge, PR, or amend.

## Required behavior

At entry to `audio_sink_init()`:

- if `configured == true`, return 0 immediately;
- do not change `started`, saved frame, input frame selection, ASRC state,
  offload sequence, slab ownership, I2S queue, or dependency state;
- do not call device-ready, configure, ASRC init, actuator init, or timing init;
- preserve every queued driver-owned block.

If not configured, initialize from a clean not-started/saved state as before.
Any first-attempt failure leaves configured false and must permit a later retry;
successful retry performs the full normal init exactly once.

Do not call DROP/PREPARE from idempotent init. Stream control remains
`audio_sink_stop()` responsibility.

## Tests

Both variants:

- init success then immediate second init: return 0, no additional calls;
- init, start DMA, capture queued pointers/count/free slab, second init: return
  0, started remains true, exact queue/pointers/free count unchanged, no trigger
  or dependency call added;
- configure failure then retry success;
- ASRC-init/actuator/timing failure then retry success where applicable;
- repeat init does not reset input frame selection (set 360 first).

Replace any test/evidence that expected re-init failure to clear an already
configured sink. That behavior was unsafe and was never part of public contract.

Correct `docs/testing/t3-audio-i2s-tests.md` line claiming tests compile
“unmodified” `audio_i2s.c`: say they compile and execute the real current
production source, including T3 hardening.

## Verification

```bash
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t3_fix_asrc tests/unit/audio_i2s -p -t run
west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/t3_fix_identity tests/unit/audio_i2s_identity -p -t run
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

Validate exact final commit on workstation: two consecutive serial full gates,
all builds, unchanged BSim hashes. Remove temp artifacts.

## Commit

```text
fix: preserve active I2S state across reinit
```

Return counts, queue/slab preservation proof, gates, builds, hash, cleanup, and
blockers.
