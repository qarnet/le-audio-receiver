# PB-053: source-aware emulator preparation repair

## Goal and boundaries

Complete one preparation checkpoint: compile the pinned local BlueZ emulator
without macro-redefinition warnings and retain fresh immutable build evidence.
This is not PB-053 host-suite acceptance. The item remains In Progress.

In scope: `scripts/bluez_host_prepare.py`, a focused regression file
`tests/unit/bluez_host_prepare/test_bluez_host_prepare.py`, and PB-053 execution
notes through Backlog.md. Out of scope: VM boot, controller execution, host
adapters, package downloads/installers, vendor/SDK edits, PB-051, PB-013,
opencode configuration, release, and acceptance/status changes.

## Completed analysis

- Source HEAD is clean at `4dc15be8ee3f7422d447087f1893d215575cb2c8` in
  `/tmp/opencode/bluetooth-test-resources-20261003/bluez`.
- `configure.ac:3` identifies BlueZ 5.87. `Makefile.tools:97-106` defines
  the emulator source set and its internal Bluetooth/mainloop libraries.
- Existing script supplies global `-D_GNU_SOURCE`; retained
  `/tmp/opencode/pb053-emulator-build-r1/01.log` proves redefinition error
  at `emulator/serial.c:16` under `-Wall -Werror`.
- Selected files defining the macro themselves: `emulator/serial.c`,
  `emulator/bthost.c`, `emulator/phy.c`, `lib/bluetooth/hci.c`,
  `src/shared/mainloop.c`, `src/shared/mainloop-notify.c`,
  `src/shared/util.c`, and `src/shared/ecc.c`.
- Current `cc` is installed GCC 14.3.0. No dependency fetch is needed to retry.

## Exact implementation

1. Remove `-D_GNU_SOURCE` from common flags. Add a small source-specific flag
   helper accepting source bytes. Detect a line-start preprocessor definition
   with regex `^\s*#\s*define\s+_GNU_SOURCE\b` in multiline mode. Return no
   additional macro flag when present; otherwise return `-D_GNU_SOURCE`.
   Use this helper for each translation unit before compiling it. Preserve
   all other existing flags, source list, pin checks, exclusive output creation,
   logs, timeouts, version-only probe and failure record. Do not change vendor
   source or disable/suppress warnings.
2. Add focused unittest regression using actual `cc` invocations on authored
   temporary C translation units, one defining `_GNU_SOURCE` itself and one
   relying on preparation flags. Both should compile GNU `pipe2` declarations
   under `-Wall -Werror` using the helper. This proves compiler-visible behavior,
   not only flag-array shape. Missing compiler is failure, not skipped coverage.
   Temporary fixtures are authored, not copied vendor source.
3. Retry the public preparation CLI into a new exclusive external directory.
   Never reuse or clobber r1. Execute no emulator operation other than existing
   `--version`, which exits before controller creation. Keep raw errors if the
   next compilation/link stage fails. Do not invent a dependency or warning fix:
   return the next exact failure to Delegator for source analysis.
4. Record verified result, command, source pin, external evidence paths and
   limitations via `backlog task edit PB-053 --append-notes ...`. Do not check
   acceptance criteria or mark Done from this preparation checkpoint.

## Verification

From repository root:

```sh
python3 -m unittest discover -s tests/unit/bluez_host_prepare -p 'test_*.py' -v
python3 scripts/bluez_host_prepare.py --source /tmp/opencode/bluetooth-test-resources-20261003/bluez --output /tmp/opencode/pb053-emulator-build-r2
git -C /tmp/opencode/bluetooth-test-resources-20261003/bluez status --porcelain
git diff --check
```

Verify `/tmp/opencode` exists before creating output. If r2 already exists,
choose a new unique suffix, record it and preserve existing contents. Read real
build logs and record SHA-256 of binary and build record on success. Failure
must remain nonzero and retain record/logs. Do not run dirty-tree canonical
gates: paused PB-051 scaffolding is red and explicitly held.

## Executor contract

Use apply_patch for edits. Preserve all unrelated dirty files, private graph
data and immutable evidence. No commit/push in this narrow preparation phase;
commit belongs to a later reviewed, clean-candidate gate handoff. Return exact
files/diff summary, commands/results, external evidence, warnings and blockers.
If a load-bearing decision is missing, evidence contradicts the plan, two
materially different attempts fail, or repair needs scope expansion, stop and
return preserved state plus exact question. Do not guess architecture or
weaken tests. This handoff requests only the grounded repair above.
