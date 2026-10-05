# PB-051 owned matrix runner (pre-execution design, 2026-10-05)

Approved policy SHA-256 is `addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`.
Public entry point accepts only a new external output path, this caller-supplied
digest and a 30 to 600 second per-cohort timeout. Six families are mandatory
(60 cases, 65 rendered phases, 259 raw exchanges and 269 response records).
No optional family success, legacy `ASCS_CASE`, SDK mutation or canonical
BSim change. No real build, simulator execution or hardware action authorized
in this design phase.

Outer single-main-thread descendant scope owns all source checks, bounded
build commands, runtime readiness, six cohort supervisors and checker runs.
Each cohort supervisor has its own scope and starts three worker processes
concurrently. Each worker owns one actor via `run_owned`, bounded log and
record; supervisor seals execution only after scope closes. The checked
schema is the exact schema accepted by `ascs_results.check_execution_record`.
Parent records failures and stops remaining families without fabricating
missing records or laundering partial results into success. Retain all raw
commands, configs, logs, hashes, tool/SDK/source identities and owner scopes.
Successful synthetic unit jobs do not establish BabbleSim execution.

Before execution authorization, exercise real ordinary-child process
boundaries in a private test root, ResourceWarning-as-error suites for runner,
checker, native link resolver and process/descendant owners, shell syntax
and `git diff --check`. Preserve all dirty user edits and historical evidence.

## Implementation checkpoint (before any actual SDK build or matrix)

`scripts/ascs_bsim_run.py` now fixes policy, six-family order, cohort
timeout, source and SDK identity, exclusive external output, native-image
copy and three-role argv. Main owns a descendant scope; a separately owned
cohort supervisor opens its own scope, starts three actor workers concurrently
with bounded selector output and identity-checked pidfds, and seals a family
record only after all actor records and scope closure pass the existing
checker. Final verdict requires all six checker results and 60/65/259/269;
failed builds or families leave partial evidence and `accepted: false`.
`scripts/ascs-bsim-run.sh` now supplies pinned shell environment and calls
the Python entry point; legacy `ASCS_CASE` fails with explicit full-matrix
diagnostic. No SDK source or fixture was edited.

Real ordinary subprocess tests cover three concurrent workers, first failure
terminating siblings, short internal test deadline, output quota, spawn
failure, two delivered signals, and a detached grandchild whose containment
causes an expected failed scope. Private job tests validate exact job digest,
owned paths, copied ELF32 identities, source mutation and missing actor
records before sealing. Synthetic CMake text checks exact expected-warning
whitelist and rejects unrelated warnings. A separate host-compiled ELF32
fixture uses verified 32-bit linker paths and three actual `run_owned` actor
processes to seal one authored seven-case control-family trace through the
unchanged checker. First compile exposed five `skipping incompatible`
linker diagnostics despite exit 0; corrected fixture compiler search paths
using `bsim_link_env.resolve_library_dirs`, preserving visible warnings
and requiring empty compiler stderr. The fixture logs and ELF32 images are
explicitly synthetic, **not** BabbleSim peer execution or firmware proof.
Actual SDK builds, six-family runtime records and matrix remain unverified
and require separate authorization. No hardware, SDK build, simulator matrix,
staging, commit or push in this phase.

Final focused verification: ResourceWarning-as-error unit suites
`ascs_runner` 10/10, `ascs_results` 10/10, `bsim_link_env` 7/7,
`bluez_host_process` 11/11, `bluez_host_descendants` 4/4; total 42/42.
`bash -n scripts/ascs-bsim-run.sh` and `git diff --check` passed.
Expected argparse diagnostics for rejected legacy `ASCS_CASE` and wrong
policy digest appeared in the negative CLI tests. No actual six-family
runner or clean-commit/hosted gate was attempted. Review code and native
owner evidence before authorizing the first real matrix run.

## Direct-review repairs before matrix authorization

Installed BSim PHY is ELF64 little-endian x86_64 (class 2, machine 62);
NtNcable and Magic models share that ABI. Encrypted receiver/client and
`libCryptov1`/crypto probe are ELF32 little-endian i386 (class 1, machine
3). `check-bsim-runtime.py` compares only encrypted peers to crypto, not
PHY to crypto. Runner copy/job verification and independent execution
checker now enforce these role-specific ABIs and retain existing exact
bytes/hash, argv and scope checks. Synthetic native test compiles encrypted
peer stand-ins `-m32` with verified ELF32 search paths and PHY stand-in
`-m64` with ordinary host environment; both compiler stderr streams are
required empty. Previous all-three-ELF32 fixture is rejected for PHY.

