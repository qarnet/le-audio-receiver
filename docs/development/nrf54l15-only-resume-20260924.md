# All-nRF54L15 migration: pause and resume, 2026-09-24

## Continuation update (2026-09-25)

For the current state, read `nrf54l15-only-continuation-20260925.md` first.
The checkpoint below records what was known at pause; its matrix-running,
HCI-board-image, and unfinalized-QoS statements are not current instructions.

The pause-state tables below remain a historical checkpoint, not today's
implementation status. Primary branch `feature/nrf54l15-only-continuation`
at `6941c82` still has substantial uncommitted migration work. Read
`AGENTS.md` and the newer dirty-tree records
`docs/development/pb-019-hci-resume-results.md`,
`pb-034-primary-repair-results.md`, `pb-035-source-matrix-results.md`,
`pb-036-source-artifact-results.md`, and `pb-037-retirement-results.md`.
XIAO source uses direct GRTC and one CPUAPP image; receiver uses CPUAPP + FLPR;
second XIAO HCI and standalone source roles alternate. PB-035 full 20-row
matrix r2 subsequently passed; later final-image HCI repeat failed. No clean
canonical gate, exact RH4/FR4, analog qualification, completed migration, or
public release is claimed. Preserve
all immutable run evidence and hashes below.

## Read this first

The user paused work to update OpenCode. **The overall migration is not
complete. Do not report it as accepted or publish a release.** Continue
directly in:

```text
/home/thomas-workstation/repos/le-audio-receiver
branch: feature/nrf54l15-only-continuation
HEAD:   6941c82 (PB-033: prepare implementation review)
```

There are substantial intended **uncommitted and untracked** changes. No new
commit, push, PR, or merge was made in this session. Do not reset, clean,
stash, or overwrite them. `git diff --stat` excludes the important untracked
HCI application, new PCM references, tests, backlog items and documents.

The earlier primary checkout was at `12b1ba6`. Inspection found the actual
continuation at `6941c82` in `/tmp/opencode/le-audio-receiver-pb-fixture`.
The primary branch was created from that commit and the pending continuation
files were moved into the primary repository. Consequently the old worktree
now has intentional missing/moved files. **Do not resume it, copy its tree
over this one, or mistake its deletions for new cleanup work.** No merge or
cherry-pick is needed to recover PB-033 here.

## Authority and goal

The approved goal is no active nRF5340 dependency anywhere in the product or
its test infrastructure:

- XIAO nRF54L15 receiver with the attached I2S DAC is the final product.
- The second bare XIAO nRF54L15 supplies standalone HIL source and Linux HCI
  central roles sequentially, by changing firmware.
- Canonical BSim uses `nrf54l15bsim/nrf54l15/cpuapp` for both peers.
- Host-native unit tests, Python and Twister remain appropriate. They are
  not nRF5340 hardware dependencies.
- Remove obsolete active nRF5340 receiver, fixture, controller, board/helper,
  build/test and documentation paths after replacement validation.
- Preserve historical evidence, old hashes, completed/archive backlog and
  historical startup recipes. Do not remove facts from the past.

The user explicitly rejected repeated permission requests for routine work.
`AGENTS.md` now records this in both worktrees: expand research, repair
fixtures, make grounded production changes and rerun gates autonomously.
An obsolete handoff's narrower scope is not a permission barrier. A failing
test is not a hard blocker. Preserve the intended product and meaningful
acceptance; never disable audio, skip failing cases, loosen limits, or replace
the tested fault merely to obtain a pass. Ask only for a real missing
capability/access or a consequential product/acceptance decision.

Standing Nordic lab authority includes flash/reset/RF/testing after fresh
identity verification. Do not touch unrelated peripherals, modify SAMD11
firmware, publish firmware, or merge PRs. No new commit/PR authorization was
requested or exercised. Respect the repository's clean-commit coverage rule;
do not fabricate a clean acceptance record.

## Durable checkpoint

