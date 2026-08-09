# FR1 review-fix handoff: clean output I/O failures

Date: 2026-08-09

## Goal

Close one FR1 review defect before FR1 remains accepted: output-side filesystem
failures must produce one stable `package-firmware-release: error: ...`
diagnostic with no Python traceback, while preserving atomic cleanup.

This is a focused correction to the accepted FR1 packager. Do not begin FR2.

## Review evidence

`scripts/package-firmware-release.py:401-409` catches `PackagerError` only.
Output creation and ZIP writing can raise `OSError` after all caller inputs pass
validation. The outer staging block removes the private staging directory, but
`main()` lets that `OSError` escape and Python prints a traceback.

Public-boundary reproduction used a valid fixture and launched the CLI with a
100-byte `RLIMIT_FSIZE`. Observed result:

- exit code 1;
- final output directory absent;
- no `.firmware-release-*` staging sibling remained;
- stderr contained three chained `OSError: [Errno 27] File too large`
  tracebacks rather than one stable prefixed diagnostic.

This violates `docs/development/firmware-release-fr1-handoff.md:67-68` and
exposes that current test 13 fails before staging is created, so it does not
prove cleanup after a packaging write failure.

## In scope

1. Update `scripts/package-firmware-release.py` so output-side `OSError`
   failures are handled by the public CLI with the same stable error prefix and
   no traceback.
2. Strengthen `scripts/test_package_firmware_release.py` with a Linux
   public-subprocess regression that fails during ZIP writing, after staging
   creation, and proves both diagnostic and cleanup behavior.
3. Run focused verification, commit the implementation fix, run the clean
   canonical gate, and update FR1 acceptance evidence to cite the fix commit.

## Out of scope

- FR2 CI workflows, container setup, artifact upload, or root `VERSION`.
- Artifact names, ZIP layout, manifest schema, checksum format, compression,
  input validation, success stdout, or flash notes.
- Firmware source, build helpers, board configuration, build outputs, release
  tags, GitHub Releases, MCUboot, or DFU.
- Broad exception swallowing. Programmer errors such as `AssertionError`,
  `TypeError`, or unexpected non-I/O exceptions must not be converted into
  caller diagnostics.

## Exact implementation

### Public CLI

In `scripts/package-firmware-release.py`, change only `main()` error handling:

- catch `(PackagerError, OSError)`;
- print exactly one line using existing `ERROR_PREFIX` and exception text;
- return the existing nonzero code `1`;
- retain current `PackagerError` behavior and successful return `0`;
- do not catch `BaseException` or broad `Exception` in `main()`.

Keep the existing `except BaseException` staging cleanup in `_run()`. It owns
cleanup and re-raises; `main()` owns conversion of expected caller/I/O failures
into process diagnostics.

### Public-boundary regression

In `scripts/test_package_firmware_release.py`:

1. Import Python's Linux `resource` module.
2. Add one test that uses `make_fixture()` and invokes the copied CLI through
   `subprocess.run`, with all normal required arguments and a `preexec_fn` that
   applies:

   ```python
   resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))
   ```

3. Assert externally observable behavior:
   - process exits nonzero;
   - stderr starts with `ERROR_PREFIX`;
   - stderr contains no `Traceback`;
   - stderr contains `File too large` (the Linux failure being exercised);
   - final output directory does not exist;
   - temporary parent contains no name beginning with `STAGING_PREFIX`.

Use a top-level helper for the `preexec_fn` if needed. Keep test Linux-specific
and deterministic; repository and hosted CI targets are Linux. Do not inspect
packager private functions or mock ZIP helpers.

Retain existing tests. Focused suite should increase from 18 to 19 tests.

## Verification and commits

First run:

```bash
python3 scripts/test_package_firmware_release.py
python3 scripts/test_inventory.py --python
python3 scripts/check-test-matrix.py
git diff --check
```

Inspect `git status`, `git diff`, and `git log --oneline -10`. Stage only:

- `docs/development/firmware-release-fr1-fix-handoff.md`
- `scripts/package-firmware-release.py`
- `scripts/test_package_firmware_release.py`

Commit:

```text
fix: handle firmware packaging I/O failures
```

With a clean tree at that commit, run:

```bash
./scripts/test-all.sh
```

Any failure or unexplained warning blocks acceptance. Expected child inventory
remains 63 total: this adds one test method inside the existing Python child,
not another gate child.

After a clean 63/63 gate, update only acceptance evidence:

- `docs/development/firmware-release-fr1-results.md`: record review defect,
  reproduction, fix commit, 19 focused tests, clean canonical result, and final
  FR1 acceptance at original implementation plus correction.
- `docs/development/firmware-release-plan.md`: make accepted FR1 line cite the
  correction commit/evidence without changing FR2-FR5 scope.
- `AGENTS.md` and `STATUS.md`: update top current-state clean-tree commit from
  `f3cd4c4` to the correction commit; keep 63 total, coverage population,
  coverage counts, build contract, and BSim facts unchanged unless actual gate
  output differs.

Run:

```bash
python3 scripts/test_package_firmware_release.py
git diff --check
git status --short
```

Inspect diff, stage only those four evidence/status files, then commit:

```text
docs: record FR1 packaging error fix
```

Do not push, merge, open a PR, tag, create a release, or amend commits.

## Escalation

Stop without committing incomplete work and report to Orchestrator if two
materially different attempts fail, `RLIMIT_FSIZE` does not reliably enter ZIP
writing on this Linux environment, cleanup leaves staging content, canonical
gate fails, or fix requires broader exception handling or artifact-contract
changes. Preserve worktree and return exact commands, output, diff/status, and
one precise question.

## Return

Return changed files, behavior change, focused and canonical results, both
commit hashes/messages, final git status, deviations, blockers, and suggested
FR2 follow-up. Include no AI or tool attribution.
