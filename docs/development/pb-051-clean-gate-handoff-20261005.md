# PB-051 clean gate handoff (2026-10-05)

Prepared checkpoint for the PB-051 clean-commit canonical gate. No commit,
stage, push, matrix run or hardware action has been made by this document;
review of this manifest is a precondition before the commit is prepared.

## Why a clean gate is required

The r3 full matrix ran on the dirty continuation tree with the pre-race-fix
runner (`scripts/ascs_bsim_run.py`
`ca974e1c2c082fbfd3b4b6eecb4a9d4200f46763239185a50472ce210577339d`). After
r3, one small runner repair made fast-exiting worker failures attribute
their real cause instead of the generic post-break diagnostic (current
SHA-256 `811da2c768b75df0e976fe136247904765bcbe74b96ea18aaa043fe709acd821`,
verified by direct review of the reviewed lines around the exited-break
re-evaluation). The clean canonical gate must therefore rerun the full
six-family matrix on the committed race-fix source; r3 is not
final-source acceptance. The production root CMakeLists also gained the
`-Wl,--wrap=bt_audio_data_parse` linkage, so the clean gate must build the
physical receiver images (build only, no flash) and prove the wrap applies
with zero new build diagnostics before any later physical use.

## Exact include list for the PB-051 code checkpoint

Add files by explicit `git add <path>` only; never `git add .` and never a
bulk unspecified `-A`. Everything not in this list stays untracked or
user-owned as it is today.

Tracked files (already in the index before this work):

- `CMakeLists.txt` (root; gains the production `-Wl,--wrap=bt_audio_data_parse`)
- `scripts/bsim-stage1-run.sh`
- `scripts/test-all.sh` (gains mandatory `bsim:ascs-protocol` dispatch
  after unchanged Stage 1, only in `bsim/all` phase, fresh external
  container, optional exclusive `TEST_OUTPUT_DIR` symlink)
- `tests/bsim/CMakeLists.txt`
- `tests/bsim/client/CMakeLists.txt`
- `tests/bsim/client/src/bsim_tx.c`
- `tests/bsim/client/src/bsim_tx.h`
- `tests/unit/test_coverage_runner/test_test_coverage_runner.py`

New files to add:

- `scripts/ascs-bsim-run.sh` (must carry the executable bit, 100755: the
  canonical dispatch invokes it as `bash <script>`, so the bit is not
  strictly load-bearing for that path, but it is a shell entry point and
  must be marked executable exactly like the neighboring stage1 runner)
- `scripts/ascs_bsim_run.py` (0644; always launched through
  `sys.executable` / `python3`, no exec bit)
- `scripts/ascs_results.py` (0644; imported, no exec bit)
- `scripts/bsim_link_env.py` (0644; imported, no exec bit)
- `scripts/check-ascs-results.py` (0644; launched via `python3`, no exec
  bit, although the shebang exists for direct manual use)
- `scripts/check-native-bsim-build.py` (0644; launched via `python3`)
- `src/bt_audio_ltv_guard.c` (0644)
- `tests/ascs_bsim/cases.json`
- `tests/ascs_bsim/client/CMakeLists.txt`
- `tests/ascs_bsim/client/Kconfig`
- `tests/ascs_bsim/client/client.c`
- `tests/ascs_bsim/client/overlay-ascs-mtu.conf`
- `tests/ascs_bsim/client/prj.conf`
- `tests/ascs_bsim/client/sysbuild.cmake`
- `tests/ascs_bsim/common/lane.c`
- `tests/ascs_bsim/common/lane.h`
- `tests/ascs_bsim/receiver/CMakeLists.txt`
- `tests/ascs_bsim/receiver/Kconfig`
- `tests/ascs_bsim/receiver/main.c`
- `tests/ascs_bsim/receiver/prj.conf`
- `tests/ascs_bsim/receiver/sysbuild.cmake`
- `tests/test-matrix.json` (one added `src/bt_audio_ltv_guard.c` entry:
  direct `ltv_bounds` suite, `stateful: false`, empty transitions/
  exclusions/acceptance, three `__wrap_bt_audio_data_parse` outcomes
  witnessed by existing `ltv_bounds` tests; this file was omitted from
  the first checkpoint and the canonical matrix failed for exactly that
  omission)
- `tests/unit/ascs_results/test_ascs_results.py`
- `tests/unit/ascs_runner/test_ascs_runner.py`
- `tests/unit/bsim_link_env/test_bsim_link_env.py`
- `tests/unit/ltv_bounds/CMakeLists.txt`
- `tests/unit/ltv_bounds/prj.conf`
- `tests/unit/ltv_bounds/test_ltv.c`
- every current `docs/development/pb-051-*.md` handoff and result document
  (the 27 dated files listed by
  `ls docs/development/pb-051-*.md`, including the new
  `pb-051-clean-gate-handoff-20261005.md` and
  `pb-051-ascs-results-20261005.md`)
- `docs/testing/ascs-protocol-regression.md`
- the PB-051 task file
  `docs/product/backlog/tasks/pb-051 - ASCS-protocol-rejection-and-lifecycle-regression-matrix.md`
  (its current on-disk state carries this track's history through the
  Backlog.md record)

## Explicit exclusions

- `AGENTS.md` and `opencode.json` (user/config-owned changes)
- `docs/product/backlog/tasks/pb-013 - ...`,
  `pb-041 - ...`, `pb-042 - ...` (user-owned task edits untouched by this
  track)
- `.codebase-memory/` (private graph data, untracked by policy)
- generated build output anywhere (`build/`, `twister-out*`,
  `compile_commands.json` links, `sysbuild-cli-*`)