A private, gitignored checkpoint is stored under:

```text
.session-checkpoints/2026-09-24-nrf54l15/
```

Its README and manifest describe source snapshot, Git patch/status, diagnostic
scripts, selected build images/configuration and retained session evidence.
The checkpoint is meant to survive loss of `/tmp` or an OpenCode update.
Raw HCI logs may contain pairing material: **keep this directory private;
never stage or upload it.** New checked-in PCM fixture files in
`tests/fixtures/lc3/` are different: those are intended source assets.

The current working tree is authoritative. Do not apply the saved patch to an
already-modified tree. If `/tmp` still exists, use its original immutable run
directories. If not, consult the checkpoint inventory and extract only the
needed evidence/tools into a fresh external directory. Revalidate hardware;
an archived identity is not fresh authorization to operate on a probe.

## Backlog ownership

Use `nix develop -c backlog ...`; do not hand-edit statuses.

| Item | State at pause | Responsibility |
| --- | --- | --- |
| PB-033 | Review | Existing two-XIAO session binding and standalone-source physical proof, committed before this session. |
| PB-034 | In Progress | nRF54L15BSim migration, now radio/PCM matrix passing; remaining docs/full-gate/coverage integration. AC 1, 2, 3, 5 checked from evidence; AC 4 pending. |
| PB-019 | In Progress | XIAO Linux HCI controller/bridge prototype and physical qualification; no AC checked yet. |
| PB-035 | Inspect current file | Promote the XIAO standalone HIL source throughout mandatory matrices. |
| PB-036 | Inspect current file | Source artifact/schema and capture-fixture migration. |
| PB-037 | Inspect current file | Delete active legacy E83 receiver paths. |
| PB-038 | Backlog | Final all-nRF54L15 integration/audit/gates. |
| PB-039 | Ready | Renewal-of-approval record created before the existing continuation was discovered. Not a competing implementation; PB-038 owns final integration. |

PB-019 was explicitly refined from a DK/RTS-CTS assumption to the actual
two-XIAO setup. Its filename still contains the old DK title; use the item ID.
PB-034/PB-019 overlap because one owner was validating the simulator repair
while qualifying the next hardware component, not because work was delegated.

## PB-034: implemented and verified

Detailed record: `docs/development/pb-034-primary-repair-results.md`.
Earlier `pb-034-continuation-20260924.md` records failed probes and the
withdrawn scope-only blocker; do not reuse its old blocker conclusion.
`pb-034-nrf54l15bsim-handoff.md` contains useful grounding, but its old
low-latency policy and PCM-reuse assumptions were disproved below.

### Simulator and fixture

- Both peers target nRF54L15BSim, integrated SW Split, single image.
- Removed BSim CPUNET child setup and old controller fragment filenames.
- Client uses `BT_CTLR_CONN_ISO_RELIABILITY_POLICY=y`, not low-latency policy.
  Actual payload tracing proved single-CIS loss under the old policy. Merely
  changing receiver ISO reservation did not fix it and was reverted.
- Client TX uses three-SDU per-CIS prefill, then shared SDU-period pacing,
  separate logical-corpus and transport sequence numbers, and a real
  18-interval right-side omission after 48 right payloads.
- Fixed first-stop scenario's admission threshold: after startup and before
  disabling the first ASE, allow the remaining CIS to finish its 45-send cap.
- Existing corpus bytes, exact TX hashes, 17 scenarios/26 runs and numerical
  PCM limits remain intact. No empty-SDU substitution for absent transport.
- Expected negative-path SDK warning for the intentional sequence jump is
  required exactly once, on the client in that scenario only:
  `Unexpected seq_num diff between 47 and 66 for <pointer>`.
  Wrong count, gap, role, scenario or unrelated warnings still fail.

### Production receiver repair

