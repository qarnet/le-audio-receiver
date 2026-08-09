# Firmware release FR0 handoff

## Goal

Create the accepted implementation plan for publishing verified, factory-flash
firmware through GitHub Releases. Update the public future-feature backlog to
separate this work from future MCUboot/DFU research.

This phase changes documentation only. It does not add CI, packaging code,
version files, tags, releases, MCUboot, or firmware behavior.

## In scope

1. Add `docs/development/firmware-release-plan.md` as plan of record.
2. Update `PLANNED_FEATURES.md` item F to describe factory-image releases and
   link the plan.
3. Add a future MCUboot/signed-DFU item to `PLANNED_FEATURES.md` after item H.
4. Commit this handoff with those files.

## Out of scope

- GitHub Actions workflow implementation.
- Release packaging scripts or schemas.
- `VERSION` or release-version selection.
- Firmware builds, flashing, or hardware tests.
- MCUboot, MCUmgr, SMP, image signing, partition changes, or key generation.
- Automatic publication of a GitHub release.
- Changes to current factory flash scripts.

## Grounding evidence

- Current NCS version is `v3.3.0` in `flake.nix`.
- Official build container tag `v3.3.0` exists at
  `ghcr.io/nrfconnect/sdk-nrf-toolchain:v3.3.0`; verified manifest digest:
  `sha256:f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276`.
- Official GitHub Actions container guidance is
  `/home/thomas-workstation/ncs/v3.3.0/nrf/scripts/docker/README.rst`; Bash must
  be the job shell so toolchain environment variables load.
- Current release inputs:
  - nRF5340: `build/nrf5340/merged.hex` and
    `build/nrf5340/merged_CPUNET.hex`.
  - nRF54L15: `build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex` and
    `build/nrf54l15/flpr/zephyr/zephyr.hex`.
- Current image measurements from the accepted build:
  - nRF5340 cpuapp: 375,528 bytes; cpunet: 146,780 bytes.
  - nRF54L15 cpuapp: 531,784 bytes; FLPR: 32,668 bytes.
- `scripts/bin/fw-flash-5340` requires both nRF5340 core images through the
  OpenOCD dual-core chain.
- `scripts/bin/fw-flash-54l15` requires separate cpuapp and FLPR images.
- NCS 3.3.0 supports nRF54L15 MCUboot for cpuapp only; stock FOTA does not
  update this repository's FLPR image.
- NCS 3.3.0 supports nRF5340 app/net multi-image MCUboot, but an internal-only
  layout needs two app slots plus a 256 KiB netcore secondary region. Current
  cpuapp size already exceeds the approximate resulting app-slot budget before
  MCUmgr is added. This must remain a measured research question, not a claim
  of DFU support.

## Required release-plan decisions

The plan must make these decisions explicit.

### Product boundary

First release track publishes factory-flash firmware only. MCUboot and DFU are
a separate future track and do not block useful release artifacts.

### Artifact contract

One ZIP per receiver target:

```text
le-audio-receiver-vX.Y.Z-nrf5340-e83-factory.zip
  merged.hex
  merged_CPUNET.hex
  release-manifest.json
  SHA256SUMS
  FLASHING.md

le-audio-receiver-vX.Y.Z-nrf54l15-xiao-factory.zip
  cpuapp.hex
  flpr.hex
  release-manifest.json
  SHA256SUMS
  FLASHING.md
```

The plan must require target-local manifests to identify image role, original
build path, packaged filename, byte size, SHA-256, required flash order,
project version, Git commit, NCS version, board target, and schema version.
Companion images form one indivisible release tuple; users must not mix files
from different versions.

Top-level `SHA256SUMS` attached to the GitHub release must hash the downloadable
ZIP files so users can validate downloads before extraction.

Debug bundles (ELF, map, resolved `.config`, and `zephyr.dts`) are separate
maintainer artifacts, never mixed into the public factory ZIP.

### Build environment

Use the official Nordic v3.3.0 toolchain container pinned by digest, not a
floating tag. Checkout `sdk-nrf` at tag `v3.3.0`, initialize/update its west
manifest, place this application inside that workspace, set Bash as job shell,
and invoke repository build helpers. Do not install J-Link in build-only CI.

### Release lifecycle

- Pull requests, `main`, and manual dispatch build and upload workflow
  artifacts.
