# Session state — RH3 transport investigation, SN_STRICT validation pending

Written 2026-09-04 (thinker session). Read this fully before continuing. It
replaces nothing; it is the working restart state for the RH3 track.

## Where the repo stands

- Branch: `feature/firmware-release-acceptance`
- HEAD: `719cb3d` (docs(hil): record TX pacing-regime isolation verdict)
- Worktree has exactly two intentional changes:
  - `hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf` —
    STAGED, UNCOMMITTED, UNTESTED: `CONFIG_BT_CTLR_ISOAL_SN_STRICT=n` with
    a comment block whose last line says "Validation run pending". This is
    the NEXT ACTION's change. Do not commit before it is validated on
    hardware.
  - `docs/development/system-hil-rh3-modea9-snstrict-handoff.md` —
    untracked; the ready-to-execute handoff for the validation run.
  - Also untracked, dormant: `docs/development/system-hil-rh3-modea3-source-isoq-handoff.md`
    (deferred design, blocked by an upstream Kconfig gap; do not implement).
- All HIL evidence roots are immutable under `/tmp/opencode/hil-runs/`.
- Committed this session (chronological): `3755d92` (HIL track landing +
  RH3a limits), `ea73413` (first matrix attempt), `f0ae6d8` (RH3b raw
  fallback), `13abf5e` (ModeA1 baseline), `eac880d` (offload-disabled
  runner mode), `df81981` (ModeA1b result), `d8a7ed2` (RH3c full-segment
  scan), `f1c13f0` (ModeA2 verdict), `f24ee6d` (Mode B control verdict),
  `0c3c9f7` (HILRX timing instrument + run), `c02f74e` (RX=6 verdict),
  `b71230f` (RTN=1 verdict), `000a960` (PHY=1M verdict), `719cb3d`
  (TX-out=6 verdict).

## The diagnosis (settled 2026-09-04, thinker analysis)

**The HIL source fixture's SW-split (Zephyr open-source) central controller
drops Mode B / Mode A ISO payloads because its ISO-AL `SN_STRICT` default
expires TX SDUs whose submission phase slips past CIS event preparation.
The HIL source host is completion-paced (`sem_tx_wake`, outstanding 3), so
at CIG event duty >= ~80% the buffer-release-to-refill phase slips; SN_STRICT
then marks the late SDUs' payload numbers expired; the central transmits
empty subevents; the peripheral flags every event LOST with zero CRC
errors. Under completion pacing the phase can never recover, so delivery
is permanently zero after ~1-2.5 s of progressive degradation.**

Load-bearing evidence (all recorded in committed result docs):

1. Mono 10 ms (120 B, `cig_sync=5304`, ~53% duty) passes perfectly:
   12644/12644, plc=12.
2. Every failing shape has higher duty: Mode B 7.5 ms (~72%, dead by seq
   24 = 180 ms), Mode B 10 ms 240 B (~82%, `cig_sync=8184`, dead by ~seq
   141), Mode A 10 ms dual CIS (~106%, slot 1 dead from seq 0).
3. Delivered fraction scales with duty margin: 2M/RTN5: 113; RX6: 109;
   RTN1 (nse=2, on-air verified): 130; TX-out=6 (out=5 verified): 137;
   1M (`c_phy=1` verified): 144 (full preamble); 7.5 ms: 24.
4. Source host exonerated every run: `sub=cb=12644, sc=12000, sf=0,
   first_errno=0` (all SDUs submitted, all callbacks fired).
5. Receiver exonerated by history with other centrals: SDC dongle passed
   Mode B 240 B (`SDUs=11659, plc=2`; 120 s and 300 s runs); Intel AX210
   passed 7.5 ms Mode B (BZ2 gate). Only the SW-split central fails.
6. Mechanism is documented in the installed tree:
   `zephyr/subsys/bluetooth/controller/Kconfig.ll_sw_split:643`
   (`BT_CTLR_ISOAL_SN_STRICT`, default y) — its help text describes exactly
   this: payloads "expired (will be dropped)" when submission does not
   strictly follow the SDU interval; disabling "could be shifted... less
   likely to be dropped."