`src/audio_modea.c/.h` now resolves the oldest surviving half with mate PLC
when the bounded queue fills while the other CIS supplies no callback. It
does not discard the surviving audio. A wrap-safe resolved-event timestamp
fence rejects late halves or sequence-generated sentinels for emitted events.
`audio_stream_session.c` reapplies the configured frame interval after
start-clear resets the assembler.

Before this repair, a new public-boundary assembler test failed on both
native_sim and the physical source XIAO. The same test passed after repair:

- `/tmp/opencode/pb034-modea-red-board-evidence`: 15 pass / 1 fail.
- `/tmp/opencode/pb034-modea-green-board-evidence`: 16 pass / 0 fail.
- `/tmp/opencode/pb034-modea-green-native`: 16 pass / 0 fail.
- `/tmp/opencode/pb034-session-green-native.log`: real session/liblc3 tests
  passed, including absent callbacks, resumed sequence gaps without double
  PLC, close behavior and post-start-clear LOST prediction.

The on-board test is a production-module regression, not a physical
radio-loss-injection claim. The receiver board itself was **not** reflashed
with this repair during the session. Final physical receiver validation is
still needed.

### PCM reference correction

Pinned liblc3 is `48bbd3eacd36e99a57317a0a4867002e0b09e183` from NCS v3.3.0.
Its PLC random seed survives good frames. Eight startup PLC calls therefore
change a later loss/recovery trace even after 48 valid frames. The prior
handoff's proposal to reuse the legacy loss PCM for zero-start history was
wrong. Do not resurrect it or relax numerical limits.

Six target-native recipes were appended, preserving the nine historical
recipes. Two new generated references were independently replayed through
pinned host liblc3, not copied from receiver output:

| File | SHA-256 |
| --- | --- |
| `stateful_48k_10ms_skip20_start0_l.pcm` | `cead2e59efb32c78cb87818c710ca727082fd9bb9137bb8255b4f1e37d9be024` |
| `stateful_48k_10ms_loss48x18_start0_r.pcm` | `40204f38b2a359c3d4348131b5900bc1d065fda4423173c6eee5839c1ddf3fbd` |

Legacy generated files remain byte-identical. Generator rejects aliasing
different generated recipes to the same filename. Manifest, Python/C/CMake
recipe mirrors, BSim embedding and ARM calibration were updated. Host and
physical ARM calibration passed **46** records, including a wrong-startup-
history negative case:

- `/tmp/opencode/pb034-native-calibration-with-mutation-20260924.json`
- `/tmp/opencode/pb034-arm-calibration-evidence/uart.bin`:
  `PB031_ARM_PASS metrics=46`.

### Gate evidence and pending checks

- **BSim strict 17 scenarios / 26 runs PASS:**
  `/tmp/opencode/pb034-primary-canonical-20260924-04.log`, with peer logs in
  the same path without `.log`.
- Parser suite: **146 PASS / 0 FAIL**, `pb034-parser-tests-03.log`.
- Focused pytest snapshot: **62 passed**, `pb034-focused-tests-02.log`;
  this predates the final additional calibration mutation.
- Full unit phase: **71 PASS / 1 FAIL / 72 TOTAL** in
  `pb034-unit-gate-20260924-02.log`. The only failed child was the BSim parser's
  stale 108/26 loss mirror; fixed and independently rechecked afterward.
  **No complete unit rerun after all later HCI/central/QoS changes.**
- New BSim configuration test moved to `tests/unit/bsim_target/`. Two Python
  files in the old `bsim_runner/` directory caused duplicate inventory labels.
- Coverage baseline was not changed. Use report-only first for diagnosis;
  baseline writing/enforcement requires a clean exact commit. Production
  branches changed, so check real per-file coverage rather than assuming
  old ratios survive.

## PB-019: current HCI implementation

New untracked application: `dongle/hci_uart/`:

- `CMakeLists.txt`, `prj.conf`, `app.overlay`.
- `src/main.c`: continuous asynchronous UART RX with two 512-byte DMA
  buffers, an 8 KiB SPSC byte ring, thread-context packet allocation and
  controller submission, asynchronous TX, fail-visible hardware-error path,
  and retained assertion file/line for SWD diagnosis.
