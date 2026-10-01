# PB-037/PB-038: final platform reference and generated-output audit

## Scope and evidence

Source implementation checkpoint: `b21c7a762b35a127708b4c41a8d537a66c5be5e3`.
Primary tree also contains pending documentation and the preserved, unstaged
PB-013 user refinement. This record does not recast that dirty documentation
tree as clean firmware provenance. Physical images and canonical software were
produced from separate clean exact-commit validation checkouts.

Detailed literal ledger, per-file content hashes, matched line numbers, generated
output hashes and audit script snapshot:
`/tmp/opencode/nrf54-migration-audit-20261001-r2/audit.json` and
`audit-script.py`. It inspected **990 tracked or explicitly intended pending
documentation files**, found **265 files with retained legacy references**,
and left **zero unclassified files**. Initial r1's five script files requiring
manual review remain in that earlier report; r2 records their resolved classes.
New closure documentation may add further references without adding a target.

Search terms: `nrf5340`, standalone `nrf53`, standalone `E83`, `ebyte`,
`cpunet`, and `hci_ipc`, case insensitive. SHA substrings are not board matches.
This literal scan is not a proof of arbitrary program behavior. Positive target
selection was separately inspected in root build/sysbuild/Kconfig, board files,
source/HCI applications, simulator peers, executable helpers, fixtures, artifact
schemas, CI, build contracts and resolved outputs. Existing public-boundary
rejection tests and actual clean builds/gates supply behavioral evidence.

No positive nRF5340 board, hardware role, build/flash helper, sysbuild image,
simulator target, source archive, release target, fixture default or CI job
remains. nRF5340 implementation files are Git-deleted, not merely unused.

## Retained-reference disposition

The detailed ledger identifies each retained file rather than asserting that
all documentation is historical. Important distinctions:

| Class | Witnesses and interpretation |
| --- | --- |
| Unsupported-input tests | `tests/unit/build_contract/`, `tests/unit/bsim_target/`, `tests/unit/hil_source_target/`, `tests/hil/rh2_test.py`, `tests/hil/rh4_artifact_test.py`, `scripts/test_hil_runner.py`, HCI helper tests and firmware-CI tests contain old targets as rejected inputs. Removing these strings would remove retirement regressions. |
| Portable test-local history | Historical APLL and sample-adjust sources live under their dedicated `tests/unit/` directories. They execute as host tests, not nRF5340 firmware, and are not production actuator choices. |
| Historical algorithm/SDK origin | `Kconfig`, `src/audio_iso_seq.{c,h}`, `src/audio_modea.c`, `dongle/hci_identity.h`, source TX/app headers describe older omission, starvation, identity or scheduling observations. No old board selection or CPUNET clock backend remains. |
| Current generic behavior with historical motive | `scripts/bap_central_security.py`, `scripts/bap_central_writer.py` and `scripts/hci_raw_connect.py` preserve optional exact-peer and ordered teardown behavior. Their old-controller names occur in comments/docstrings, not hardware selection. |
| Explicit release exclusion | `scripts/package-firmware-release.py` and `scripts/prepare-draft-release.py` state that the old receiver release target is eliminated. Real package/resolver tests and the new one-target archive inventory prove current behavior. |
| Explicit helper rejection | `scripts/bin/fw-flash-hil-source-54l15` rejects legacy CPUNET/J-Link overrides before target-changing actions. It does not expose a legacy source branch. |
| Historical/amended plans and immutable evidence | Dated results, `docs/design.md`, completed refactor/test records, the amended System HIL plan, old coverage records and AGENTS/STATUS historical sections retain original hashes, warnings and failures. Current amendments identify replacement workflows; old recipes are not revived. |
| Public unsupported/external context | Retired technology/flashing/wiring appendices remain explicit history. Nordic platform recommendations and unverified third-party Audio DK material are external context, not repo hardware support. Public HCI DK research now explicitly says the old project fixture is unsupported. |
| Product history and supersession | Completed/archived policy history remains unchanged. PB-018 is explicitly superseded by PB-019/PB-039, not marked as a successful DK qualification. Migration items retain old Problem sections and immutable notes; current evidence and summaries own their disposition. PB-020's Audio DK reference is external transmitter research, not a repo source/controller build. |

## Generated and archived outputs

The clean `b21c7a7` physical checkout at
`/tmp/opencode/nrf54-b21c7a7-clean/le-audio-receiver` supplied **12 resolved
outputs**: `.config`, `zephyr.dts`, and generated `autoconf.h` for receiver
CPUAPP, receiver FLPR, standalone source CPUAPP and HCI CPUAPP. Audit found
**zero enabled/positive legacy selections**. Disabled SDK declarations, where
present, do not make an old SoC active. Resolved receiver build contract passed
**73/73**. Source/HCI remain single CPUAPP images; receiver remains CPUAPP/FLPR.

Canonical simulator binaries are the two exact nRF54L15BSim CPUAPP peers:

```text
bs_nrf54l15bsim_nrf54l15_cpuapp_le_audio_receiver_bsim_prj_conf
bs_nrf54l15bsim_nrf54l15_cpuapp_bsim_client_bsim_prj_conf
```

Their hashes are in the audit ledger; the clean canonical log retains their
build/selection and all 17 scenarios/26 runs. Physical compiler/configuration
outputs and archive inventory are independent of these simulator binaries.
The strict PCM oracle, source payload hashes and lifecycle cases are unchanged.

New exact local receiver/source archives at
`/tmp/opencode/nrf54-b21c7a7-artifacts-20261001-r1/validation.json` contain
only the receiver CPUAPP/FLPR and source CPUAPP contracts. Public extraction,
checksum, metadata and staged-image revalidation passed; legacy multi-image,
wrong-board/version/provenance and modified members remain rejected in tests.
These are local acceptance candidates, not active GitHub draft assets.

Current production numeric coverage contains **36 files**, with baseline
enforcement passing at the exact clean commit. The retired production APLL
path is absent; its test-local history remains. The earlier 37-file baseline
statements in dated records are not today's coverage population.

Tracked compile-database links select nRF54L15 or portable host trees. Clean
builds changed only four generated link targets; those exact link-only deltas
were inspected and restored in verification clones without altering firmware
outputs. No broad dirty-tree reset or modification of user edits was used.

Gitignored old builds, stale local fixture files, private snapshots and old
SDK source support are excluded from active selection and are **retained**,
not deleted to make searches empty. No active helper/default points at them.
Private raw identity/RAM/bond evidence must never be committed or published.

## Capture and release boundaries

Mono/stereo logical fixtures and their public model/helper tests now bind
XIAO source CPUAPP and immutable paired sessions. Fresh clean HIL host evidence
uses those migrated contracts with unchanged recipes and thresholds. This
satisfies migration of capture infrastructure, **not qualification of physical
analog capture hardware**. PB-023/PB-025 own analog electrical, measurement and
MA1/SA1 acceptance. `capture_capability` still means ALSA analog capture;
digital I2S windows never relabel it mono/stereo or stand in for analog proof.

PB-041 nonce identification and PB-013 360-frame FLPR support remain separate
Backlog work. Native tests are portable host tests, not nRF5340 dependencies.
Historical release archives and the existing private draft are not rewritten.
Exact active draft FR4 and publication remain separate from the local-artifact
matrix used to complete platform migration.