SDK root now comes from absolute `ZEPHYR_BASE` ending `v3.4.1/zephyr`,
not a workstation-specific location. Both Zephyr and nrf HEAD must match
approved pins and tracked `git status --porcelain --untracked-files=no`
must be empty before build and again at final integrity check. Generated
untracked SDK output is not treated as tracked source. Exact source
population includes `tests/bsim/Kconfig`, ASCS internal headers and
bounded sysbuild/native board support files. Every source is a bounded
regular read (2 MiB/file, 32 MiB total, at most 512 files), copied to an
exclusive SHA(path)-named snapshot, then live and copy bytes/identities
are checked before build, after build and at final seal. No SDK source or
policy changed.

Worker cohort now bounds inherited-pipe drain for two seconds after all
leaders exit, then returns a failure so its `DescendantScope` can clean
adopted detached descendants. On error, TERM has one six-second grace,
KILL one further second, with bounded worker waits and captured cleanup
faults. A real worker forked a TERM-ignoring detached grandchild *with
stdout still inherited*; the cohort failed within bounded time, scope
cleaned the child, and an unrelated sibling stayed alive until test-owned
cleanup. No pipe was mocked closed.

Cancellation now latches signals across `run_owned` handler replacement,
retains each owned command record before rejecting, and prevents later
commands. Root `Cancel` spans output creation through bounded terminal
suite-record publication; after scope entry it reinstalls its body handler
without nesting, merges scope cancellation after exit, and records first
signal as `cancelled_signal`. One corrective write to that new owned record
handles a signal during initial publication. Actor record publication uses
same first-signal fail-closed rule. Real signal tests cover child command,
worker cohort and root seal outside any child call. Internal job fields use
strict JSON types (including rejecting boolean schema/int aliases) and
closed role paths/argv; no new public subset/case option.

All checks remain host-only. Full six-family firmware build/matrix,
hardware, staging, commit and push are still prohibited pending review.

Final pre-matrix unit verification after these corrections:
`ascs_runner` 13/13, `ascs_results` 11/11, `bsim_link_env` 7/7,
`bluez_host_process` 11/11 and `bluez_host_descendants` 4/4,
all under `-W error::ResourceWarning` (46/46). Shell `bash -n` and
`git diff --check` passed. Real mixed-ABI synthetic cohort sealed through
independent checker; real six-family BabbleSim execution not attempted.

## Post-r2 host-owner diagnostic phase (2026-10-05)

The one authorized full six-family r2 using the preceding runner source was
accepted at `/tmp/opencode/pb051-owned-full-matrix-r2` (run ID
`0f6e3024ced843f481dcca2633dca6f4`, SHA-256 of terminal suite record
`a1ece653b093e4056ed1396205be1fb6dd529a168f74740937d6a936fb7080eb`).
Six actual checkers accepted 60 cases, 65 renders, 259 raw exchanges and
269 response records with 18 zero-exit native actors. That immutable run
precedes this diagnostic phase and does **not** test the diagnostic additions.

Before any further native controls, `worker_cohort` now optionally populates
a real-process diagnostic dictionary. Fields: `started_at`, `ended_at`,
`first_cause` (`cancelled`, `deadline`, `worker_failure`, `output_quota`,
`spawn_or_identity`, `final_cleanup` or null on normal completion), first
`cancelled_signal`, `timed_out` (true **only** when the first detected cause
is deadline), `error`, `cleanup_faults` and `workers` keyed by the three
roles. Each started worker retains actual `pid`, process `start_ticks`,
observed exit or null, capped log byte count/SHA-256, quota flag and own
cleanup faults. No missing actor record gets a fabricated zero result.
After the supervisor scope exits, its `cohort-result.json` schema 1 retains
that diagnostic, exact family/run/image metadata, `accepted`, error(s),
cancellation/deadline flags and the finalized scope. Normal acceptance
occurs only **after** unchanged `seal_family` verifies the exact independent
execution-record schema. Generic cohort `accepted` is strictly owner-level:
the parent checks it before running the separate public protocol checker,
and it never claims a 60-case result by itself. Signal during publication
corrects this newly owned record once to failure, without changing an old
execution record or clearing the first cancellation.

