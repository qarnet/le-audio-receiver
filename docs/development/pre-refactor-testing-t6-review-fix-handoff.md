# Phase T6 review-fix handoff

T6 remains open after `411f6f2`.

## Required fixes

1. `scripts/flpr_hang_gate.py` defines `RE_RUNTIME_RESTART_OK` without the
   literal `epoch` emitted by production `src/audio_shell.c`:

   ```text
   FLPR restart OK: epoch <old>→<new> crc=... duration=total ... ms
   ```

   Correct the parser to match exact production output and preserve the four
   captured numeric groups. Add a stdlib unit suite dedicated to this gate
   parser (or an equally focused existing parser suite) proving:

   - exact production line matches and extracts old epoch, new epoch, CRC, and
     duration;
   - unrelated/partial/old drifted forms do not produce a false recovery
     observation;
   - representative surrounding console text still matches.

   Add the suite to `scripts/test-all.sh`; update documented counts. Do not
   weaken production output or accept both stale and current formats merely to
   hide drift.

2. Add direct `#include <stdbool.h>` to `src/app_lifecycle.c`; do not rely on
   transitive Zephyr headers for `bool`.

3. Correct T6 evidence. A desktop gate with missing BSim binaries is useful
   development evidence but is not acceptance. Run the complete canonical gate
   on an exact final code commit in the workstation detached-worktree flow. It
   must report every child PASS, including `bsim: stage1`. Then run all three
   pristine builds and the 74-assertion resolved checker on that same code
   commit. Update STATUS/behavior/coverage evidence with exact counts and commit
   hash; remove the premature 34/35 acceptance claim or label it development
   only.

## Verification

```bash
python3 tests/unit/flpr_hang_gate/test_flpr_hang_gate.py
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
git status --short
```

Commit code/tests/handoff, validate exact code commit, then commit corrected
evidence docs. Leave both repositories clean and remove temporary artifacts.
No push, merge, PR, amend, force-push, hardware, attribution, warning
suppression, skipped BSim, or partial acceptance.

## Escalation

If workstation BSim or any checker assertion fails twice through materially
different diagnosis attempts, stop and report exact logs/evidence/question.
Do not call environment failure acceptable, weaken regex/assertions, or keep
guessing.