### Corrected analysis (supersedes ModeA4's conclusion)

`docs/development/system-hil-rh3-modea4-rxtiming-result.md` records
"scored-onset correlation" as the supported branch. That conclusion is
WRONG: the HILRX per-second lines log `t=<k_uptime/1000>` (uptime since
boot), not time since streaming start; streaming began at uptime ~9 s.
Corrected profile: delivery was NEVER healthy (first 9 events LOST, 23%
LOST in the best second), degrades monotonically to zero over ~2.5 s of
streaming, and stays zero. The "last valid seq 140 vs scored onset seq
144" was a coincidence artifact; the 7.5 ms runs die at seq 24, far before
scored onset (preamble length is fixed in samples = 1.44 s wall in all
profiles, so scored onset does NOT align with a fixed wall time). Content
hypothesis is dead; duty/phase-slip is the consistent mechanism. A dated
correction note must be appended to that result doc (original observations
untouched) when the validation run lands.

### Exonerated variables (each with a runner-validated or build-proven run)

FLPR offload (ModeA2, offload disabled); receiver ISO RX pool 3->6
(ModeA5); central CIS layout policy (hci_ipc base config already sets
LOW_LATENCY — pre-exonerated without a run); RTN/subevent duty 5->1
(ModeA6, on-air `nse=2` sanity); PHY 2M->1M (ModeA7, `c_phy=1` sanity);
TX pacing regime target 3->6 (ModeA8, `out=5` sanity); scored-content
onset (ModeA4 corrected); source host starvation (counters every run).

## Why the fix is on the nRF5340 (user question, settled)

The nRF5340DK is the TEST INSTRUMENT (HIL audio source), not a product.
The 2026-09-03 decision eliminated the nRF5340 receiver product track; the
DK's only role is streaming audio TO the nRF54L15 so the nRF54L15 can be
validated. The failing component is the fixture's controller choice
(SW-split), so this is instrument calibration (like replacing a broken
probe cable), not nRF5340 validation. The receiver is untouched. User
explicitly agreed; long-term a second nRF54L15 as fixture device would
make sense (plan-revision + hardware purchase, not part of closing RH3).
Fallback lever if SN_STRICT=n fails: `CONFIG_BT_CTLR_ISOAL_PSN_IGNORE=y`
(trades stream alignment for delivery; USER DECISION REQUIRED before
running it). Alternative larger option discussed: switch fixture net core
from SW-split to SDC (production-grade controller that already passes
Mode B via dongle) — a small sysbuild.cmake rework, better long-term
fixture.

## NEXT ACTION (ready to execute)

Execute `docs/development/system-hil-rh3-modea9-snstrict-handoff.md`
exactly. Summary:

- One hardware run: `scripts/hil-runner.py run`, row
  `rh3.fresh_mode_b_48_4_1`, run ID `rh3-modeb-snstrict-20260904`
  (validated unused; both output paths absent). Source build uses the
  staged overlay (SN_STRICT=n); receiver is the normal current-HEAD build.
- Falsifiable prediction: Mode B delivery recovers to PASS under frozen
  limits (`rx_valid >= 11379`, `plc <= 5% of decoded`).
- On PASS: (1) update the overlay comment's "Validation run pending"
  clause to cite the run evidence, making SN_STRICT=n the fixture default;
  (2) write
  `docs/development/system-hil-rh3-modea9-snstrict-result.md` (canonical);
  (3) append the dated correction note to the ModeA4 result doc (see
  above); (4) update resume-state (run count, last flash identity,
  verdict); (5) commit exactly
  `fix(hil): relax fixture ISOAL strict sequencing to stop payload expiry`
  including overlay + handoff + result + ModeA4 correction + resume-state;
  report readiness for the full RH3 matrix attempt (separate phase, new
  run ID `rh3-matrix-<date>-2`).
