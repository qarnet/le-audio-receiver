# PB-053: guest-side file, event and subprocess bounds

## Goal and scope

Complete remaining resource-bound review before cancellation/accounting lane.
Files: new `scripts/bluez_guest_limits.py`, new
`tests/unit/bluez_guest_limits/test_bluez_guest_limits.py`, guest init/public,
host staging/source-pin checks/tests, PB-053 notes. No protocol/configuration,
vendor/SDK/host Bluetooth, PB-051, arbitrary RPC, downloads or unrelated edits.
R24 normal3phase/48frame run passed; preserve behavior and failed history.

## Exact implementation

1. Stdlib `read_bounded(path,limit)` opens O_RDONLY|O_NOFOLLOW|O_NONBLOCK,
   fstat regular, size<=positive integer limit; stream at mostlimit+1 bytes,
   fail on exceed. Empty regular logs allowed. Proc regular size0 may have content,
   so bound actual bytes too. No FIFO/device/link reads or silent truncation.
2. Stdlib `EventLedger(list)` defaults max_events4096,max_bytes524288. Override
   append under RLock: JSON-serialize finite/valid value to bytes, reject quota
   before insertion, store JSON round-trip copy so later external dict mutation
   cannot grow or alter retained event. Exact int positive limits, bool invalid.
   Expose normal list serialization; no dependency on private allocation identity.
   Public child result.events uses EventLedger. Existing callback/protocol checks
   unchanged. Errors fail operation, never drop events and claim success.
3. Guest PID1 after guards and before spawning applies process-local
   resource.setrlimit(RLIMIT_FSIZE,(8388608,8388608)). All inherited long-child
   regular logs hard-bounded8MiB; SIGXFSZ or abnormal exits fail lane. This is ONLY
   guarded guest process limit, never host kernel/user limits. No preexec_fn in
   multithreaded process. Monitor retains stricter own4MiB stdout budget.
4. Replace guest short command/public capture subprocess.run(capture_output) with
   reviewed run_owned, copied into `/opt/pb053/bluez_host_process.py`. Each command
   gets exclusive sequential `/tmp/pb053-command-NNN.log`, cap2MiB, current15s or
   public90s timeout. Use captured combined stdout/stderr, exact process record,
   read_bounded after child. Preserve failed child marker before acceptance,
   current structured parse then reject behavior. Do not lose command logs/errors.
5. Copy limits module and process owner into guest. Preparation source_hashes adds
   `limits` and `process` to prior7-key set; exact9 keys now host,guest,public,
   results,monitor,acquire,limits,process,emulator. Host validates current hashes.
6. Guest state_hashes caps file count1024, each regular nonsymlink file2MiB,
   aggregate state8MiB; stream hashes not read_bytes-all. Unsupported specialfile
   fails. Logs printed through read_bounded8MiB (monitor4MiB); any exceed marks
   failed cleanup, not truncated accepted evidence. Cleanup loops independently
   guard per-child TERM/wait/KILL/log reads and continue after first error; final
   marker and original failure retained. Do not let cleanup exception skip later
   actors or final output.

## Tests and verification

Real file/OS boundaries: exact/oversized/empty regular inputs, symlink/FIFO refused
promptly; EventLedger quota before insertion, original dict mutation does not
alter encoded report, concurrent append stays within quotas or rejects; real
ordinary child applying RLIMIT_FSIZE writes beyond cap and fails with file length
bounded, parent limit unchanged. Tests must never set parent/host process limit.
Existing nine process owner and23guest tests stay green. No private-field/helper
call-count feature acceptance, no skips.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_guest_limits -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_host_process -p 'test_*.py' -v
git diff --check
```

No VM/commit until review. Return exact tests/diff. First missing decision,
unexplained warning or two differing failures: stop with evidence, no guesses.
