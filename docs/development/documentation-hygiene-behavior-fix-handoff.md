# Documentation-hygiene behavior-fix handoff

Base commit: `c966dc2` (P8 closeout). User approved fixing all confirmed
executable defects found during full-repository documentation audit. This is a
correctness/safety fix track, not permission for feature expansion or weakened
acceptance.

Validate each finding against current executable code before editing. If a
finding is not reproducible, record evidence and correct only false prose. Add
behavioral regression tests for every behavior change.

## Required fixes

### 1. Coverage output deletion safety

`scripts/test-coverage.sh` claims repo-contained output paths are rejected, but
current containment check is reversed. Before any `rm -rf`, reject:

- repo root itself;
- any output path inside repo root;
- `/`, empty, unresolved, relative/traversal ambiguity;
- any path whose canonical parent/target cannot be proven outside repo.

Preserve allowed `/tmp/...` output. Extend
`tests/unit/test_coverage_runner/test_test_coverage_runner.py` with real
temporary-repo path cases proving repo child is rejected before deletion and
external temp output works. Never run destructive test against real repo path.

### 2. Dongle merge failure

`scripts/bin/fw-build-dongle` must not suppress `mergehex.py` failure with
`|| true` or report a stale/missing merged hex as written. Fail nonzero and
remove/no-clobber stale target semantics as appropriate. Extend
`tests/unit/fw_flash_dongle` or add focused build-helper tests using fake tools
and temp files: merge failure returns nonzero and never reports success;
successful merge produces expected artifact.

### 3. Dongle reset probe selection

`scripts/bin/fw-reset-dongle` must select the dongle's J-Link, not
`scripts/probe-serial.local` or `nrf-probes --find nrf53` (those identify
CMSIS-DAP receiver targets). Ground selection in current dongle flash/reset
tooling and `nrfjprog`/J-Link enumeration already used by repo. Add fake-tool
tests proving receiver CMSIS-DAP selectors are never invoked and exact J-Link
selector/reset argv is used. No hardware in this fix phase.

### 4. BlueZ/WirePlumber receiver identity filtering

`scripts/bluez-wireplumber-gate.py` currently allows any BlueZ device/node,
including unrelated/MIDI nodes, to satisfy receiver readiness and sink lookup.
Require configured receiver identity/address/device association and actual
audio playback node/profile. Do not accept an unrelated BlueZ object. Extend
`scripts/test_bluez_wireplumber_gate.py` with multiple-device/node fixtures,
unrelated MIDI nodes, wrong receiver, and exact target success.

### 5. FLPR hang/stall gate claimed checks

For `scripts/flpr_hang_gate.py` and `scripts/flpr_stall_gate.py`, implement the
documented acceptance rather than weakening headers:

- parse captured `audio status`/relevant summaries;
- require zero I2S underrun, decode errors, push failures, stream resets, and
  other fault fields named by current output contract;
- hang gate require resumed `success + fallback >= expected` using an explicit
  tolerance grounded in duration/cadence, and include
  `asrc_fallback_triggered` in required checks when contract requires it;
- final status commands must be parsed, not merely sent.

Extend both Python unit suites with positive, each-fault negative, malformed,
missing, stale-summary, and partial-output cases. Preserve exact failure
evidence.

### 6. Raw-HCI retry/cancellation contract

`scripts/hci_raw_connect.py` must classify fatal command-status errors versus
retryable outcomes and stop retries for fatal status. At global deadline,
cancel any in-flight create-connection attempt before socket close regardless
of per-attempt deadline. Define status sets from Bluetooth HCI semantics and
existing caller needs; do not guess silently. Extend
`tests/unit/hci_raw_connect` for retryable status, fatal status, global deadline
mid-attempt cancel, cancel failure, and bounded exit.

### 7. Raw helper termination

`scripts/bap_central_security.py` must guarantee no surviving helper after
normal post-spawn failures: terminate, bounded wait, then kill + bounded wait;
surface failure if process still cannot be reaped. Do not claim SIGKILL/host
loss cleanup. Extend security tests for cooperative terminate, terminate
timeout→kill, kill timeout/error, and already-exited helper.

### 8. Official BSim smoke acceptance

`scripts/bsim-official-smoke.sh` must either verify its claimed >=100 completed
SDUs before accepting known upstream teardown failure, or stop calling that
failure accepted. Implement log parsing with exact marker/count and fail on
missing/malformed/short progress. Add a non-hardware shell/Python fixture test
for success, known teardown after sufficient progress, teardown too early, and
unrelated failure.

## Prose-only corrections in same implementation commit

Correct comments that overpromise behavior while touching these files:

- `bluez-wireplumber-phase3-gate.py`: serial close/reopen description, stale
  main.c line refs, `atexit`/SIGKILL and “every exit path” claims;
- `bap_central_security.py`: dangling pre-split line reference;
- `fw-flash-54l15`: remove unused merged-hex fallback prose/variable or make
  behavior match one clear supported path;
- test names/comments that claim strict last-summary semantics when parser uses
  maxima + last fault counters;
- stage1 scenario description must match transport-visible duplicate Release
  behavior expected by parser.

## Verification

Run every focused Python/shell suite changed, shellcheck/syntax checks available
in dev shell, `python -m py_compile`/unit tests, then canonical
`./scripts/test-all.sh`, fresh 3 builds only if build helper changes can affect
production artifacts, build contract 95/95, and `git diff --check`.

Warnings/failures cannot be normalized. Do not alter production firmware,
coverage baseline, BSim pins, acceptance thresholds, or hardware state unless a
confirmed fix strictly requires it and Orchestrator approves expansion.

## Commit shape

1. This handoff.
2. Safety/tool behavior fixes + focused regression tests.
3. Gate/parser enforcement fixes + focused tests.
4. Results doc with exact old bug/new behavior/verification.

No push, PR, amend, force-push, hardware, destructive operation, or attribution
footer.
