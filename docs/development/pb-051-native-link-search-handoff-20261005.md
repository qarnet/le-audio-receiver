# PB-051 native BSim linker search order (2026-10-05)

## Bounded repair

Canonical receiver and client `ninja.out` each contain eight hidden
`skipping incompatible` diagnostics: the Nix GCC wrapper puts 64-bit glibc
and libgcc directories ahead of the ELF32 native simulator libraries. A
private ASCS build with validated 32-bit paths prepended to `NIX_LDFLAGS`
removed the diagnostics. Preserve all existing flags and hardening; do not
turn warnings off, change architecture or patch the SDK. Keep canonical 17/26
recipes unchanged.

Add `scripts/bsim_link_env.py` with a stdlib `resolve_library_dirs(compiler='gcc')`
returning validated libc32 and libgcc32 directories. Query only the two fixed
GCC `-m32 -print-file-name=` runtime library names. Reject missing or malformed
results; for libgcc test the reported file followed by the documented sibling
`32`, `lib`, `lib32` candidates in order. Validate real regular ELF32 i386
little-endian libraries through resolved symlinks, with bounded reads and no
nonregular reads. Validate glibc runtime siblings separately because `libc.so`
may be a linker script. Emit only shell-quoted `export NIX_LDFLAGS=...` with
the new `-L` paths ahead of the previous value. Integrate after each runner's
nrfutil toolchain environment and before its compile. No physical builds.

Public CLI tests use an authored executable compiler and temporary real ELF
headers, symlinks and FIFOs; include query failures and shell-quote injection
negative controls. Run a real current-compiler query and one fresh exclusive
external `gcc -m32 -Wall -Werror` link/execute smoke under
`/tmp/opencode/pb051-native-link-smoke-r1` with full stdout/stderr and ELF
header proof. Run focused ResourceWarning unit tests, shell syntax and diff
check. No canonical/full ASCS matrix, hardware, commit, stage or push.

Record outcomes in this handoff and PB-051 notes. Synthetic header tests prove
validation logic, not actual ABI; smoke proves host linker and runtime only,
not Bluetooth behavior or PB-051 acceptance.

## Focused result

`python3 -W error::ResourceWarning -m unittest discover -s
tests/unit/bsim_link_env -p 'test_*.py' -v`: six tests passed. Both runner
scripts passed `bash -n`; `git diff --check` passed. Current `gcc -m32`
reported `libc.so` under
`/nix/store/yhawd8dka2563b5mg3vjm5h14sw5lv95-glibc-multi-2.40-224/lib/32`
and **64-bit** `libgcc_s.so.1` under
`/nix/store/yygma80xg8axc2df157lvdnf181zhx7s-gcc-14.3.0-lib/lib64`.
The helper checked four ELF32 i386 glibc siblings and selected verified GCC
`/nix/store/yygma80xg8axc2df157lvdnf181zhx7s-gcc-14.3.0-lib/lib`.
These paths are observed evidence, not production hardcodes. Prior
`NIX_LDFLAGS` remained appended, with new paths first.

Exclusive `/tmp/opencode/pb051-native-link-smoke-r1` holds authored `main.c`,
executable, complete `compile.stdout`, `compile.stderr`, `run.stdout`,
`run.stderr` and `record.json` (compiler argv, old/new flags, header and
exits). Real `gcc -m32 -Wall -Werror` compile/link returned 0; both compiler
streams empty, so no `skipping incompatible` or other diagnostic in this
smoke. Executable's first 20 bytes
`7f454c4601010100000000000000000003000300` prove ELF32 little-endian i386;
execution returned 0 with empty stdout/stderr. No BSim or ASCS receiver/client
rebuild was run; absence of their Ninja warnings is **not yet verified**.
Canonical 17/26, full ASCS, physical and hosted gates remain unrun.

## Direct review follow-up: bounded query and file descriptor (2026-10-05)

Review found the original fixed GCC query captured output in memory before
checking its 64 KiB limit and accepted nonempty stderr below that limit.
`_query` now uses the existing `bluez_host_guest.run_bounded_command` generic
process owner with a fresh `TemporaryDirectory`, separate 64 KiB stdout/stderr
caps, fixed arguments and 10-second production deadline. No guest setup or
VM checks are called. Successful bounded stdout is read after the command
exits; any stderr, nonzero exit, cap breach or deadline fails before parsing.
Compiler output must name exactly one absolute path without whitespace; a
shell-quoted *assignment* alone cannot preserve spaces in the subsequent
`NIX_LDFLAGS` compiler flag list. `_elf32` resolves the candidate symlink,
opens with `O_NONBLOCK | O_NOFOLLOW`, checks `fstat` regular-file status on
that descriptor before reading at most 20 bytes, then closes it. No runner,
production or PB-053 implementation changed in this follow-up.

Focused ResourceWarning-as-error run: seven unit methods passed. New negative
controls reject exit-zero compiler stderr and space-containing paths. Authored
compiler fixtures flood stdout or stderr in repeated 4096-byte writes, or
stall with a live child; the bounded owner rejects each and test checks no
live descendant remains. Test-only `_query(timeout=0.3)` speeds deadline
control; production default stays 10 seconds. `git diff --check` passed.

New exclusive `/tmp/opencode/pb051-native-link-smoke-r2` retains authored
`main.c`, full separate compile/run stdout and stderr, ELF32 i386 executable,
and `record.json` with real search directories, old/new flags, header and
exit codes. Actual `gcc -m32 -Wall -Werror` compile/link and executable both
returned 0 with **zero bytes** in each captured stdout/stderr. ELF header
`7f454c4601010100000000000000000003000300` confirms 32-bit little-endian
i386. R1 evidence remains untouched. Neither canonical nor ASCS matrix was
rerun, so actual receiver/client Ninja warning removal remains unverified.
