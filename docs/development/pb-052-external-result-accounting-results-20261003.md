# PB-052: strict external report accounting

## Implemented boundary

`scripts/check-external-test-results.py` accepts only complete, hash-bound,
required-case-consistent reports from its pinned BlueZ tester and pytest xunit2
profiles. The public CLI does not run tests or privileged helpers. It validates
caller-reported execution outcomes and provenance, not authenticity/freshness.
PB-053 owns actual isolated execution, discovery and evidence sealing.

The schema and trust boundary are specified in
`docs/testing/external-test-result-accounting.md`. Exact IDs and independently
trusted inventory hash prevent count-only coverage or simultaneous narrowing of
submitted inventory/run record. Excluded cases are explicitly unselected and
absent, never a skipped-required-case waiver. Normal child/prerequisite exit 0,
exact prerequisite identity set, discovered universe, producer pin and raw report
identity are mandatory. Invalid/duplicate/unknown inputs, unsafe XML, hidden
nested outcomes, Not Run, skip, failure, error, timeout and count disagreement
cannot produce an accepted verdict.

No external production suite is invented, no upstream GPL fixtures/source copied,
and no BlueZ/firmware/codec/physical acceptance is claimed from parser fixtures.
Frozen HIL/PCM limits, canonical BSim 17/26 and coverage baseline are unchanged.
Owner's Windows laptop setup holds PB-042 reference execution but not this item.

## Public-boundary tests and review

The new automatically discovered Python child is
`tests/unit/external_test_results/test_external_test_results.py`. It invokes the
real CLI with actual file bytes and checks output/exit/provenance. Complete
authored reports cover ANSI BlueZ rows, long names and status words inside names,
structured/escaped pytest identities and declared unselected exclusions.

Controls cover zero/missing/duplicate/unreviewed execution, failed/Not Run/skipped
outcomes, count/format errors, independent inventory shrink, mismatched hashes,
failed/missing/duplicate/skipped prerequisites, failed/timed-out/cancelled/signaled/
spawn-failed children, invalid schemas and unsafe XML.

Actual pytest 8.4.2 emits reports for pass, failure, skip, xfail, teardown error,
double failure, collection error and empty selection. Unhappy XML is also tested
against a falsely reported child exit 0, so child failure does not mock away the
parser boundary.

Read-only review exposed an honest-evidence false acceptance: `--runxfail` used
as a directory path after the pytest `--` separator did not enable the option;
an actual non-strict XPASS emitted ordinary pass XML and exit 0. The CLI now
requires an explicit supported launcher and the option before the separator.
The real directory/XPASS negative and effective-option positive tests prove this
repair. XML controls were also corrected to exercise intended guards rather
than unrelated syntax errors: a valid internal entity declaration which ordinary
ElementTree resolves, a valid-root nested suite, and a hidden diagnostic failure.
The follow-up review found no new substantive correctness issue.

Focused command:

```sh
env -u ZEPHYR_BASE nix develop -c \
  python3 tests/unit/external_test_results/test_external_test_results.py
```

Eight methods passed in `/tmp/opencode/pb052-focused-r3.log`; SHA-256
`2da412166be4d38daf97a088bc24e99ca2efa6091fbb579eebffc883abf2bad1`.
Earlier failed log `/tmp/opencode/pb052-focused-r1.log`, SHA-256
`91aaef203585cd7e6e96757b8613b00181d73c0c127210d84b8dc6c4b3596f7d`,
is retained, not acceptance: it exposed a non-mutating XML test control. The
corrected tests require encoded-report mutations actually change bytes.

`scripts/check-test-matrix.py` and `git diff --check` passed at the focused
checkpoint. The subsequent clean candidate and hosted evidence below close the
implementation gate. Evidence availability is session-local and must be rechecked
before later reuse.

## Verified clean candidate and PR gate

Candidate `08b85230e0d53281a0316e8d2f9a7c351c4a8f04` passed canonical
**83 PASS / 0 FAIL / 83 TOTAL** in detached clean worktree
`/tmp/opencode/pb052-clean-r1`: 41 Twister, 6 exec-only and 33 Python children,
coverage, matrix and unchanged canonical BSim 17 scenarios / 26 runs.

```sh
env -u ZEPHYR_BASE TEST_OUTPUT_DIR=/tmp/opencode/pb052-clean-gate-r1 \
  nix develop -c bash scripts/test-all.sh
```

Raw log `/tmp/opencode/pb052-clean-canonical-r1.log`, SHA-256
`e339b0fdbf09bb070d759b899dc93aad1598dd39912c2785ef7d7a36a76e0e6c`.
The coverage manifest records the exact candidate and `dirty: false`; SHA-256
`96a1a6d12b9a38c9b1dccda1a5b7b33952d928531d76ce603e7ae890645ba959`.
Numeric coverage remains 5049/5491 lines, 2245/3036 branches and 378/378 functions.
Baseline SHA-256 remains
`5bb01f95afc12c0771086a537cb70c92d20f7d96c8b9b4323528b6d9ed76de7a`.
Matrix reports zero errors/notes. Production source, board configurations,
toolchain pins and workflow are byte-unchanged from the PB-045 completion commit.

Hosted [run 37140245411](https://github.com/qarnet/le-audio-receiver/actions/runs/37140245411)
on the exact candidate passed unit, coverage, BSim, aggregate `tests` and
`firmware`; `release` was skipped. The firmware build contract and package
verification passed. Watch log `/tmp/opencode/pb052-hosted-candidate-watch.log`,
SHA-256 `ea8a360daad3efe926f03427a16489a1e6061c4d09429bc099f4ea370b29b6b2`.

The Done transition ships in existing PR #16. Human merge is official acceptance.
This completes result accounting only, not actual isolated host execution or
any codec, protocol, RF, physical audio or release acceptance.
