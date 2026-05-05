---
name: commit-and-push
description: Review outstanding changes, ensure all tasks are complete, run tests if needed, then create logically separated commits and push to the remote.
---

# commit-and-push

Review the current state of the working tree, confirm that all tracked tasks
are complete, decide whether testing is required, then create logically
separated commits and push to the remote.

## Steps

### 1. Check for unfinished TODOs

Read any active TODO list (in the conversation or on disk).  If any item is
still `pending` or `in_progress`, stop and ask the user whether they want to
complete those items first, skip them, or cancel.

### 2. Collect the diff

```bash
git status
git diff --stat
git diff --cached --stat
```

Note every changed, added, or deleted file.  Use `git diff` (unstaged) and
`git diff --cached` (staged) to understand the content of every change.

### 3. Decide whether testing is required

Testing is **required** unless all of the following are true:
- Every changed file affects only documentation (`.md` files, `AGENTS.md`,
  `README.md`, skill files under `.agents/`).
- No `.c`, `.h`, `.conf`, `.overlay`, `.cmake`, `.yml`, or build-script
  files are touched.

When testing is required:

3a. **Build natively**
```bash
nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- bash -c "cd ~/ncs/v3.3.0 && west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine"
```

Must succeed with `build/merged.hex` present.

> Use `--pristine` after Kconfig or DT changes to avoid stale CMake cache
> issues.

3b. **Check for warnings**
Scan the build output for compiler warnings (especially `-Werror` violations,
`-Wint-conversion`, or `-Wimplicit-function-declaration`).  If any warnings
appear, treat them as failures and fix them.

If the change touches audio pipeline (LC3, I2S, BAP stream receive) or
board-level config, ask the user whether they want to flash and verify on
the real device.

If any test step fails, report the failure and stop.  Do not commit.

### 4. Group changes into logical commits

Inspect the diff and group files by **logical unit**.  Every commit should
contain a coherent, self-contained change.  Examples:

| Logical unit | Typical files |
|--------------|---------------|
| LC3 decoder fix | `src/main.c` |
| I2S driver fix | `src/audio_i2s.c`, `src/audio_i2s.h` |
| Build-system change | `CMakeLists.txt`, `prj.conf`, `sysbuild.cmake`, `sysbuild.conf` |
| Board config change | `boards/*.overlay` |
| West manifest | `west.yml` |
| Documentation update | `.md` files, `AGENTS.md`, `README.md` |

If a file contributes to more than one logical unit, split the changes into
separate commits using `git add -p`.

**Ordering principle:** infrastructure first (configs, west manifest), then
library code, then application code, then docs.

**Rules:**
- Never combine unrelated changes into one commit.
- Prefer 2-4 small commits over one giant commit.
- If in doubt, ask the user how they want the commits structured.

### 5. Commit

Follow the existing commit-message style (check `git log --oneline -5`):

```
<type>: <imperative summary>
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `build`.

Messages are lowercase, no trailing period, 50-72 chars for the summary.

```bash
git add <files for commit 1>
git commit -m "type: summary"
git add <files for commit 2>
git commit -m "type: summary"
# ... repeat for each logical unit
```

**Git Safety Protocol:**
- NEVER update `git config`.
- NEVER run destructive or irreversible commands ( `push --force`,
  `hard reset`, etc.) unless the user explicitly requests them.
- NEVER skip hooks (`--no-verify`, `--no-gpg-sign`) unless the user requests it.
- NEVER force-push to `main` or `master`.  Warn the user if they request it.
- Avoid `git commit --amend`.  Only amend when ALL conditions are met:
  1. User explicitly requested amend, OR the commit succeeded and pre-commit
     hooks auto-modified files that need including.
  2. HEAD was created by you in this conversation.
  3. HEAD has NOT been pushed to remote (`git status` shows
     "Your branch is ahead").
- If a commit FAILED or was REJECTED by a hook, NEVER amend — fix the issue
  and create a NEW commit.
- If you already pushed to remote, NEVER amend unless the user explicitly
  requests it (requires force push).

### 6. Verify

```bash
git status
git log --oneline -5
```

Confirm the working tree is clean and the commit history looks sensible.

### 7. Push

```bash
git push
```

If the branch has no upstream and `git push` would fail, use:

```bash
git push --set-upstream origin <branch>
```

## Exit handling

- If TODOs are still open, stop and ask.
- If tests fail, report the failure and stop.
- If any commit is rejected by a hook, report the hook output and stop.

## Notes

- Build runs natively via nrfutil toolchain launcher.
- West-managed repos (zephyr, nrf, modules) live in `~/ncs/` on the host
  and are **never** committed to this repo.
- Never commit generated files: `build/`, `.west/`, `zephyr/`, `nrf/`,
  `modules/`, `tools/`, `bootloader/`, `nrfxlib/`, `test/`.
- The `.gitignore` already covers all generated and west-managed content.
