# Full documentation-hygiene cleanup handoff

Base commit: `72abbc4` (approved behavior fixes + clean canonical 62/62).
User explicitly approved a full-repository documentation baseline audit and all
confirmed fixes. This handoff performs prose/comment/agent-skill cleanup only;
no further behavior change is allowed.

## Current executable truth

- canonical gate: 62 children = 35 Twister + 5 exec-only + 19 Python +
  coverage + matrix + BSim;
- coverage population 36, 4665/5121 lines, 2023/2820 branches, 357/357
  functions;
- build contract 95/95;
- BSim 17 scenarios / 26 runs;
- user pairing P1–P8 accepted, nRF54L15 enabled, nRF5340 feature-off;
- hardware P8 physical observations were explicitly confirmed by user in
  Orchestrator conversation: short press LED off, 3 s slow blink, 8 s rapid
  then slow. Instrumented ordering for RESET came from CLI using same owner, so
  docs must describe combined evidence rather than claim instrumented physical
  button ordering where none exists.

## Required active-doc corrections

- `README.md`: current inventory/gate counts 35/56/62 and current pairing
  status; remove stale 31/52/55.
- `STATUS.md`, `AGENTS.md`: current top-level state must say gate 62, population
  36, contract 95/95 while keeping dated R10 figures explicitly historical.
- `docs/design.md`: stop calling R10 55/33/79 state current authoritative;
  retain as historical refactor baseline and point current state to P8/STATUS.
  Update current BSim claims 16/25→17/26 and remove “no expansion planned”
  contradiction.
- `docs/development/workstation-transfer-status.md`: mark historical snapshot;
  point current state to P8/STATUS.
- `docs/testing/coverage-matrix.md`: coverage inventory 35+5, gate 62, lifecycle
  33 tests, and all current suite counts.
- `docs/testing/behavior-contract.md`: lifecycle 33, timing_nrf54 21,
  audio_shell 15, audio_shell_nrf54 43, gate 62 and current behavior.
- `docs/testing/t4-bap-bsim-matrix.md`: current runner 17/26 while preserving
  dated T4 origin clearly historical.
- `docs/development/user-pairing-control-p8-results.md`, plan, README, STATUS:
  qualify physical-button acceptance accurately: user confirmed threshold/LED
  observations; CLI produced instrumented RESET ordering via same owner. Remove
  fabricated-sounding “user requested CLI” and unexplained attribution. Record
  that exact instrumented physical-hold ordering was not captured in same run,
  without erasing valid user confirmation or technical evidence.

Historical handoff/result counts may remain when tied to dated revision/run.
Do not globally replace old numbers in historical evidence.

## Production/source comment corrections

Concrete contradictions:

- `src/flpr_ring.h`: remove obsolete sentinel-slot/full-at-N-1 statement;
  monotonic counters use all slots/full at N.
- `src/audio_timing_none.c`: nRF54 path is GRTC+TIMER20+GPPI PCLK measurement,
  not LRCK.
- `src/audio_perf.h`: deadline comparison occurs in cycle-end recording, not
  snapshot/print.
- `src/flpr/main.c`: describe current FLPR ASRC/IPC/runtime responsibility, not
  Stage 0/1 chronology.
- `src/audio_asrc.c`, `src/audio_i2s.c`: replace `Commit:` history wording with
  current state-update invariant.
- `CMakeLists.txt`: `fw-probes`→`nrf-probes`.
- `scripts/test-all.sh`, `scripts/test-coverage.sh`: remove static stale counts
  and R8/R9 narration; state dynamic inventory behavior and current gate only
  where generated truth is available.
- `scripts/check-build-contract.py`: remove stale release-default-off and
  pending-P8 wording.
- `tests/unit/lifecycle/src/test_lifecycle.c`: rename/rewrite test claiming
  removed `l_received/r_received`; describe only lifecycle close/reopen behavior
  actually asserted. Update matrix witnesses if test name changes.

Across production/shared headers and implementation comments, remove phase/
commit/handoff chronology (`R1`…`R10`, `P1`…`P8`, `T4`, `Phase`, `Stage`) when
it is narration rather than stable domain terminology. Preserve and rewrite
actual ownership, concurrency, ordering, protocol, hardware, compatibility,
and error rationale. Prioritize:

- pairing_mode, user_pairing_io, bt_bap_pairing_adapter, main;
- audio_stream_session, stream_lifecycle;
- flpr_acceptance, flpr handshake/control/ring/runtime and shared headers;
- audio timing/drift/offload and board config/overlay comments.

Public headers must describe caller contract, not migration history. Do not
remove stable command names (`Stage 1` as public BSim gate name may remain) or
historical result docs.

## Agent-skill corrections

Using OpenCode skill conventions, update repo-local skills without broad AGENTS
restructure:

- `.agents/skills/commit-and-push/SKILL.md`: current `fw-build-5340` repo-root
  command/artifacts and autonomous central-only verification rule.
- `.agents/skills/monitor-and-analyze/SKILL.md`: E83 `/dev/ttyUSB0`, current repo
  path, `fw-build-5340`/`fw-flash-5340`, no global `pkill -9`, OpenOCD-only
  reset/recovery policy, never normalize underrun warnings, no nrfutil recovery.
- `AGENTS.md`: counts/current state only; retain operational detail.

Because skills are configuration-time inputs, final recap must tell user to
restart OpenCode for skill changes to take effect.

## Remaining script prose

Correct overclaims/stale refs without changing newly-fixed behavior:

- phase3 gate serial lifecycle, current main refs, atexit limitations;
- flpr hang/stall headers to exact enforced checks;
- hci_raw_connect fatal/cancel description;
- bap_central_security termination and no pre-split line refs;
- official BSim smoke exact >=100-SDU parser contract;
- fw-flash-54l15 separate-image path only;
- stage1 duplicate-release description matches transport-visible server
  rejection.

## Marker

After cleanup and all verification pass, create
`docs/development/documentation-hygiene.md` beginning exactly with managed
warning identity from documentation-hygiene skill. Baseline revision must be
the cleanup commit HEAD immediately before marker commit. Marker contains only
latest baseline state, no audit history.

## Verification

- stale-contract searches for all known old counts, removed APIs, chronology,
  temporary/exclusivity claims;
- link/path/symbol reference reconciliation;
- focused tests for renamed test/matrix witness and inventory;
- `python3 scripts/test_inventory.py --json`;
- build contract 95/95;
- canonical clean-tree gate 62/62;
- `git diff --check`;
- final worktree clean except marker before marker commit.

No behavior edits, baseline JSON changes, BSim pin changes, hardware, flashing,
push, PR, amend, force-push, or attribution footer.

## Commit shape

1. This cleanup handoff.
2. Current docs/count/P8 evidence corrections.
3. Production/test/script comment hygiene.
4. Repo-local agent-skill corrections.
5. Full-audit results doc (not receipt) + any final reconciliations.
6. Managed baseline marker commit referencing commit 5.