Host-only ResourceWarning-as-error suites: `ascs_runner` 15/15 and
`ascs_results` 11/11, plus `git diff --check`, passed. Real ordinary child
controls cover success, failure, cancellation and deadline causality;
one TERM-ignoring child delayed cleanup past a test deadline without
reclassifying the initial worker failure. An actual host-compiled mixed-ABI
synthetic cohort retained success and separate failed-client terminal
records; neither is a new BSim run. A real signal delivered during JSON
encoding changed a prospective accepted cohort record to cancellation
failure. Detached inherited-pipe cleanup and restored scope flags remain
tested. No firmware, client, policy, SDK, hardware, new matrix, commit or
push occurred in this phase. Actual native three-actor control runs await
separate authorization.

## Post-r2 review repairs (2026-10-05, three load-bearing fixes, no new matrix)

Direct review of the accepted r2 matrix found three concrete gaps; each is
now closed, without any client, policy, canonical, SDK or hardware change.
The accepted r2 evidence (60/65/259/269, suite digest
1ac67c47bddf9d1a697c4afd655f898150b187c538d54f345958b0168f676d6a and the
earlier immutable runs) remains untouched; the current runner source is
again unverified against a real matrix until a separately authorized run.

1. Runtime identity re-verification. The runner captured the three live
   simulator libraries once; nothing rehashed them across the six cohorts
   or the final seal. `verify_runtime_identity(result, bsim, stage)` now
   rehashes exact installed bytes/size/path against the initially recorded
   identity via the same strict `_regular_snapshot` (O_NOFOLLOW regular
   bounded read, IMAGE_LIMIT) at three boundary classes: after the
   runtime-ready preflight, immediately before each of the six cohorts
   (`cohort:<family>:pre`), and at the final source/lineage check. A
   drifted, resized, replaced or absent live library raises ValueError,
   lands in the suite errors list and keeps `accepted: false` while all
   earlier raw evidence stays retained. This proves detectable ordinary
   file-level runtime drift per read; it makes no claim of immunity
   against adversarial same-size same-content replacement races.

2. PHY `-nodump`. The installed PHY opens default dump files when unset
   (`components/ext_2G4_phy_v1/src/p2G4_main.c:1100` calls
   `open_dump_files` unless `args.dont_dump`, flag declared at
   `p2G4_args.c:81`), so the fixed argv now carries literal `-nodump`:
   `expected_argv` in `scripts/ascs_bsim_run.py`, the exact validation
   argv inside `check_execution_record` in `scripts/ascs_results.py`, and
   every authored synthetic execution fixture in
   `tests/unit/ascs_results/test_ascs_results.py`. Records missing the
   flag are rejected by both the runner job contract and the independent
   checker. This disables unused default CSV side effects outside the
   external evidence root; protocol cases, limits, logs and the frozen
   totals (same 60/65/259/269, RealEncryption=1) are unchanged. Canonical
   Stage1 argv and existing SDK/results artifacts are untouched.

3. Components root pin and consumed-header snapshots. `FindBabbleSim.cmake`
   resolves `BSIM_COMPONENTS_PATH` separately from `BSIM_OUT_PATH`
   (`zephyr_get` of both, lines 25-26), so an alternate components root
   could previously build peers against different sources than the pinned
   output. The public preflight now fails before root creation/build
   unless an absolute `BSIM_COMPONENTS_PATH` resolves exactly to the
   installed `<sdk>/tools/bsim/components` (existing output path pin
   unchanged). The consumed component include surface is now snapshotted:
   all `libUtilv1/src/*.h` (11 headers) and `libPhyComv1/src/*.h` (4
   headers) from the pinned components root plus the installed
   `zephyr/cmake/modules/FindBabbleSim.cmake` join the frozen source
   population (150 entries at this SDK, still inside the 512-entry/2
   MiB-per-file/32 MiB budget) and are covered by the existing before,
   after-build and final freeze comparisons. This bounds the headers this
   lane's builds actually consume; it is not and is not labeled an
   SDK-wide or whole-components source pin.

New real-file/CLI tests (all host-only, no SDK launch): runtime identity
drift detected between simulated cohort boundaries and at the final check,
population missing entry rejected, alternate root path drift rejected;
exact PHY job argv carries `-nodump` while a stub job stripped of it fails
the job argv equality contract; public preflight rejects wrong absolute
and unset components roots before root creation and proceeds past the pin
to the deeper ordinary failure when the correct path is set (suite record
still `accepted: false`, no components-pin error); consumed component
headers plus FindBabbleSim enter `source_paths` and real header content
drift is detected by the freeze comparison, with `.32.d` build artifacts
excluded.

