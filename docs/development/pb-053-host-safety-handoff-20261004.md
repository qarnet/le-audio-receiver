# PB-053: safe host runner integration and input sealing

## Scope

Repair host runner safety before further guest execution. Files:
`scripts/bluez_host_guest.py`, `tests/unit/bluez_host_guest/test_bluez_host_guest.py`,
PB-053 notes. Do not change guest behavior or kernel/controller options yet.
Existing reviewed `bluez_host_process.run_owned` is available at committed
36e0f9e; nine real OS boundary tests passed. No downloads, vendor/SDK edits,
host Bluetooth/kernel changes, PB-051 or unrelated dirty files. No commit or VM
until Delegator reviews this integration.

## Exact API and validation

1. Import `run_owned` from sibling script module. Test import setup adds ROOT/scripts
   to sys.path before importlib-loading runner; no dynamic path guess in production.
2. `run(prepared, output, timeout, manifest_sha256)` now requires independently
   supplied lowercase64-hex manifest digest. CLI run adds required
   `--manifest-sha256`. Preparation stdout includes absolute prepared path and
   actual emitted manifest SHA256 alongside existing success/scope. Caller must
   retain digest from prepare, not recompute untrusted input inside run. This
   anchors local preparation evidence, not hostile same-user attestation.
3. Preparation emits schema_version2. Define scope constant:
   `isolated Linux/BlueZ host regression; no physical/codec acceptance`.
   Include qemu dict exact path/size/SHA256 and source_hashes for `host`, `guest`,
   `public` and `emulator`; current script source hashes captured during prepare.
   Existing kernel/config/initramfs artifact SHA256s and stimulus identity remain.
4. Before creating run output, read manifest from regular non-symlink file using
   `os.open(O_RDONLY|O_NOFOLLOW|O_NONBLOCK)`, fstat regular, cap64MiB. Read at most
   cap+1, validate expected hash on those same bytes, then JSON decode with
   duplicate-key rejection and no NaN/Infinity. Recursion/type errors fail cleanly.
   Require schema2, version7.1.5, exact scope/cpu profile, exact three artifact keys
   with lowercase64hex hashes, exact four source_hash keys matching current source
   and pinned emulator, exact stimulus size/hash/purpose, exact QEMU identity.
   Preserve remaining prepared provenance fields; validate types before indexing.
5. Artifact leaf files regular/nonsymlink via same safe open/fstat before any hash.
   Caps: kernel64MiB, config2MiB, compressed initramfs2GiB, all nonempty.
   Stream hashes with bounded chunks. Verify copied kernel matches pinned local
   KERNEL SHA256 and config matches current `/proc/config.gz` after matching
   uname/booted kernel checks. Missing local required runtime fails readiness.
   Do not follow FIFO/device/symlink or trust replaced kernel plus edited hash.
6. Resolve output protection against prepared directory, pinned emulator build
   directory, source clone, repository, home and store. Refuse equality/descendant,
   existing/symlink output before modification. New external sibling allowed.
   Preparation also protects preserved `/tmp/opencode/pb053-*` parents, same
   established preparation helper rule. No nested run output inside evidence.

## Snapshot and owned execution

7. After successful preflight, create exclusive output. Stream regular prepared
   kernel/config/initramfs into exclusive `output/inputs/` copies, revalidate cap
   and hash against manifest from bytes actually copied; make copies read-only.
   Fixed QEMU argv uses those snapshots, not prepared original paths. This closes
   original-file replacement between validation and launch. No host sharing.
8. Replace detached Popen/wait/kill logic with `run_owned(argv, output/serial.log,
   timeout, max_log_bytes=33554432)`. Keep same fixed QEMU flags, stdin DEVNULL,
   no extra-argument/command/network/passthrough/share option. Return record embeds
   entire process result and independently anchored manifest digest, snapshot
   hashes, QEMU identity, wall start/end, scope and cpu profile.
9. Always retain `run-record.json` in outermost finally once output created, even
   snapshot/spawn/signal/timeout/parser failure. Process failures cannot be accepted
   from a success marker. Only owned outcome.ok=true plus valid guest result may
   accept run. Existing guest result parser gets repaired next phase; keep its
   current fail-closed checks in this phase, no validation weakening.
10. Serial read is safe after process owner capped32MiB. Parse marker once; retain
    raw decoded guest dict (if exactly one valid JSON marker) even when ok=false
    or lifecycle validator fails, then retain validation_error and reject. Missing/
    malformed/duplicate markers fail. Do not lose structured guest failure evidence.

## Tests and verification

Add process-boundary CLI tests: FIFO/symlink artifact, oversized manifest, invalid
hash/types/keys/version/source pin, output nested under prepared/build evidence
all reject promptly without modifying sentinel or launching child. Input-shape
checks happen before installed-runtime checks so portable tests can prove rejection
on hosted Linux without private Nix artifacts. Test snapshot helper with authored
regular input: exact bytes/hash, replacement/change fails, source unchanged.
Actual process owner has separate nine tests; do not fake QEMU success as acceptance.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_process -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
git diff --check
```

No VM, staging or commit yet. Stop first unresolved failure/warning/design gap,
return exact question/diff. Preserve previous evidence and blocked task history.
PB-053 currently In Progress; source-owned errors are not external blockers.