- `__pycache__/` and `*.pyc` (already ignored, must never be staged)
- vendor/SDK files (never modified or added)
- external evidence roots under `/tmp/opencode/` (immutable, never copied
  into the repo by this manifest; the frozen `tests/fixtures/lc3/*.lc3`
  corpus files are already tracked and not re-added)

## Executable-bit check before commit

Exactly one new script needs the executable bit:

```
git update-index --chmod=+x scripts/ascs-bsim-run.sh
```

Verify with `git ls-files -s scripts/ascs-bsim-run.sh` expecting `100755`.
All Python files in this manifest stay 0644; they are always launched via
`python3`/`sys.executable` (never `./script.py`), so no `.py` exec bit is
needed and adding one would diverge from the repo style.

## Generated-file inventory exclusions for direct child launch

The runner itself requires no generated artifact in the tree: it creates
its own exclusive output root, writes build trees there and snapshots
sources. Nothing in the checkpoint list may include a file under
`build/`, `sysbuild-cli-*`, `twister-out*`, `__pycache__/`, a symlinked
`compile_commands.json` or any `.elf`/`.hex`/`.log` artifact. The BSim
environment lives in the SDK (`$ZEPHYR_BASE/../tools/bsim`), produced
in place by `bsim-env.sh`'s documented runtime step; it is never
committed.

## Pre-commit verification (already green, re-run before commit)

```
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/ascs_runner -p 'test_*.py'
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/ascs_results -p 'test_*.py'
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bsim_link_env -p 'test_*.py'
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_process -p 'test_*.py'
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_descendants -p 'test_*.py'
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/test_coverage_runner -p 'test_*.py'
bash -n scripts/ascs-bsim-run.sh scripts/bsim-stage1-run.sh scripts/test-all.sh
git diff --check
```

Latest observed results: ascs_runner 21/21 (8 fullsuite repeats plus 12
targeted repeats green after the race repair), ascs_results 13/13,
bsim_link_env 7/7, bluez_host_process 11/11,
bluez_host_descendants 4/4, test_coverage_runner green (45 tests), all
`bash -n` clean, `git diff --check` clean.

## Clean detached candidate and matrix plan (prospective, no run yet)

All three target paths verified absent before any use; none exist today:

- Candidate worktree: `/tmp/opencode/pb051-clean-candidate-r1` (fresh
  detached checkout of the committed PB-051 checkpoint commit; never
  created yet).
- Canonical output: `TEST_OUTPUT_DIR=/tmp/opencode/pb051-canonical-r1`
  (absent; the gate itself creates it).
- Raw owner log: `/tmp/opencode/pb051-canonical-r1.log` (absent).

Planned full canonical run from the candidate, with the candidate as the
command working directory (no `cd` chain), a preserved pipeline exit code,
and an exclusive immutable raw log (`noclobber` refuses to overwrite a
prior log):

```
env -u ZEPHYR_BASE nix develop -c bash -c '
  set -o pipefail
  TEST_OUTPUT_DIR=/tmp/opencode/pb051-canonical-r1 \
    bash scripts/test-all.sh --phase all
' > /tmp/opencode/pb051-canonical-r1.log

echo "gate exit: $?"
```

Run the outer command with the working directory set to
`/tmp/opencode/pb051-clean-candidate-r1` (never a `cd` inside the
command), and capture the exit code separately if the log redirection is
part of a pipeline. The raw log is exclusive: if
`/tmp/opencode/pb051-canonical-r1.log` already exists the shell must not
run (use `set -o noclobber` or verify absence first). An alternative is
running the command with only its exit status observed and the raw bytes
already redirected; either way a non-zero gate exit must never be masked
by a successful `tee`.

Plus the physical receiver build (no flash), proving the production root
wrap linkage with zero new diagnostics, same working-directory rule and
exclusive log:

```
env -u ZEPHYR_BASE nix develop -c bash -c 'fw-build-54l15' \
  > /tmp/opencode/pb051-canonical-r1-fw-build.log
echo "fw-build exit: $?"
```

Acceptance for the clean gate: the full canonical gate passes with the
frozen BSim 17/26 Stage 1 unchanged, the ASCS matrix at exactly
60/65/259/269 on the race-fix runner, coverage baseline SHA unchanged
exactly at `5bb01f95afc12c0771086a537cb70c92d20f7d96c8b9b4323528b6d9ed76de7a`
(the baseline is frozen; it must NEVER be re-baselined here; if the new
suites create a legitimate coverage conflict, escalate for analysis
instead of silently approving any rebaseline), no
unlisted warning in any build, and the physical receiver CPUAPP/FLPR
build clean with the wrap verified. Any failure is evidence, not a waiver;
no skip rule, no baseline loosening. No flash, no hardware acceptance, no
release: those stay separate.

Canonical gate failure record (2026-10-05, immutable, never retried or
amended): the first checkpoint `af83365` omitted
`scripts/check-ascs-results.py` and the `src/bt_audio_ltv_guard.c`
manifest entry, so its canonical run at `/tmp/opencode/pb051-canonical-r1.log`
(SHA-256 `1aedc228a6a1a86aa30afc4932e35395d1fc89c66db1d3f72078444d52debc46`)
and retained ASCS record `/tmp/le-audio-ascs.s547IF/run` completed
`94 PASS / 4 FAIL / 98 TOTAL` with all four failures rooted in those two
omissions; that run stays accepted=false evidence. The completion commit
adds exactly those two items; a fresh candidate, output root, raw log and
fw-build log (all `-r2`, verified absent before use) carry the only
authoritative rerun.