# Phase 0 — Tooling & Hygiene — Implementation Handoff

Status: **ready for implementation**
Phase plan source: `docs/design.md` §Phase 0

## Goal

Collapse the three coexisting build workflows into one (the serial-mcp pattern),
delete dead code, remove the unused `west.yml`, move the CMSIS-DAP probe serial
out of the repo, and unify code style. After this phase a fresh clone plus
`direnv allow` (with a local probe-serial file) builds, flashes, and streams on
nRF5340 with no machine-specific edits to tracked files.

## Decisions locked (from orchestrator/user clarification)

1. **Helper scripts**: one script per target — `fw-build-5340`, `fw-build-54l15`,
   `fw-flash-5340` (and a `fw-flash-54l15` placeholder is out of scope — nRF54L15
   flash is single-core and can be added later; for now ship `fw-flash-5340`
   only since 54L15 audio is not yet working). Also ship a `fw-shell` helper that
   drops the user into a shell with the toolchain env loaded (the equivalent of
   the old `activate.sh` but using the new flake shellHook). Each script is
   self-contained, sources a shared `fw-common.sh`, validates that
   `ZEPHYR_BASE`/`west` are present, and exits with a clear message otherwise.
2. **Probe serial**: untracked local file `scripts/probe-serial.local`
   (gitignored). `CMakeLists.txt` reads it via `file(READ ...)` if present and
   passes the value to `--cmd-pre-init=cmsis_dap_serial <serial>`. If the file
   is absent, emit a status line and let OpenOCD auto-detect (no serial passed).
3. **Build tree**: repo-local `build/` (gitignored as today). The build scripts
   pass `-d build` (or a per-target subdir like `build/nrf5340` — see scripts
   below). Building happens from the repo root, NOT from `~/ncs/v3.3.0`.
4. **Code style**: full `clang-format` run with Zephyr's `.clang-format` (copied
   into the repo root as `.clang-format`, or referenced from the toolchain —
   executor decides; copying into repo is recommended so the format is stable
   and not dependent on NCS install). Apply to `src/main.c` and any other
   2-space-indented source files. The other modules already use Zephyr tab style
   so they should be a no-op but run clang-format across all `src/*.c`/`*.h` for
   consistency. This must be its own isolated commit (pure reformat, no logic
   change) so future diffs are clean.

## In scope

### A. flake.nix rewrite (port serial-mcp pattern)

Rewrite `flake.nix` to:

- Add `flake-utils` input and use `flake-utils.lib.eachDefaultSystem` (drop the
  hardcoded `system = "x86_64-linux"`).
- Drop nixpkgs `nrfutil` and `segger-jlink` from buildInputs (SEGGER J-Link is
  unused since the OpenOCD switch).
- Add a self-contained `nrfutil-core` derivation (copy the pattern from
  `~/repos/serial-mcp/flake.nix` — fetchurl from
  `https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/<triplet>/nrfutil`
  with autoPatchelfHook + the listed buildInputs: glibc, stdenv.cc.cc.lib,
  zlib, xz, libusb1, udev). Provide per-system hashes for x86_64-linux and
  aarch64-linux (copy the hashes from serial-mcp). aarch64-linux is optional
  but include the hash if it's already known there.
- Keep the existing `openocd-master` derivation (`nix/openocd-master.nix`) and
  the `openocdWrapped` wrapper. Keep `systemd` as a buildInput only for the
  `LD_LIBRARY_PATH` the openocd wrapper needs.
- Replace the `westWrapped` shell-script wrapper with the dynamic toolchain
  load: in `devShells.default.shellHook`, run
  `eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)"`
  guarded by `command -v nrfutil`. This sets PATH, LD_LIBRARY_PATH,
  ZEPHYR_SDK_INSTALL_DIR, ZEPHYR_TOOLCHAIN_VARIANT, PYTHONHOME/PYTHONPATH, and
  NRFUTIL_HOME — the toolchain's own `west` and `clang-format` then resolve
  correctly with no Python wrapper needed.
- Add the `ZEPHYR_BASE` derivation block from serial-mcp: strategy 1 (derive
  from `ZEPHYR_SDK_INSTALL_DIR`), strategy 2 (`$HOME/ncs/v3.3.0/zephyr`), clear
  error if neither exists.
- Add `gccMultiStdenv.cc` to the shell packages (Linux only) so native_sim /
  host builds work on NixOS.
- Put `$PWD/scripts/bin` on PATH in shellHook so `fw-*` helpers resolve.

### B. Helper scripts (`scripts/bin/`)

