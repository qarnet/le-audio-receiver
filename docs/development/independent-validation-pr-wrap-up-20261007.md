# Independent validation PR wrap-up (2026-10-07 snapshot)

This snapshot records the verified completion state of PR 16 for the
independent-validation track. The backlog remains the sole live ledger; this
document is a dated snapshot, not a new task list. Human merge of the PR is
the official acceptance act and has not happened.

## Scope

Branch `feature/independent-firmware-validation`, PR 16 (draft, OPEN,
MERGEABLE, CLEAN), merge base `origin/main` at
`0c9d2391f8532684e5014225b82749f0d930ce6e`, head
`c64cbcf37ac2b7e6b0060df0d854897a1c431522`, 27 commits on this branch in
this range. The metadata in this snapshot describes the PR state
immediately before this wrap-up's own docs-only commit; the actual final
head is whatever the live PR shows at read time, because a snapshot
commit cannot cite itself. The PR title carries the PB-045 prefix from
the original scope; the combined item scope was owner-approved and the
human product-owner merge remains the pending official acceptance.

## Hosted and local verification

Latest verification on the exact head `c64cbcf`: hosted run
[37468117709](https://github.com/qarnet/le-audio-receiver/actions/runs/37468117709)
with `test-unit`, `test-heavy (coverage)`, `test-heavy (bsim)`, aggregate
`tests` and `firmware` all SUCCESS and `release` SKIPPED (pull_request).
PR 16 is draft, OPEN and mergeable clean for the human product-owner
merge; no agent performs a merge and no release is published by agents.

Local verification on the completed source: full canonical gate
98 PASS / 0 FAIL / 98 TOTAL from a clean detached candidate at exact
head `e132fd274087fee3d78177b52304cfea20b96033`
(`docs/development/pb-051-native-probe-entry-results-20261006.md`
records the gate details); cold-cache native bsim phase 2/0/2 before the
canonical run; BabbleSim Stage 1 exactly 17 scenarios / 26 runs
strict-checked in both; the additive ASCS lane seals exactly 60 cases /
65 render phases / 259 raw exchanges / 269 response records per accepted
run. The frozen coverage baseline is unchanged
(SHA-256 `5bb01f95afc12c0771086a537cb70c92d20f7d96c8b9b4323528b6d9ed76de7a`)
with the frozen 36-file population exactly
5049/5491 lines, 2245/3036 branches, 378/378 functions; the additive
sidecar is exactly 13/13 lines, 12/12 branches, 1/1 functions. After
that gate, the only later source change was the test-only
portability repair in `tests/unit/ascs_runner/test_ascs_runner.py`
(focused 43/13 suites plus the green hosted proof; no full gate
rerun at the later head). The production receiver build proof is exact
at its recorded identity: the 73-assertion resolved build contract
passed (`scripts/check-build-contract.py --nrf54l15 build/nrf54l15`)
and the ARM GNU wrap proof
(`docs/development/pb-051-ascs-results-20261005.md`) shows both real
production `lc3_config` callsites bound through
`__wrap_bt_audio_data_parse` into the real SDK parser, all at the
unchanged `33310f0` production source identity; the hosted firmware
job rechecked the build contract on green runs and did not assert
byte-identical image equality on every run. The recorded board image
was last verified as the pure parser diagnostic image; its current
physical state is not freshly queried and no new board facts are
claimed.

## Completed items (5)

- PB-045 independent ASRC arithmetic and full-waveform oracle:
  `docs/development/pb-045-independent-asrc-results-20261003.md`.
  Production behavior fix: the signed negative-ppm rounding bias in
  `src/audio_asrc.c:compute_step` (the old code added a positive bias
  before C signed division, so negative deltas rounded toward the wrong
  tick; repaired and independently oracle-checked, no tolerance
  widened). FLPR audio here and below is the CPU software model, not a
  physical FLPR hardware claim.
- PB-046 closed-loop clock recovery with independent timed output:
  `docs/development/pb-046-clock-loop-results-20261003.md` and
  `docs/development/pb-046-clock-model-refinement-20261003.md`.
- PB-051 encoded ASCS rejection and lifecycle regression matrix plus the
  per-entry LTV guard:
  `docs/development/pb-051-ascs-results-20261005.md` and
  `docs/development/pb-051-native-probe-entry-results-20261006.md`.
  Second production behavior fix: the receiver root
  `CMakeLists.txt` now always links the repository-owned per-entry LTV
  guard through `-Wl,--wrap=bt_audio_data_parse`, forwarding individually
  valid entries to the real installed SDK parser and rejecting only the
  offending entry before the callback. GNU `--wrap` limits are explicit:
  same-object calls that resolve internally inside the SDK link unit are
  not intercepted (`docs/testing/ascs-protocol-regression.md`). The
  native capability-probe grammar (`scripts/native_bsim_probes.py`)
  belongs to this item: it recognizes exactly the four recorded-purpose
  Zephyr linker capability probe events with exact pinned SDK source
  hashes; every other compiler/Ninja/CMake diagnostic stays a hard
  failure. The earlier successful hosted run `37461484949` bsim artifact
  (downloaded and verified by the delegator) contains the four accepted
  dispositions, supported values `false, false, true, false`. The
  completion head's hosted run `37468117709` is verified green on all
  five required contexts via the run status; its bsim artifact was not
  separately downloaded or read.
- PB-052 external test result accounting:
  `docs/development/pb-052-external-result-accounting-results-20261003.md`
  and `docs/testing/external-test-result-accounting.md`. This item owns
  strict executed-result accounting of external runs; it proves the
  accountant, not the freshness of any particular external execution.
- PB-053 isolated Linux/BlueZ host regression lane:
  `docs/development/pb-053-host-lane-results-20261005.md`. The lane's
  manual 9/9 private-guest external pytest case passed on recorded
  retained evidence; it is not an automatically rerunnable canonical CI
  lane, and it proves no physical RF/codec behavior.

Not claimed complete: the other selected items below stay held or
dependency-held; the PR covers the five completed items above, not "all
selected items are done".

## Remaining holds (8)

- PB-041 fresh DUT/analyzer wiring identity and truthful DAC-presence
  evidence: waits for the incoming ADC model, its input limits and safe
  wiring review.
- PB-042 independent LC3 reference tooling and rights: waits for the
  owner's dedicated Windows laptop access/tool readiness and a separate,
  explicit rights/EULA decision; machine availability is not acceptance.
- PB-043, PB-044, PB-047, PB-048, PB-049, PB-050: dependency-held behind
  PB-042 (and the physical ones also need PB-041's fixture identity).

The backlog is the only live ledger; statuses there win over any snapshot.

## Preserved boundaries

- LC3plus remains excluded. Frozen PCM metric limits, HIL transport
  limits and canonical BSim pins are unchanged.
- No RF transmission, physical I2S/DAC/analog output, presentation phase,
  full LC3 decoder conformance, FLPR hardware offload execution or
  release/publish qualification is claimed by this track.
- Last recorded board image was the pure parser diagnostic image; the
  current physical state was not queried during this wrap-up and there
  is no current role/probe-to-board mapping, so any later physical
  action requires fresh identity and role provisioning first. No new
  hardware action happened in this wrap-up.
- External retained evidence availability was rechecked for selected
  files cited in the completed results docs; nothing guarantees every
  historical `/tmp/opencode` root stays available. The one execution
  deviation stays as recorded: the allocated prelaunch ASCS container
  `/tmp/le-audio-ascs.sTgQ0p` was reported removed after the prelaunch
  fatal missing-output-dir abort, the path is now absent and its
  contents cannot be re-verified; it is not claimed as a sealed failed
  record and not used for acceptance. No files were restored or
  fabricated to change that fact, and no further roots or logs are
  deleted.

## Merge expectation

Human product-owner merge of PR 16 is official acceptance. Everything in
this document is a snapshot record; the backlog and live status decide
what is current afterwards.