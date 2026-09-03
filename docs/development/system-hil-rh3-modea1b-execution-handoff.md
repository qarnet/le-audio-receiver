# RH3-ModeA1b offload-disabled rerun handoff

Status: approved one-run hardware diagnostic. Fix validation for the
RH3-ModeA1 fixture defect (run `rh3-modea1-20260903-offload-disabled` failed
at `receiver active` because the runner lacked the offload-disabled concept;
repaired in commits `f0ae6d8` + `eac880d`). The first attempt stays immutable
as the fixture-defect baseline. This rerun uses a NEW run ID.

## Goal

The same binary isolation as RH3-ModeA1: run fresh Mode A `48_4_1` with the
offload-disabled receiver image (`tests/hil/receiver-offload-disabled.conf`,
CPUAPP `91223b359b49f92eb1e5b7b2a03b025939d74ec40d8dda4598053f6d45347a84`,
FLPR unchanged `45ab8d15...`) using
`hil-runner.py run --allow-offload-disabled`.

- PASS under frozen limits: FLPR offload pipeline implicated in the Mode A
  dual-CIS starvation.
- FAIL under frozen limits (real transport numbers): transport/controller
  path implicated; FLPR cleared as primary suspect.
- Any other boundary: record, classify, stop for review.

## Fixed identity

```text
run ID: rh3-modea1b-20260903-offload-disabled (validated unused, length 37)
row:    rh3.fresh_mode_a_48_4_1
fragment SHA-256: 9d803fa50488b2058480d5888b2d09b2c87c1a2bef570b768f2b689984605b81
diagnostic CPUAPP: 91223b359b49f92eb1e5b7b2a03b025939d74ec40d8dda4598053f6d45347a84
```

Note: the CURRENT normal local CPUAPP identity is
`7dabfdb2571e1d6f1d9e1fe417d096d12485e32ec9dbe154b9d0f7384e2f20a9` (the
committed tree's normal build; the old `e67265c1...` pin predates the HIL
landing commit `3755d92`). Restoration must prove `7dabfdb2...` and
`CONFIG_AUDIO_OFFLOAD_ASRC=y`. FLPR and source identities are unchanged.

## Ordered procedure

1. Disk gate + `git diff --check` + `git status` clean.
2. Output-path ownership checks for run dir + external JUnit.
3. Fixture validate; source-image hash check
   (`f0e1c5ab...`, `4e4b82f5...`).
4. Rebuild the diagnostic image
   (`fw-build-54l15 -DEXTRA_CONF_FILE=$PWD/tests/hil/receiver-offload-disabled.conf`),
   prove the config lines and the exact diagnostic hashes above. The
   `git status` at preflight must be run BEFORE this build (the build output
   is not repo state).
5. Single run with the new flag, outer tool timeout 3600000 ms:

```bash
set +e
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modea1b-20260903-offload-disabled \
  --junit /tmp/opencode/hil-runs/rh3-modea1b-20260903-offload-disabled.junit.xml \
  --row rh3.fresh_mode_a_48_4_1 \
  --allow-offload-disabled
status=$?
set -e
printf 'status=%s\n' "$status"
```

6. Read-only integrity review; extract per-slot summaries, limits verdict,
   FLPR handshake + offload snapshots (`submit=0 success=0` expected in both
   active and post-stop), ISO link-quality tail.
7. Restore normal build; prove `7dabfdb2...` CPUAPP, FLPR/source unchanged,
   `CONFIG_AUDIO_OFFLOAD_ASRC=y`; disk gate.
8. Write canonical result `docs/development/system-hil-rh3-modea1b-result.md`
   (scope, evidence, hashes, outcome, per-slot values, binary classification
   with the deciding evidence, interpretation limits, restoration, stop
   point). Update `system-hil-resume-state.md` run count and last-flash
   identity (diagnostic CPUAPP `91223b35...`, or the exact flashed images).
   Also correct any remaining stale `e67265c1...` normal-image pins in the
   resume state to `7dabfdb2...` where they describe CURRENT local normal
   output (do not rewrite historical records' original observations).
9. Commit exactly:

```text
docs(hil): record Mode A offload-disabled rerun outcome
```

## Constraints

Treat actionable build warnings as errors (allowed: documented dirty-tree
notice, nRF54L15 watchdog empty-library, global `__ASSERT()`). No production
changes, no retry, no evidence mutation, no manual target operations. The
runner owns all hardware. Escalate on: unexpected boundary failure,
classification ambiguity, hash/config mismatch, integrity failure.