Create `scripts/bin/fw-common.sh` (modeled on serial-mcp's `firmware/bin/fw-common.sh`):
- `_fw_script_dir`, `_fw_repo_dir`, `_fw_build_dir` helpers.
- `_fw_require_env` guard: exits with a clear message if `west` is missing or
  `ZEPHYR_BASE` is unset/missing. Message tells the user to enter the dev shell
  (`direnv allow` or `nix develop`).

Create:
- `scripts/bin/fw-build-5340` — builds `nrf5340dk/nrf5340/cpuapp` with
  `--sysbuild --pristine` into `build/nrf5340` (or `build/` — executor picks one
  and is consistent; `build/nrf5340` recommended so a future `build/nrf54l15`
  doesn't collide). Passes extra args through (`exec west build ... -- "$@"`).
- `scripts/bin/fw-build-54l15` — builds `nrf54l15dk/nrf54l15/cpuapp` with
  `--pristine` (NO `--sysbuild` — single core) into `build/nrf54l15`. NOTE: this
  target is known-broken (design.md F1) and will fail. The script should still
  exist and run; the failure is expected and is the Phase 1 entry point. Add a
  comment at the top of the script noting this.
- `scripts/bin/fw-flash-5340` — runs `west flash --build-dir build/nrf5340`
  (or whichever build dir `fw-build-5340` uses). The OpenOCD runner config in
  `CMakeLists.txt` already chains the dual-core flash TCL.

Make all `scripts/bin/fw-*` executable (`chmod +x`).

### C. Probe serial extraction

In `CMakeLists.txt`:
- Remove the hardcoded `--cmd-pre-init=cmsis_dap_serial E6635C08CB1F502B` line.
- Before the `board_runner_args(openocd ...)` call, read the local file:
  ```cmake
  set(_probe_serial "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local" _probe_serial_raw)
    string(STRIP "${_probe_serial_raw}" _probe_serial)
  endif()
  ```
- Conditionally add the `cmsis_dap_serial` arg only when `_probe_serial` is
  non-empty. When empty, print a status line and let OpenOCD auto-detect.
- Add `scripts/probe-serial.local` to `.gitignore` (it already lists
  `.direnv/` and `build/`; add the new line).
- Commit a `scripts/probe-serial.local.example` documenting the format
  (single line, just the serial, e.g. `E6635C08CB1F502B`).

### D. Dead code deletion

Delete these files:
- `src/net_core_bootloader.c`
- `src/net_core_fw.h`
- `src/stream_tx.c`
- `src/stream_tx.h`

In `CMakeLists.txt` remove the line:
```cmake
zephyr_sources_ifdef(CONFIG_BT_AUDIO_TX src/stream_tx.c)
```

Verify with `git grep` that no other file references
`net_core_bootloader`, `net_core_fw`, `stream_tx`, or `stream_lc3`. The grep
output above shows references only in the dead files themselves and in
`docs/design.md`/`AGENTS.md` (documentation references are fine and stay).

### E. Remove `west.yml`

Delete `west.yml`. The app is freestanding against `~/ncs/v3.3.0`; the manifest
is unused (design.md F5, Assumption 3). Also drop the `west.yml` reference from
any docs that mention it — check `AGENTS.md` and `docs/flashing.md`.

### F. Delete legacy build scripts

Delete:
- `activate.sh`
- `build.sh`

These are replaced by the flake shellHook + `fw-build-*` helpers. The
`env_ncs.sh` mechanism dies with `activate.sh` (it was generated by
`activate.sh` at runtime; nothing committed).

### G. Code style unification

Copy `~/ncs/v3.3.0/zephyr/.clang-format` into the repo root as `.clang-format`.
This makes the format stable and not dependent on NCS install path.

Run clang-format across all `src/*.c` and `src/*.h` files using the toolchain
clang-format (available after `direnv allow` / `nix develop`):

```bash
eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)"
clang-format -i src/*.c src/*.h
```

This must be its OWN commit (pure reformat, no logic changes) so future diffs
stay reviewable. Run it AFTER all other Phase 0 changes are committed so the
reformat reflects the final file list (dead code already deleted).

### H. AGENTS.md updates

Update the `## Build`, `## Flash`, and `## Serial` sections of `AGENTS.md` to
reflect the new workflow:

- Build: `direnv allow && fw-build-5340` (from repo root, no `cd ~/ncs/v3.3.0`).
  Build tree is `build/nrf5340/` in the repo.
- Flash: `fw-flash-5340` (flashes both cores via the OpenOCD TCL chain).
- Probe serial: document `scripts/probe-serial.local` (gitignored) — copy
  `scripts/probe-serial.local.example` and put your probe serial in it.
- Keep the existing gotchas sections (they describe runtime behavior, not
  build workflow, and remain valid).

Do NOT update the "Plan of record" section's "Current status: no phase
started yet" line — that's the orchestrator's job after review. Leave the
gotchas about SW Split LL, settings_load, etc. untouched.

### I. flake.lock

`flake.lock` must be regenerated after adding the `flake-utils` input. Run
`nix flake update` (or `nix flake lock --update-input flake-utils`) and commit
the new lockfile.

## Out of scope (DO NOT touch)

- nRF54L15 build fix (F1.1/1.2/1.3) — Phase 1.
- Board abstraction / custom board definition — Phase 1.
- PACS 48 kHz restriction — Phase 2.
- `main.c` split / audio-sink interface — Phase 2.
- Drift controller refactor — Phase 3.
- `audio_i2s.c` `DT_ALIAS` switch — Phase 1 (F1.2).
- `sysbuild.conf` board-conditional netcore — Phase 1 (F1.3).
- `docs/nrf54l15-drift-compensation.md` — superseded, never update.

## Relevant files and current behavior

| File | Current | Phase 0 action |
|------|---------|----------------|
| `flake.nix` | x86_64-only, hardcoded toolchain path, nixpkgs nrfutil, westWrapped | Rewrite per serial-mcp pattern |
| `flake.lock` | nixpkgs only | Regenerate after flake-utils input |
| `activate.sh` | generates `env_ncs.sh` | Delete |
| `build.sh` | sources env_ncs.sh, runs west build | Delete |
| `west.yml` | unused manifest | Delete |
| `CMakeLists.txt` | hardcoded probe serial, dead-code CMake line | Extract serial, remove dead line |
| `src/net_core_bootloader.c`, `src/net_core_fw.h` | dead | Delete |
| `src/stream_tx.c`, `src/stream_tx.h` | dead, won't compile | Delete |
| `.clangd` | points at hardcoded build/le-audio-receiver compile DB | Update path to new build dir (`build/nrf5340`) OR make it relative — see below |
| `.gitignore` | `.direnv/`, `build/` | Add `scripts/probe-serial.local` |
| `AGENTS.md` | build/flash via `cd ~/ncs/v3.3.0` | Update to new helpers |
| `nix/openocd-master.nix` | openocd derivation | Keep as-is |
| `scripts/flash_nrf5340.tcl` | dual-core flash procs | Keep as-is |
| `scripts/monitor.sh`, `scripts/read_acm.py` | serial helpers | Keep as-is |

### .clangd update

`.clangd` currently points at
`/home/thomas-workstation/repos/le-audio-receiver/build/le-audio-receiver` — a
hardcoded absolute path that doesn't match the new build dir. Update to
`build/nrf5340` (relative to repo root, clangd resolves relative to the
`.clangd` file location). Keep the `Remove`/`Add` filter lists as-is.

## Test plan / verification

The executor MUST run these and report results:

1. **Flake loads**: `direnv allow` (or `nix develop`) succeeds, `west --version`
   prints, `ZEPHYR_BASE` is set and points at a real dir, `clang-format
   --version` works (no `libpython3.12.so` error — that error means the
   toolchain env wasn't loaded). Report the printed versions.
2. **No machine-specific paths in tracked files**:
   ```bash
   git grep -nE '/home/thomas-workstation|E6635C08CB1F502B|911f4c5c26'
   ```
   must return NOTHING (excluding the `.envrc` `git_autopull` line and any
   `docs/` references that are historical — but ideally none of those
   patterns remain in tracked files at all). Report the grep output.
3. **Dead code gone**:
   ```bash
   git grep -nE 'net_core_bootloader|net_core_fw|stream_tx|stream_lc3' -- src CMakeLists.txt
   ```
   returns nothing. The files `src/net_core_bootloader.c`,
   `src/net_core_fw.h`, `src/stream_tx.c`, `src/stream_tx.h` no longer exist.
4. **Build the nRF5340 target from repo root**:
   ```bash
   fw-build-5340
   ```
   completes. Report the final `build/nrf5340/zephyr/zephyr.elf` size or the
   west output's last lines. A successful build is the primary exit
   criterion. (Do NOT run `fw-flash-5340` — no hardware approval, and
   streaming verification is out of scope for the executor.)
5. **Probe-serial file ignored**: `git status` after creating
   `scripts/probe-serial.local` shows it as ignored.
6. **Clang-format clean**: after the reformat commit, `git diff` shows only
   whitespace/brace changes (no logic). Run
   `clang-format --dry-run --Werror src/*.c src/*.h` and report it exits 0.
7. **Unit tests still build** (sanity): `twister -T tests/unit/drift
   --platform native_sim` or, if twister is unavailable in the shell, at least
   confirm `tests/unit/drift/CMakeLists.txt` still references only files that
   exist. Report what you ran.

## Constraints and invariants (from repo docs)

- **nRF5340 must keep building.** Every change in this phase must preserve
  that. The build is the exit criterion.
- **No phase started yet** — the "Plan of record" section of `AGENTS.md` says
  so. Do NOT update that status line; the orchestrator does it after review.
- **`docs/nrf54l15-drift-compensation.md` is superseded** — do not edit it.
- **`docs/design.md` is the plan of record** — do not edit it.
- **The `sdk-nrf` west project must be named `nrf`** (AGENTS.md gotcha) —
  irrelevant once `west.yml` is deleted, but don't reintroduce a manifest.
- **SW Split LL overlay pair** (`sysbuild.cmake`) must stay intact — Phase 0
  does NOT touch `sysbuild.cmake` or `sysbuild.conf`.
- **`scripts/flash_nrf5340.tcl` must stay intact** — the OpenOCD dual-core
  flash procs are unchanged. Only the `cmsis_dap_serial` arg source changes
  (in `CMakeLists.txt`).
- **OpenOCD derivation stays** — `nix/openocd-master.nix` is kept as-is.

## Commit structure

The executor should make these commits, in order, each logically separated:

1. **flake + helpers**: rewrite `flake.nix`, regenerate `flake.lock`, add
   `scripts/bin/fw-*` + `fw-common.sh`, delete `activate.sh` + `build.sh`,
   delete `west.yml`, update `.gitignore`, update `.clangd` build path.
2. **probe serial extraction**: `CMakeLists.txt` changes +
   `scripts/probe-serial.local.example`.
3. **dead code deletion**: remove the four dead files + the CMake line.
4. **AGENTS.md workflow update**: build/flash/serial sections.
5. **code style**: copy `.clang-format` into repo + run clang-format across
   `src/`. Pure reformat commit.

Commit messages: no `Co-Authored-By`, no `Generated with`, no AI attribution.
Match the repo's existing style (imperative subject, body explaining why).

## Verification commands (executor runs these)

```bash
# 1. Flake loads
direnv allow
west --version
echo "ZEPHYR_BASE=$ZEPHYR_BASE"
clang-format --version

# 2. No machine-specific paths in tracked files
git grep -nE '/home/thomas-workstation|E6635C08CB1F502B|911f4c5c26' ':!docs' && echo "FAIL: machine paths found" || echo "OK: clean"

# 3. Dead code gone
git grep -nE 'net_core_bootloader|net_core_fw|stream_tx|stream_lc3' -- src CMakeLists.txt && echo "FAIL: dead code refs found" || echo "OK: clean"
ls src/net_core_bootloader.c src/net_core_fw.h src/stream_tx.c src/stream_tx.h 2>&1

# 4. Build nRF5340 from repo root
fw-build-5340 2>&1 | tail -30

# 5. Probe serial file ignored
echo "E6635C08CB1F502B" > scripts/probe-serial.local
git status --porcelain scripts/probe-serial.local  # expect empty (ignored)
rm scripts/probe-serial.local

# 6. Clang-format clean
clang-format --dry-run --Werror src/*.c src/*.h && echo "OK: format clean"

# 7. git status / log review
git status
git log --oneline -8
```

## Executor recap requirements

Return a concise summary covering:

- Files changed (per commit).
- Behavior changed (what's different for a developer).
- Tests/commands run + results (paste the actual output of the verification
  block above — at minimum the `fw-build-5340` tail and the `git grep` results).
- Commit hashes/messages.
- Blockers or deviations from this handoff (e.g. if a hash for
  aarch64-linux nrfutil was not available, or if clang-format produced
  unexpected changes in a non-main.c file).
- Suggested follow-up for Phase 1.

## Hard rules for the executor

- Implement ONLY the scoped Phase 0 work above. Do not fix F1 (nRF54L15
  build), F4 (PACS 48 kHz), or any other finding.
- Do NOT touch `sysbuild.cmake`, `sysbuild.conf`, `prj.conf`, `boards/*`,
  `src/main.c` logic, `src/audio_*.c/h` logic, `docs/design.md`,
  `docs/nrf54l15-drift-compensation.md`, or `scripts/flash_nrf5340.tcl`.
  The ONLY `main.c` change allowed is the clang-format reformat (commit 5).
- Do NOT run `fw-flash-5340` or any hardware command. No flashing, no reset,
  no serial port open. The build is the exit criterion; streaming verification
  is the orchestrator/user's job.
- Do NOT push, merge, open PRs, amend commits, or force-push. Commit locally
  only.
- No AI/tool attribution in commit messages.
- If something is ambiguous and not covered above, ask the orchestrator (do
  not guess at scope).