Focused verification after the three fixes, all under
`-W error::ResourceWarning`: `ascs_runner` 20/20 (16 existing + 4 new),
`ascs_results` 13/13 (12 existing paths + new "phy missing -nodump"
execution-record negative), `bsim_link_env` 7/7, `bluez_host_process`
11/11, `bluez_host_descendants` 4/4. `bash -n scripts/ascs-bsim-run.sh`
and `bash -n scripts/bsim-env.sh` pass; `git diff --check` passes. Post-fix
source SHA-256: scripts/ascs_bsim_run.py
3f4b34f8c9f7e1679e068e036f36ad29a5b12102ea90bc6b6e060fea8a66b510,
scripts/ascs_results.py
d9b8f467fc66c0080ad95df377ffdb650b536d8812a5e24a693f322d8440536a,
tests/unit/ascs_runner/test_ascs_runner.py
2432a3cc138a19e78ae4300aae202733b67d5e54f300452a4dd8444ee4abdf17,
tests/unit/ascs_results/test_ascs_results.py
a975012f0faf68040d19d13cc12559beb1df302db50b2b9385c8a83805a73c17.
No real matrix or clean gate has run against the fixed runner; that
requires direct review of these changes and a fresh authorization.

## Runtime recheck call-site completion (2026-10-05, review follow-up)

Direct review found the initial placement incomplete: the helper ran only
after the runtime-ready preflight, before each cohort, and on the success
final, but not before the readiness command, not after each cohort
returned, and not on a failed execution. Exact call sites now match the
review contract in `execute()`:

- `runtime-ready:pre` runs immediately after the initial three-library
  capture and before the `runtime-ready` owned command; the post-command
  `runtime-ready` recheck follows.
- Each cohort loop runs `cohort:<family>:pre` before the owned cohort
  command and `cohort:<family>:post` immediately after the command returns,
  before `verify_cohort_result` and any protocol acceptance.
- A `finally` block recheck with stage `post-run` runs whenever the suite
  ends, success or failure, guarded only by a populated identity record and
  a resolved pinned root; a vanished root or missing library is itself
  reported through a distinct
  `post-run runtime integrity: ...` entry after the SDK and source
  integrity entries so the original in-flight failure is preserved, never
  overwritten, and root absence is never a waiver. `bsim` is resolved once
  (strict absolute Path) before the capture so the finally recheck cannot
  silently target a changed env root, and it is pre-bound to `None` so a
  pre-capture ending skips the recheck legitimately instead of raising a
  new NameError inside the finally block.

Behavioral proof comes from executing the real `execute()` lifecycle
against an isolated fake SDK with only external boundaries replaced
(review follow-up, replacing an earlier removed stand-in that had copied
the finally logic into the test): three fresh roots drive plain,
ready-stage-hook, cohort-stage-hook and vanished-library phases. The
ready-stage hook mutates real `libCryptov1` bytes through the real
command boundary and raises an authored original failure; the recorded
outcome keeps that original failure first and appends the distinct
post-run integrity entry. The cohort-stage hook mutates the same real
library inside the cohort command; the `cohort:control_frame_validation:post`
recheck fails with the exact drift name before any fake family verdict is
recorded and no protocol acceptance exists for that family. The
vanished-library phase proves root/library disappearance is reported by
the closing recheck instead of being silently skipped. Only the external
SDK identity, tool discovery and owned command dispatch are replaced;
`verify_runtime_identity` itself is never mocked; library files are
actually read by every stage and actually mutated by the hooks, and the
actual execute finally runs in every phase. Header glob stays top-level
`.h` (15 files at this SDK); no nested or speculative scope added.

Verification after this fix: `-W error::ResourceWarning` ascs_runner
21/21 and ascs_results 13/13, `bash -n scripts/ascs-bsim-run.sh`, and
`git diff --check` pass. Source SHA-256 after this follow-up:
scripts/ascs_bsim_run.py
415c68127a288cf166fcf07205b023ef4efd4301faa0d2db4cc377fca2af14db
(plus the finally-guard correction removing the silent root-existence
skip); tests/unit/ascs_runner/test_ascs_runner.py
534f857ed05010eec1f9125efb35e3363c8ae4458f58f5f29179a6646dc55807
(behavioral execute-lifecycle replacement with vanished-root phase
added). Historical note: the runner digest
415c68127a288cf166fcf07205b023ef4efd4301faa0d2db4cc377fca2af14db cited
here earlier is a historical intermediate only; the final current file
after the follow-up is
ca974e1c2c082fbfd3b4b6eecb4a9d4200f46763239185a50472ce210577339d
(sha256sum on the live file). Still no real matrix or clean gate until
review of the behavioral call sites; prior r2 evidence remains immutable.
