# nRF54L15 clean-checkpoint results, 2026-09-25

## Scope and source

Clean validation ran from `/tmp/opencode/nrf54-validation-21ff2f0/le-audio-receiver`,
not the former migration worktree. Coverage manifest:
`/tmp/opencode/nrf54-21ff2f0-clean-coverage-r1/run-manifest.json`.
It records source commit `21ff2f0fd1dc4e16071dfbc0cad9359d33885018`,
`dirty: false`, `write-baseline` mode, 46 successful C suites, GCC/gcov
14.3.0 and gcovr 8.4. The candidate is
`/tmp/opencode/nrf54-21ff2f0-coverage-candidate.json`.

The clean-checkpoint unit gate recorded 74 PASS / 1 FAIL. The failure was
`tests/unit/hci_attach/test_hci_attach.py` using hardcoded `/tmp/opencode` as
the CLI output root; that path contains the clean checkout, so it is not a
valid external output root. The test now passes `str(self.out)`, the fixture's
independent temporary output root. The expected malformed-manifest diagnostic
and production root guard remain unchanged. Focused rerun:
`nix develop -c python3 tests/unit/hci_attach/test_hci_attach.py`, 9 tests OK.
This does not turn the preceding 74/1 gate into a clean full-gate pass.

Full HIL suite recorded 340 passed and one intentional hardware skip. This
is the existing software checkpoint, not a new physical test or proof of HCI
streaming qualification. The HCI production failure remains unresolved.

## Coverage baseline accounting

The committed baseline before adoption contained 37 production files:
4962/5420 lines, 2153/2960 branches, 380/380 functions. Its sole removed
production entry, `src/audio_clock_actuator_apll.c`, contributed 15/15 lines,
4/4 branches and 3/3 functions. Historical APLL behavior remains covered by
the test-local copy with eight APLL tests and one no-HFCLK test; that copy is
not a production numeric-population entry. No files were added, no exclusions
were added, and no threshold was reduced.

The clean candidate contains 36 production files: 4971/5427 lines,
2203/3008 branches, 377/377 functions. Of 36 retained files, 34 have
unchanged numeric entries. Two improved:

| File | Old lines | New lines | Old branches | New branches |
| --- | --- | --- | --- | --- |
| `src/audio_modea.c` | 109/115 | 124/128 | 61/76 | 89/102 |
| `src/audio_stream_session.c` | 323/339 | 332/348 | 159/222 | 185/248 |

`tests/coverage-baseline.json` adopts exact candidate JSON bytes, including
its updated `src/main.c` exclusion rationale and generated-commit provenance.
The historical numbers in other records remain historical; baseline adoption
does not rewrite them. This clean source checkpoint and baseline refresh do
not complete PB-037, the full migration, canonical clean-gate acceptance,
HCI qualification, exact-artifact acceptance or physical audio proof. Rerun
full gate on a clean commit and resolve the HCI production failure before
claiming those outcomes.
