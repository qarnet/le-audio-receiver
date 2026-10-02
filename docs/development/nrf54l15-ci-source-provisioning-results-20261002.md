# PB-040/PB-039: durable CI source-provisioning repair

## Failure and scope

PR #15 run `37041093569`, head `106a13b`, failed `test-unit` with
**76 PASS / 1 FAIL / 77 TOTAL**. All four original `bsim_components` tests
failed in `setUp`, before compiler-policy checks, because
`/home/runner/ncs/v3.4.1/tools/bsim/components/libUtilv1/src/bs_oswrap.c`
was absent. The unit SDK cache missed; NCS disables the optional `babblesim`
group by default. Only the BSim worker fetched that group. Coverage completed
successfully; the firmware job was skipped by its unit dependency. This is
a dependency-contract defect, not an observed packet-delivery failure.

The earlier workflow contract test explicitly forbade source population in the
unit worker. Local SDKs already contained the sources, masking the cold-runner
assumption. No source hash, compiler warning policy, firmware behavior, codec
recipe, coverage baseline or hardware acceptance limit is relaxed by this fix.

## Durable contract

- `scripts/prepare-bsim-sources.sh` is the shared source-only preparer for unit
  and BSim workers. It checks the west workspace against the declared SDK,
  resolves projects through the pinned imported manifest with
  `west update --narrow -o=--depth=1 --group-filter +babblesim`, then checks
  every audited source. It does not compile components or install SDKs itself.
- The unit preparation step is unconditional after SDK install/cache restore
  and exact SDK/toolchain verification, before the unit gate. A cache hit or
  existing SDK directory cannot substitute for source readiness. The BSim
  worker uses the same preparer before its separate component build.
- `scripts/bsim-component-cc.py --check-sources COMPONENT_ROOT` shares the
  existing five SHA-256/diagnostic entries with compilation checks. Missing,
  modified or symlinked audited source paths fail closed before tests/builds.
  Ordinary compiler invocations retain `-Werror`, the same narrow exceptions,
  source re-audit failures and child exit-status propagation.
- Only the BSim matrix variant compiles components. Coverage and firmware
  remain independent of this optional source dependency. Cache keys and SDK
  pins are not bumped merely to hide the missing dependency.

## Behavior proved locally

`tests/unit/bsim_components/test_bsim_components.py` now runs real west and Git
against an offline local Git remote pinned by a manifest revision. It copies
the installed SDK's actual audited source bytes into temporary fixtures, never
into the repository. The test input is an already bootstrapped SDK/cache with
west metadata and absent optional projects, not a mock SDK installer.

The cold test first runs the real compiler-policy suite and observes missing
source failures, then invokes the actual preparer and runs that suite to
success. Additional cases prove partial cache hits, repeated preparation,
changed/missing pinned sources, wrong workspace, failed fetch and symlink
rejection. Workspace paths contain spaces. Installed SDK files are read-only
during these local tests; no SDK-source patch or local SDK population ran.

The workflow regression test rejects the actual failed `106a13b` workflow
because it lacks unit source provisioning. Updated declarative checks require
unconditional unit preparation after exact environment verification and before
the unit gate, while retaining BSim-only component compilation.

Validation:

| Command/boundary | Result |
| --- | --- |
| Focused component/preparation and workflow tests | 48 tests, OK (11 component/preparation, 37 workflow/version/release) |
| Failed-workflow red control | New unit dependency contract rejects `106a13b` |
| `bash scripts/test-all.sh --phase unit` | 77 PASS / 0 FAIL / 77 TOTAL |
| Source-only preflight on installed SDK | All five existing hashes verified, no write/build |
| `bash -n scripts/prepare-bsim-sources.sh`, `git diff --check` | Pass |

Raw focused log: `/tmp/opencode/migration-ci-source-focused-20261002-r2.log`,
SHA-256 `e82a737e93321f58ae84416abfe3227845b524bf1268c42bcf68e6dc464ff8bf`.
Full local unit log: `/tmp/opencode/migration-ci-unit-phase-20261002.log`,
SHA-256 `c31d8461ffc7442b843c7581f921536e7dae9256fab4d7bb523761734e16a25a`.
Red-control log: `/tmp/opencode/migration-ci-workflow-red-control-20261002.log`.
Adjacent JSON records retain exact commands, UTC bounds, return codes and hashes.

Hosted verification is pending the repair push. This does not rerun physical
hardware or recreate prior missing raw lab archives. The exact `104e67a`
physical firmware verification and its evidence-availability limits remain
separate. No release, publication or merge is performed.