- `src/h4_rx.c/.h`: portable H4 parser. Supports command/ACL/ISO framing,
  arbitrary chunking and multiple packets per chunk. Errors latch until
  explicit reset rather than searching payload bytes for new headers.
- `tests/unit/hci_h4/`: encoded-traffic C regression compiled/run by Python.
  Passed. No ISO-credit proxy remains in the final source.

Target: `xiao_nrf54l15/nrf54l15/cpuapp`, **SDC**, UART20 P1.9/P1.8 at
1,000,000 baud, no RTS/CTS, stock SAMD11 bridge unchanged. TIMER21 belongs
directly to the UART driver's byte-counting mode; its standalone counter node
is disabled. MPSL's documented TIMER10/TIMER20 ownership is not violated.

Source public lab BD_ADDR stays `C0:AA:BB:CC:DD:EE`. Controller peripheral
connections are disabled. Broadcaster capability is enabled because Linux
queries advertising-set capacity when Extended Advertising is advertised;
without it, initial HCI setup failed at opcode `0x203b` with Unknown Command.
LL privacy was enabled to avoid current Linux/BlueZ device-privacy errors.

### Important UART SDK defect and workaround

The upstream interrupt-driven H4 sample uses one-byte RX DMA. It stalled
during streaming. The replacement uses continuous async RX and TIMER-backed
Nordic bounce buffers, with the documented 1024-byte total buffer and a
4000 us switch-latency budget.

Simply setting that budget did **not** work. Physical SWD captured assertion
`uart_nrfx_uarte.c:1270`, `bounce_limit < bounce_buf_len`, and a runtime
threshold of **502**, despite the requested 4000 us budget.

Installed NCS v3.3.0 driver's static initializer applies the microseconds-to-
bytes conversion twice and then overwrites the correctly computed runtime
value during async initialization. The application now calls
`uart_config_get()` and reapplies that same configuration with
`uart_configure()` **before RX starts**. This recomputes the correct threshold
**112** for a 512-byte half-buffer at 1 Mbaud/4000 us. SWD verified 112.
No SDK file was modified; assertions and warnings were not suppressed.

Key evidence:

- Failed state: `pb019-async-stall-core-03` through `-06`.
- Fixed threshold: `pb019-async-corrected-threshold`.
- An early debugger probe used unsupported Tcl `mrw` and stopped before its
  resume command; the next probe resumed the board. This is historical,
  not the current state. Harden that diagnostic helper before reuse.

### Current firmware on the source board

The final diagnostic flash restored candidate **async-r5**, after later
experiments were rejected:

```text
/tmp/opencode/pb019-xiao-h4-async-r5/zephyr/zephyr.hex
SHA-256 3e95fba54276d6651e1f099cc6d83e6e9d4c3e98649ee71222a2852b1ea5fed1
```

`pb019-xiao-h4-standard-qos-flash/` records that restoration and fresh
identity. This is HCI firmware, **not** the standalone HIL source or ARM
calibration firmware. Current source uses six real SDC ISO HCI buffers and
1000 us UART RX idle timeout, matching the restored candidate's behavior.

### Rejected experiments: do not retain or reintroduce casually

1. UART idle timeout 100 us instead of 1000 us did not improve peer delivery.
   Restored 1000 us.
2. In-memory NCP timestamps proved the nRF submitted completion events to UART
   about every 10 ms, while Linux received bursts of six roughly 50–60 ms
   apart. This locates observed batching downstream of event dequeue, not
   inside SDC; it does not prove exactly which downstream component batches.
3. A real-buffer-backed 20-credit bridge queue/proxy was implemented and
   tested as a sensitivity probe. It did not improve receiver delivery.
   It was fully removed: no `iso_credit.c/.h`, no event rewriting, no
   virtual credit accounting, and `BT_ISO_TX_BUF_COUNT` is back to 6.