- On FAIL same signature: record result doc with prediction-vs-outcome,
  leave the overlay line UNCOMMITTED, update resume-state, commit
  `docs(hil): record SN_STRICT validation outcome`, STOP for the
  PSN_IGNORE user decision.
- Constraints: runner owns all hardware; no manual target operations;
  actionable build warnings are errors (allowed: documented dirty-tree,
  nRF54L15 watchdog empty-library, global `__ASSERT()`); no NCS patches;
  no receiver firmware changes; no rows/limits/matrix changes; no
  evidence mutation; no retry; status 0/1/130 immutable. Build proofs and
  the exact runner command are in the handoff.

After a PASS, the queued sequence per the plan of record
(`docs/development/system-hil-milestones.md`):

1. Full RH3 matrix attempt (new run ID, `run-rh3-matrix`; 14 child runs:
   4 healthy 10 ms rows + reconnect + hang + stall, two passes) on the
   calibrated fixture.
2. If the matrix passes twice: `TRANSPORT_RUNTIME_ACCEPTED`, then RH4
   exact-artifact phase.
3. RH3-7p5 (7.5 ms rows) remains a named open phase: now likely the SAME
   mechanism (tighter duty -> earlier slip); retest after the fixture fix
   before any product decision.

## Environment / tooling notes for the fresh session

- The `executor` subagent type is intentionally DISABLED by user permission
  rules (task tool allowed in general; `executor` and `delegator` patterns
  denied). Do not attempt `subagent_type: "executor"`; either execute
  hardware phases directly under your own tool permissions (allowed) or
  ask the user.
- LSP noise: clangd/Pyright static diagnostics on firmware files
  (`hil/source/**`, `tests/bsim/**`) and on `scripts/hil/*` imports are
  host-parse artifacts (no Zephyr build context / no static package
  resolution): `gnu/stubs-32.h not found`, undeclared `CONFIG_*`,
  incomplete types, `"discovery" is unknown import symbol`. IGNORE them;
  never "fix" them in source. Real verification is west/Twister/py_compile/
  pytest. User is fixing the LSP config separately.
- CPUAPP image hashes are HEAD-dependent (`cmake/version.cmake` embeds
  APP_COMMIT); derive identities at execution time; double-build
  determinism proof for any flashed diagnostic image. Source CPUNET is
  stable (`4e4b82f5...`) unless the controller overlay changes — NOTE:
  the staged SN_STRICT change DOES change CPUNET; expect a new CPUNET
  hash after `fw-build-hil-source`.
- Serials: receiver XIAO nRF54L15 probe `8EE9B3FF` (console
  /dev/ttyACM2); source nRF5340DK J-Link `001050023938` (console
  /dev/ttyACM1). Resolve at runtime; never assume.
- Disk gate 80 GiB; last observed ~150-170 GiB free.
- The HIL runner CLI: `nix develop --command ./scripts/hil-runner.py run
  --fixture tests/hil/fixture.json --binding tests/hil/fixture.local.json
  --output-root /tmp/opencode/hil-runs --run-id <ID> --junit <ID>.junit.xml
  --row <row>`. Outer tool timeout 3600000 ms for single rows,
  10800000 ms for the matrix.

## Standing decisions (from the user, still in force)

1. nRF54L15 is the only production receiver target; nRF5340 release track
   eliminated; DK = fixture only.
2. 7.5 ms must work or must not be supported (RH3-7p5 phase owns this).
3. Receiver transport limits are frozen and runner-enforced
   (`rx_valid >= 90%`, `plc <= 5%`, zero-flags; RH3a).
4. Reruns are the fix-validation mechanism; blind unclassified retries
   prohibited; two consecutive unclassified same-row failures = stop for
   redesign review.
5. Commit work yourself (no attribution footers, no push, no merge).
6. Report back only on genuine blockers; otherwise keep executing the plan
   (feature-orchestrator loop discipline; handoff docs for every phase;
   every hardware run gets a canonical result doc + resume-state update).