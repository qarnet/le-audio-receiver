# Phase 0 — Cleanup Fix Handoff (flashing.md + script comments)

Status: **ready for implementation**
Follow-up to Phase 0 (already merged locally).

## Goal

Fix two minor issues left over from Phase 0:
1. `docs/flashing.md` is stale — it still references the old hardcoded CMSIS-DAP
   probe serial, the old build path (`build/le-audio-receiver/`), and the old
   `west flash` workflow without `--build-dir` / the `fw-flash-5340` helper.
2. `scripts/bin/fw-build-5340` and `scripts/bin/fw-build-54l15` have a comment
   typo: they say `le_audio_receiver` (underscore) but the actual sysbuild
   subdirectory is `le-audio-receiver` (hyphen).

## In scope

### A. `docs/flashing.md` updates

Rewrite the stale parts of `docs/flashing.md` to match the post-Phase-0 reality.
Read the file first. The following specific changes are required:

1. **Line 10 (Hardware, debug probe)**: replace the hardcoded serial
   `E6635C08CB1F502B` with a placeholder. New text:
   `Raspberry Pi Pico running CMSIS-DAP firmware (probe serial configured via scripts/probe-serial.local, see AGENTS.md)`

2. **Lines 27-31 (Flash workflow)**: the example `west flash` should become
   the `fw-flash-5340` helper. Replace the bare `west flash` block with:
   ```
   fw-flash-5340
   ```
   And add a one-line note that this helper runs
   `west flash --build-dir build/nrf5340`.

3. **Lines 35-53 (OpenOCD command constructed by west runner)**: the
   `cmsis_dap_serial E6635C08CB1F502B` line (line 42) should become
   `cmsis_dap_serial <from scripts/probe-serial.local or auto-detect>`. Keep the
   rest of the OpenOCD command structure unchanged.

4. **Lines 85-104 (CMake runner registration, the `app_set_runner_args` macro
   example)**: this code block currently shows the OLD macro with the hardcoded
   `--cmd-pre-init=cmsis_dap_serial E6635C08CB1F502B` line. Update it to match
   the new `CMakeLists.txt` — the `file(READ ...)` probe-serial extraction, the
   `if(_probe_serial)` branch, and the `else()` auto-detect branch. Show both
   branches (the actual `CMakeLists.txt` is the source of truth — copy its
   structure into the doc). Do NOT paste the entire CMakeLists; show just the
   `app_set_runner_args` macro body up to and including the two
   `board_runner_args(openocd ...)` branches and the `include(...)` line.

5. **Line 114-115 (CMAKE_BINARY_DIR comment)**: the comment currently says
   `CMAKE_BINARY_DIR inside the macro resolves to build/le-audio-receiver/`.
   Update to `build/nrf5340/le-audio-receiver/` (the new build tree path —
   `build/nrf5340/` is the sysbuild top-level dir passed via `-d` in
   `fw-build-5340`, and `le-audio-receiver/` is the app domain subdirectory
   inside it). The `../merged_CPUNET.hex` resolution explanation still holds:
   `CMAKE_BINARY_DIR` is `build/nrf5340/le-audio-receiver/`, so
   `../merged_CPUNET.hex` = `build/nrf5340/merged_CPUNET.hex`.

Do NOT touch:
- The "Current overlay" section (lines 119-133) — describes the board overlay,
  which is Phase 1 territory, but the description is still accurate for the
  current state. Leave it.
- The "What a custom board file would absorb" section (lines 134-152) — this is
  the Phase 1 migration plan reference. Leave it.
- Any other section not listed above.

### B. Script comment typo fix

In `scripts/bin/fw-build-5340` line 7:
```
# Result:     build/nrf5340/le_audio_receiver/zephyr/zephyr.elf
```
change `le_audio_receiver` → `le-audio-receiver`.

In `scripts/bin/fw-build-54l15` line 10:
```
# Result:     build/nrf54l15/le_audio_receiver/zephyr/zephyr.elf
```
change `le_audio_receiver` → `le-audio-receiver`.

No other changes to these scripts.

## Out of scope

- Everything else. No CMakeLists.txt changes, no flake changes, no source
  changes, no other doc changes. If you find anything else stale, STOP and
  report it rather than fixing it.

## Verification

```bash
# No machine-specific serial in flashing.md (the example in AGENTS.md is fine)
grep -n 'E6635C08CB1F502B' docs/flashing.md && echo "FAIL: serial still in flashing.md" || echo "OK: clean"

# No underscore name in script comments
grep -n 'le_audio_receiver' scripts/bin/ && echo "FAIL: typo remains" || echo "OK: clean"

# Build still works (incremental, fast — just confirms no breakage)
fw-build-5340 2>&1 | tail -5
```

The build step is a sanity check — since this handoff only touches docs and
comments, the build MUST still pass. Report the tail of the build output.

## Commit

One commit, message:
```
docs: update flashing.md for Phase 0 workflow + fix script comment typo

flashing.md still referenced the hardcoded probe serial, old build path,
and bare `west flash` workflow after Phase 0 landed. Update to match the
new fw-flash-5340 helper, probe-serial.local mechanism, and
build/nrf5340/ build tree. Fix le_audio_receiver → le-audio-receiver
typo in fw-build script comments (actual dir uses hyphen).
```

No AI attribution. Commit locally only. Do not push.

## Hard rules

- Implement ONLY the scoped fixes above.
- Do NOT touch CMakeLists.txt, flake.nix, any src/ file, sysbuild.cmake, prj.conf,
  boards/*, docs/design.md, docs/nrf54l15-drift-compensation.md,
  docs/development/*.
- Do NOT run fw-flash-5340 or any hardware command.
- Do NOT push, merge, open PRs, amend, or force-push.
- No AI/tool attribution in commit messages.

## Executor recap

Return: files changed, what changed, verification command output (paste actual
grep + build tail), commit hash/message, blockers or deviations.