4. Source ISO link quality showed 50 flushed versus 3 unacknowledged packets
   in one midstream snapshot, supporting missed scheduling/deadlines rather
   than attributing all loss to over-air errors. Counters are not a direct
   receiver-SDU count. `hcitool cmd` can print the first unrelated HCI event;
   use the matching Command Complete in `btmon.log`, not that short output.

Relevant runs: `pb019-xiao-h4-quality-02`, `pb019-xiao-h4-ncp-trace-01`,
`pb019-ncp-trace-core-01`, `pb019-xiao-h4-async-audio-09` (rejected credit
proxy experiment). Temporary NCP instrumentation was removed.

## Linux central fixes and latest QoS probe

Two grounded harness fixes are in the working tree:

- `bap_central.py`: normal-discovery `--preserve-bond` now actually invokes
  the existing BlueZ reconnect path instead of merely skipping Pair().
- `bap_central_endpoint.py`: repeated pre-configuration selection of the
  **same** reserved mono channel is idempotent. BlueZ retried selection across
  disconnects without a SetConfiguration/ClearConfiguration pair; rejecting
  every retry deadlocked reconnect. Different-channel selection and a second
  configured transport remain rejected. A new public-boundary test proves
  retry plus single-transport ownership. Endpoint suite passed 56 tests
  **before** the later QoS change.

Fresh pairing should use normal BlueZ discovery on this working controller,
not the old raw-HCI workaround. Direct raw connection caused SMP/race failures
with current BlueZ. The diagnostic uses explicit-address **bonded reconnect**,
which selects BlueZ Connect rather than the raw helper. It clears only the
selected lab peer's host cache before fresh rows, then issues receiver
`bt unpair`, keeping other adapters untouched.

### Latest unfinalized change

The endpoint now returns the standard `48_4_1`-style QoS:

```text
Retransmissions = 5  (was 2)
Latency = 20 ms      (was 10 ms)
PresentationDelay = 40000 us, unchanged
Interval = 10000 us, payload/channel shape unchanged
```

This was a diagnostic/repair probe for deadline-related flushes, not a
threshold relaxation. Under the old 2/10 QoS, six 30-second source runs
completed but mono/Mode B receiver valid delivery was about 2917 SDUs and
Mode A about 2830 per channel, with ongoing PLC. Under 5/20, mono delivery
improved to 3002/3007 valid SDUs with losses largely at startup and no
decode error, underrun or reset. **The source change remains unfinalized:**
review the trace, finish qualification, update tests/docs if retained, or
revert this isolated QoS change if evidence calls for a different repair.
Do not claim the old exact-QoS unit tests are currently green.

## Interrupted 120-second run and cleanup

Last command was:

```sh
nix develop -c env PB019_BLUEZ_DISCOVERY=1 \
  python3 /tmp/opencode/pb019-hci-probe.py \
  /tmp/opencode/pb019-xiao-h4-standard-qos-120s \
  120 DB:A6:0C:05:A2:AA \
  > /tmp/opencode/pb019-xiao-h4-standard-qos-120s.log 2>&1
```

`120` is **stream duration per case**, not the overall timeout. The helper
runs six cases: mono, mono reconnect, Mode A, Mode A reconnect, Mode B,
Mode B reconnect. Nominal audio alone takes 720 seconds, plus setup/teardown.
The tool call's timeout was 1,200,000 ms (20 minutes); each BAP subprocess
had a separate 180-second limit. Child stdout was buffered until each case
finished, then the outer shell redirected everything to a file. That explains
the long period without visible feedback.

The user aborted this run. Five case return codes and 12000-frame results
are retained. Sixth case has no completed CLI record; UART shows it had
started. **This is an interrupted partial run, not six-case acceptance.**

Important retained receiver observations:

| Completed case | Receiver summary / steady observation |
| --- | --- |
| Mono | valid 12005, decoded 12040, PLC 35; sampled PLC stayed 35 across about 100 s. |
| Mono reconnect | valid 12006, decoded 12040, PLC 34; sampled PLC stayed 34. |
| Mode A | first-half summary decoded 24080, PLC 70; sample PLC stayed 70. |
| Mode A reconnect | aggregate PLC increased **78 to 194** between samples; right valid 11899. This still needs classification and comparison with unchanged acceptance limits. |
| Mode B | valid 12006, decoded 24080, PLC 68; sampled PLC stayed 68. |

All these retained summaries reported zero decoder errors, I2S underruns and
stream resets. That does **not** excuse the Mode A reconnect loss or establish
the interrupted sixth case. Do not treat rounded `(0%)` UART percentages as
zero loss; compare exact counter deltas.

Abort killed the Python/audio processes but left detached `sudo`/`btattach`
and `sudo`/`btmon` groups. They were explicitly cleaned up after fresh probe
enumeration and verifying the owned adapter's lab address. Only that lab
peer was removed from BlueZ, the owned adapter powered off, and those exact
groups terminated. Unrelated hci0/hci1 adapters were not changed. At pause:

- no owned `btattach`, `btmon`, BAP sender or raw-HCI helper remains;
- serial-mcp has no open connections;
- the temporary lab HCI index is gone;
- source retains async-r5 HCI firmware;
- receiver retains its pre-session firmware, not the new Mode A repair;
- host lab-peer cache was removed; receiver may still retain its bond, so
  fresh tests must clear both sides through the established procedure.

Do not assume the old HCI index or tty paths on resume. Re-enumerate and match
fresh identity. Raw identity/roles remain in the external PB-033 session
manifest and the copied per-run records, not in a permanent mapping here.

## Diagnostic tools, not production harnesses

These scripts were created under `/tmp/opencode` and are checkpointed:

- `pb019-hci-probe.py`: source/receiver identity verification, raw HCI
  preflight, owns btattach/btmon, runs BAP cases and captures receiver UART.
  `PB019_BLUEZ_DISCOVERY=1` uses BlueZ fresh pairing;
  `PB019_MODE=mono|modea|modeb` limits diagnostics to that mode plus reconnect;
  `PB019_QUALITY=1` adds midstream read-only ISO quality queries.
  Default without a mode filter runs all six cases.
- `pb034-board-modea.py`: **flashes the session-bound source only** and
  captures a requested banner. `--no-banner` is flash-only, not test success.
  It was reused for module tests, calibration and HCI images.
- `pb019-core-probe.py`: identity-checked SWD halt/read/resume diagnostics.
  Treat arbitrary Tcl arguments carefully; its error-path resume guarantee
  needs repair before general reuse.
- `uart-log-text.py`: decodes retained raw UART bytes without changing them.
- `pb034-compare-payloads.py`: compares temporary BSim plaintext TX/RX traces.

They reference the immutable session manifest
`/tmp/opencode/hil-sessions/pb033-xiao-proof-20260922-r1/devices.json`, hash
`fff051aaa95e2dfbe0b19c5a3353aed584d2c591b0307541a5db977bad72a86d`.
These are useful reproducible probes, **not** finished public helpers:
outer termination can bypass finally; subprocess output is buffered;
some observer-thread failures are not propagated; per-mode counter acceptance
is not fully automated. Harden lifecycle and result reporting before adopting
them as mandatory infrastructure.

## Resume order

1. Read `AGENTS.md`, this file and `pb-034-primary-repair-results.md`.
   Inspect `git status`, branch, pending files and backlog. Do not begin from
   the old temporary worktree or recreate already-completed repairs.
2. Check whether `/tmp` evidence/tools survived. If not, use the private
   checkpoint. Confirm SDK v3.3.0 and toolchain `911f4c5c26`. Runtime probe
   command is **`nix-nrf probes`**, not the obsolete `nrf-probes` executable.
