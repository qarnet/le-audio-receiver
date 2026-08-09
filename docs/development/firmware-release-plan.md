# Firmware release plan

Status: accepted plan (FR0). Scope: publishing verified, factory-flash firmware
through GitHub Releases. This plan is documentation only; it does not add CI,
packaging code, version files, tags, releases, MCUboot, or firmware behavior.
Implementation phases FR1-FR5 are defined below.

## Goal

Publish factory-flash firmware binaries that public users can download, verify,
and flash without building from source. Separate this work from future
MCUboot/DFU research, which stays a distinct track.

## Product boundary

The first release track publishes factory-flash firmware only. MCUboot and
signed DFU are a separate future track and do not block useful release
artifacts. Factory-flash means the packaged images are flashed directly via
the probe/OpenOCD paths, replacing the firmware image in its address range,
with no bootloader update layer.

The current normal flash helpers (`scripts/bin/fw-flash-5340` and
`scripts/bin/fw-flash-54l15`) program firmware address ranges and preserve
settings and bonds; they do not perform a clean-state erase. Any destructive
clean-state or recovery procedure (for example a full chip erase) is
separate, target-specific, and must be explicitly documented and tested
rather than implied by the release package.

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

## Artifact contract

One ZIP per receiver target. The ZIP is the public release artifact and is
indivisible: companion images form one release tuple, and users must not mix
files from different versions.

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

Target-local `release-manifest.json` must identify, for every image:

- image role (which core and which function);
- original build path inside the CI workspace;
- packaged filename inside the ZIP;
- byte size;
- SHA-256;
- required flash order;
- project version;
- Git commit;
- NCS version;
- board target;
- schema version.

The manifest plus `SHA256SUMS` inside each ZIP protect extraction-time
integrity. A top-level `SHA256SUMS` attached to the GitHub release must hash
the downloadable ZIP files so users can validate downloads before extraction.

Debug bundles (ELF, map, resolved `.config`, and `zephyr.dts`) are separate
maintainer artifacts, never mixed into the public factory ZIP.

## Build environment

Use the official Nordic v3.3.0 toolchain container pinned by digest, not a
floating tag. In the CI job:

- checkout `sdk-nrf` at tag `v3.3.0`;
- initialize/update its west manifest;
- place this application inside that workspace;
- set Bash as job shell (toolchain environment variables load only in Bash
  sessions);
- invoke repository build helpers.

Do not install J-Link in build-only CI.

## Release lifecycle

- Pull requests and manual dispatch build and upload workflow artifacts.
- A trusted `main` push that changes the root `VERSION` file runs the same
  build/package path, then CI creates one draft GitHub Release with the
  exact CI-built artifacts. The draft's `tagName` and `targetCommitish`
  reserve `v<version>` at the exact main commit; GitHub does not create the
  git tag while the release stays a draft.
- Later `main` pushes that do not change `VERSION` skip release creation, so
  documentation or maintenance commits reuse the same version without
  touching a pending or published release.
- Maintainers must not push release tags manually; CI owns release
  initiation, and GitHub creates the lightweight tag at the draft's stored
  target SHA when the release is manually published.
- GitHub-hosted CI never publishes the draft automatically.
- Maintainer downloads exact draft attachments, flashes those bytes, runs
  software and hardware acceptance, records results, then manually publishes.
- Failed hardware acceptance leaves the release draft unpublished.
- After manual publication, verify the created lightweight tag:
  `refs/tags/v<version>` must point at the release's target commit (FR5).

## Hardware acceptance

Require the canonical software gate first (the repository test gate: twister
unit tests, build contract, and BSim Stage 1 where applicable). Then flash
packaged files from an extracted release ZIP, not files from a local build
tree. Flashing the packaged images preserves settings and bonds; acceptance
verifies settings load from preserved storage, and does not imply or require
a clean-state erase.

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

Both targets use the autonomous repository central (`scripts/bap_central.py`
attached via the nRF5340DK `hci_uart`). Audible confirmation is the only
permitted human observation.

## Phase structure

Phases must be small and sequential. Each phase lands independently with its
own evidence.

### FR1: package contract and packager

**FR1 ACCEPTED (2026-08-09)** at implementation commit `f3cd4c4`, with the
review-fix correction commit `1671a9f` (`fix: handle firmware packaging I/O
failures`); evidence in
`docs/development/firmware-release-fr1-results.md`.  FR4-FR5 remain planned.

Add deterministic, stdlib-only packaging logic plus public-boundary tests. No
CI and no version choice yet. Packager takes version, commit, NCS version,
build root, and output directory explicitly. Missing/malformed inputs fail
atomically with no partial final ZIP. Repeated identical input produces
byte-identical ZIP output.

### FR2: reproducible firmware build CI

**FR2 ACCEPTED (2026-08-09)** at implementation commit `a54e17d`, with
correction commits `117bc92` (`fix: upload packaged firmware from workspace`)
and `75a8093` (`fix: export Zephyr workspace to firmware builds`); successful
GitHub-hosted run `31326612845` (job `93277895593`) at PR head `75a8093`;
evidence in `docs/development/firmware-release-fr2-results.md`.  FR4-FR5
remain planned.

Add pinned-container GitHub Actions build, exact NCS workspace setup, both
production receiver builds, build-contract verification, package generation,
artifact upload, and checksums. Add a root Zephyr `VERSION` only after the next
release version is chosen.

### FR3: draft release publication

Add trusted-main automatic release initiation: a `main` push that changes
the root `VERSION` runs the accepted build/package path, then a protected
`contents: write` release job creates a draft release with provenance and
exact attachments whose `tagName`/`targetCommitish` reserve `v<version>` at
the exact main commit. Drafts stay untagged: GitHub creates the lightweight
version tag only when the draft is manually published after FR4. A main
push without a `VERSION` change skips the release job. CI never
auto-publishes.

**FR3 ACCEPTED (2026-08-09)** at implementation commit `8ef8a80`
(`ci: create draft releases from version tags`), with review corrections
`a3eef05` (`fix: validate draft release metadata checks`), `4892a6a`
(`ci: create release tags from trusted main`), `4c837af`
(`fix: verify untagged draft releases`), and `2532fea`
(`fix: detect existing draft releases`); successful one-shot draft creation
at trusted-main target `3d9a918...`; accepted merged state `b70b978...`;
evidence in `docs/development/firmware-release-fr3-results.md`.  FR4-FR5
remain planned.

### FR4: exact-artifact hardware acceptance

Add a release-candidate acceptance procedure and evidence template. Validate
exact draft assets on both targets. Do not rebuild between download and test.

### FR5: first useful release and closeout

Update public flashing/user docs, record evidence, publish manually, and verify
release download/checksum/flash instructions from a clean machine.

## Future DFU track

Keep MCUboot/DFU outside FR1-FR5. Require an ADR before implementation. The ADR
must resolve companion-image compatibility, signing-key custody, rollback,
power-loss behavior, settings preservation, downgrade policy, and transport.
BLE SMP in a physically gated DFU mode is the preferred transport hypothesis,
not an accepted implementation.

Known NCS 3.3.0 constraints that bound the DFU track:

- nRF54L15 stock MCUboot updates cpuapp only, not the custom FLPR image;
- nRF5340 full app/net update is supported by NCS but the internal-flash budget
  for this firmware is currently unresolved;
- no DFU support claim exists yet;
- acceptance must include signature rejection, rollback, interrupted transfer,
  power-loss recovery, companion-image compatibility, settings/bond
  preservation, and post-update audio.