- A `v*` tag creates a draft GitHub release with exact CI-built artifacts.
- GitHub-hosted CI never publishes the draft automatically.
- Maintainer downloads exact draft attachments, flashes those bytes, runs
  software and hardware acceptance, records results, then manually publishes.
- Failed hardware acceptance leaves the release draft unpublished.

### Hardware acceptance

Require the canonical software gate first. Then flash packaged files from an
extracted release ZIP, not files from a local build tree.

nRF5340 acceptance:

- both app and net core images flash;
- clean boot, settings load, advertising;
- mono, Mode A, and Mode B at 10 ms plus supported 7.5 ms paths;
- disconnect and reconnect;
- zero unexplained warnings, decode errors, I2S underruns, and stream resets.

nRF54L15 acceptance:

- cpuapp and FLPR images flash;
- clean boot, FLPR ACTIVE, settings load, advertising;
- mono, Mode A, and Mode B at 10 ms plus supported 7.5 ms paths;
- FLPR healthy path and accepted fallback/recovery checks;
- pairing button/LED acceptance;
- disconnect and reconnect;
- zero unexplained warnings, decode errors, I2S underruns, and stream resets.

Both targets use the autonomous repository central. Audible confirmation is
the only permitted human observation.

## Required phase structure

Plan phases must be small and sequential.

### FR1: package contract and packager

Add deterministic, stdlib-only packaging logic plus public-boundary tests.
No CI and no version choice yet. Packager takes version, commit, NCS version,
build root, and output directory explicitly. Missing/malformed inputs fail
atomically with no partial final ZIP. Repeated identical input produces
byte-identical ZIP output.

### FR2: reproducible firmware build CI

Add pinned-container GitHub Actions build, exact NCS workspace setup, both
production receiver builds, build-contract verification, package generation,
artifact upload, and checksums. Add a root Zephyr `VERSION` only after the next
release version is chosen.

### FR3: draft release publication

Add tag/version consistency checks, protected `contents: write` release job,
draft creation, release attachments, and provenance. Never auto-publish.

### FR4: exact-artifact hardware acceptance

Add a release-candidate acceptance procedure and evidence template. Validate
exact draft assets on both targets. Do not rebuild between download and test.

### FR5: first useful release and closeout

Update public flashing/user docs, record evidence, publish manually, and verify
release download/checksum/flash instructions from a clean machine.

### Future DFU track

Keep MCUboot/DFU outside FR1-FR5. Require an ADR before implementation. The ADR
must resolve companion-image compatibility, signing-key custody, rollback,
power-loss behavior, settings preservation, downgrade policy, and transport.
BLE SMP in a physically gated DFU mode is the preferred transport hypothesis,
not an accepted implementation.

## `PLANNED_FEATURES.md` requirements

Item F must say factory artifacts are first and link
`docs/development/firmware-release-plan.md`. Keep public-release acceptance:
a user downloads, verifies, and flashes without building source.

Add item I, "MCUboot and signed firmware update", with status Research. It
must state:

- nRF54L15 stock MCUboot updates cpuapp only, not custom FLPR;
- nRF5340 full app/net update is supported by NCS but internal-flash budget is
  currently unresolved for this firmware;
- no DFU support claim exists yet;
- an ADR and target-specific prototypes are required;
- acceptance includes signature rejection, rollback, interrupted transfer,
  power-loss recovery, companion-image compatibility, settings/bond
  preservation, and post-update audio.

`PLANNED_FEATURES.md` is user-facing and must contain no U+2014 em dashes.

## Verification

- Read the complete new plan and changed backlog sections.
- `git diff --check`
- `rg -n '—' PLANNED_FEATURES.md` returns no matches.
- Verify every path, artifact name, size, container digest, and NCS limitation
  against the evidence above.
- Inspect `git status`, `git diff`, and `git log --oneline -10`.

## Commit and return

Stage only:

- `docs/development/firmware-release-fr0-handoff.md`
- `docs/development/firmware-release-plan.md`
- `PLANNED_FEATURES.md`

Commit with message:

```text
docs: plan firmware release pipeline
```

Do not push, merge, open a PR, create a release, or modify tags. Return files
changed, verification results, commit hash, blockers, deviations, and suggested
FR1 follow-up. Stop and escalate before committing if repository or SDK
evidence contradicts this handoff.
