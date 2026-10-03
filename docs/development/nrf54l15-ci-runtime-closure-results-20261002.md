# PB-034/PB-039: cold CI BabbleSim runtime closure

## Independent second failure

After repairing unit source provisioning, retained logs from run `37041093569`
showed a separate BSim failure. Each attempted PHY exited 255; receiver/client
timed out after 300 seconds. The first PHY log states:

```text
p_2G4: ERROR: (src/p2G4_channel_and_modem.c:94): ../lib/lib_2G4Channel_NtNcable.so: cannot open shared object file: No such file or directory
```

The canceled run's uploaded artifact is `11246031575`, archive digest
`22907083a5b444e36173ee909a594f23dc11a93cd33408bd59063c2ccf2cb38b`.
This is failed/partial evidence, not acceptance. The source-provisioning repair
at `bd6d3e1` separately passed hosted unit, coverage and firmware jobs in run
`37049429562`; that wave still lacks the runtime-closure repair.

Installed NCS v3.4.1 PHY defaults are `NtNcable` channel and `Magic` modem
(`tools/bsim/components/ext_2G4_phy_v1/src/p2G4_args.c:27-28`). The PHY loads
them through `dlopen` from `../lib`, before peer connection, so ordinary link
dependencies do not build them. The old helper built only the PHY's static
dependency closure, and the old environment check required only an executable.
Previously built local models masked this incomplete cold CI closure.

## Durable repair, no acceptance change

`scripts/build-bsim-components.sh` now requests the precise runtime closure:

```text
ext_2G4_phy_v1
ext_2G4_channel_NtNcable
ext_2G4_modem_magic
```

Forced rebuild enumeration covers both model components as well as existing
static dependencies. No build-everything workaround, model substitution, new
warning exemption or SDK-source patch is introduced. Actual cold fixture
builds compile/link both models with the existing strict compiler policy.

`scripts/check-bsim-runtime.py` checks required nonempty model files and launches
the real PHY from its proper `bin` directory with a unique diagnostic simulation
ID. It requires `main: Connecting...`, the installed source's marker after
`RTLD_NOW` loads, symbol lookup and model initialization. A pseudo-terminal gives
line-buffered output without host/PHY ELF-class assumptions from LD_PRELOAD.
Missing/corrupt models, loader errors, warnings, early exit or a deadline without
readiness fail. Timeout alone is never PASS. Its private process is terminated
and reaped on normal, error, deadline and catchable cancellation paths.
No simulator peers launch in this preflight.
SIGTERM/SIGINT record cancellation without asynchronous exceptions during child
spawn; ownership is established before cleanup. Signal-driven process-boundary
tests verify the owned PHY disappears after SIGTERM, SIGINT and kill escalation
when the child ignores SIGTERM.

Both the build helper and shared `bsim-env.sh` use this readiness boundary. The
environment therefore rejects broken runtime prerequisites before compiling or
launching receiver/client peers, rather than letting every row wait 300 seconds.
Normal Stage 1 seeds, default models, simulation lengths, 17/26 matrix, TX
hashes, PCM limits and lifecycle oracles are unchanged. Shared SDK component
builds remain BSim-worker-only; unit regression tests compile an isolated
temporary fixture closure, not the installed SDK output tree.

## Verification

- Real pinned source trees copied into a temporary, initially empty output;
  the public helper builds the actual PHY and both models and proves startup.
- Missing either plugin rejects the real runtime and prevents a shell caller
  from reaching its peer-launch marker. Restoring it restores readiness.
- A nonempty invalid model is rejected by the actual PHY loader, proving file
  existence alone is not acceptance. A live diagnostic process without the
  readiness marker times out as failure and is cleaned up.
- Existing log-root ownership test now expects the earlier runtime-preflight
  rejection for its incomplete fake SDK; it does not fake model readiness to
  reach a later environment check. Parser tests pass 146/0.
- Local full unit phase passed **77/0/77** after this fixture correction.
- Final focused component/runtime, workflow and target contracts pass **62
  tests, OK**, including signal cancellation and no-marker timeout controls.
- Unchanged local Stage 1 passed **17 scenarios / 26 runs** with strict oracles.
  This is local software evidence, not new physical hardware qualification.

Retained local logs:
`/tmp/opencode/migration-ci-runtime-unit-20261002-r2.log` (SHA-256
`cf43c27562d23008871706a95323b7994d4d8200174c4d1a8d4f5dd6b44c747e`),
`/tmp/opencode/migration-ci-runtime-stage1-20261002.log` (SHA-256
`bbc41bd8f9136f793b6b535db3150a3023a5bb707ff7d237e2cc199fef6904de`),
and `/tmp/opencode/migration-ci-runtime-stage1-20261002/` per-run logs.
The first local unit attempt failed only the outdated fake-runtime log-root
expectation; its raw failed log remains retained, not relabeled as a pass.
Final focused log: `/tmp/opencode/migration-ci-runtime-focused-20261002-r3.log`,
SHA-256 `7526063fec5512f076afdea70e999bec1e92d938ebe50bce95f9d42d37e43cba`.

Hosted verification of this second repair is pending its own push/run. Human
merge, analog qualification, active-draft FR4 and publication remain separate.