3. Preserve the interrupted run as partial. Revalidate the two board roles,
   use a new evidence directory, and first classify the Mode A reconnect
   counter increase under 5/20 QoS. Finish the missing Mode B reconnect and
   rerun the complete six-case matrix when the candidate is stable. Source
   writes alone are insufficient: validate receiver counters and warnings.
4. Decide the retained QoS/transport configuration from evidence, not by
   reducing limits. Update endpoint tests and public-boundary reconnect tests.
   Rerun parser, H4, calibration, session and full unit suites. Retain exact
   firmware/build/source provenance for the final candidate.
5. Turn the HCI prototype into supported build/flash/reset/attachment helpers
   with runtime role identity, cleanup and timeout tests. Existing
   `fw-build-dongle`, `fw-flash-dongle`, `fw-reset-dongle` **still target the old
   nRF5340 setup**; do not use them assuming migration is finished.
6. Complete PB-035/PB-036: promote the nRF54 standalone source across all
   mandatory HIL rows and source artifact/capture contracts. Restore/build
   the standalone source firmware when switching away from HCI role. PB-033
   only proved its original three 10 ms rows, not the complete replacement
   matrix. Preserve 7.5 ms, lifecycle, fault and frozen transport acceptance.
7. Complete PB-037 legacy receiver deletion with graph/source impact checks,
   meaningful test retirement, build-contract/test-matrix updates and explicit
   coverage-population accounting. Do not delete generic loss/concealment
   protection just because historical comments mention nRF5340.
8. Complete PB-034 current docs and PB-038 exhaustive active-reference audit.
   Run the final software/build/coverage/BSim gates and physical acceptance on
   the actual new receiver image. Clean-commit coverage cannot be claimed from
   current dirty-tree diagnostics. Do not alter the release draft or publish.

Useful existing validation commands:

```sh
nix develop -c python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
nix develop -c python3 tests/unit/bsim_target/test_nrf54l15bsim_contract.py
nix develop -c python3 tests/unit/hci_h4/test_hci_h4.py
nix develop -c python3 tests/unit/bap_central_endpoint/test_bap_central_endpoint.py
nix develop -c python3 -m pytest -q tests/unit/lc3_pcm_calibrate
nix develop -c bash tests/fixtures/lc3/generate_stateful_references.sh
nix develop -c bash scripts/test-all.sh --phase unit
nix develop -c bash scripts/test-coverage.sh --report-only --output /tmp/opencode/NEW-COVERAGE-DIR
nix develop -c env BSIM_LOG_ROOT=/tmp/opencode/NEW-BSIM-DIR bash scripts/bsim-stage1-run.sh
nix develop -c backlog doctor
git diff --check
```

Use fresh output directories; do not overwrite prior evidence. BSim now takes
longer than a 120-second tool call for all 26 successful runs. Do not confuse
an execution timeout with a scenario failure, and do not reuse a partially
written run root for a retry.

## Command supervision note, for later

The user deprioritized implementing a command runner during this pause. No
plugin/MCP or wrapper was installed. Short harmless tests established:

- GNU `timeout` and systemd 260.4 are installed.
- A user systemd service enforces runtime limits for unprivileged detached
  children, but **could not kill root sudo descendants** on this host.
- A **system-manager** transient service with `User=thomas-workstation`,
  `Type=exec`, `RuntimeMaxSec=2s`, `TimeoutStopSec=1s`,
  `KillMode=control-group` killed both a TERM-ignoring user parent and detached
  sudo/root child. Result was `timeout`; the control group became empty.
- Therefore prefer a system-manager supervisor running the main job as the
  ordinary user for this lab workflow, plus live file/journal output and
  explicit start/status/log/stop commands. Do not run the whole Python job as
  root. Generic process-group timeouts alone do not contain `setsid()` children.
- Supervising process lifetime does not replace device-specific orderly
  teardown, identity checks, evidence finalization, or distinguishing timeout,
  cancellation, test failure and success.

All harmless smoke-test processes were stopped. No background test should be
assumed to be continuing across this pause.
