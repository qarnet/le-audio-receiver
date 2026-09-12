# System HIL RH4 exact-artifact integration handoff

Status: host-only implementation handoff. No candidate execution or hardware
acceptance.

## Goal

Add immutable artifact mode for HIL rows/matrix. Receiver firmware must come
from exact FR1 nRF54L15 factory ZIP. Source fixture firmware must come from a
new deterministic HIL-source ZIP. Validate complete archive/manifest/checksum
contracts before creating run evidence or touching hardware. Existing local
build mode stays unchanged for RH2/RH3 development.

## Scope

- New stdlib-only deterministic HIL source artifact packager and public tests.
- New strict artifact resolver under `scripts/hil/`.
- Internal image-path support in both HIL flash helpers.
- One-row runner and RH3 matrix artifact inputs/evidence.
- New explicit RH4 matrix CLI.
- Fake-only artifact and orchestration tests.
- HIL docs/status comments.

No hardware, live artifact download, GitHub API, release creation, tag,
publication, analog capture, firmware behavior, or accepted FR1 packager format
change. No commit.

## Artifact contracts

### Receiver artifact

Input is exact FR1 ZIP named:

```text
le-audio-receiver-v<VERSION>-nrf54l15-xiao-factory.zip
```

Require regular, non-symlink, nonempty ZIP. Compute outer SHA-256. Require exact
member set/order:

```text
FLASHING.md
cpuapp.hex
flpr.hex
release-manifest.json
SHA256SUMS
```

Reject encrypted entries, duplicate names, directories, absolute/traversal
names, unsupported compression, non-0644 regular members, extra fields,
comments, wrong fixed timestamp, CRC/read failure, or oversized members/archive
using explicit conservative limits.

Manifest schema must equal accepted FR1 schema version 1 for target
`nrf54l15-xiao`, board `nrf54l15dk/nrf54l15/cpuapp`, roles cpuapp then flpr,
filenames/flash order 0/1, canonical project/version/commit/NCS fields, size and
SHA-256 matching extracted bytes. Per-ZIP `SHA256SUMS` must exactly cover
FLASHING.md, both images, manifest, sorted with GNU two-space syntax.

### Source artifact

Add `scripts/package-hil-source-artifact.py`. Required CLI:

```text
python3 scripts/package-hil-source-artifact.py \
  --git-commit <40-lowercase-hex> \
  --ncs-version v3.3.0 \
  --build-root build/hil-source \
  --output <absent-output.zip>
```

Validate exact source inputs before output creation:

- `app/zephyr/zephyr.hex` as `cpuapp.hex`, role `cpuapp`, flash order 1;
- `hci_ipc/zephyr/zephyr.hex` as `cpunet.hex`, role `cpunet`, flash order 0.

Use same strict Intel HEX validation as FR1 behavior. Do not import hyphenated
packager through runtime hacks and do not change accepted FR1 packager. Small
shared stdlib module is allowed only if FR1 public tests prove byte-identical
existing output; otherwise duplicate narrow validated primitives.

Output exact deterministic ZIP member order:

```text
cpunet.hex
cpuapp.hex
source-manifest.json
SHA256SUMS
```

Manifest schema version 1:

```json
{
  "board": "nrf5340dk/nrf5340/cpuapp",
  "firmware_id": "le-audio-hil-source-rh1",
  "git_commit": "...",
  "images": [
    {"filename":"cpunet.hex","flash_order":0,"role":"cpunet","sha256":"...","size":1},
    {"filename":"cpuapp.hex","flash_order":1,"role":"cpuapp","sha256":"...","size":1}
  ],
  "ncs_version": "v3.3.0",
  "schema_version": 1
}
```

Sorted keys, two-space indent, one trailing newline. ZIP fixed metadata matches
FR1 deterministic rules. Internal SHA256SUMS covers two images plus manifest.
Output path must be absolute, parent existing, final absent, no symlink. Build
through sibling temporary file and atomic rename; cleanup temporary on failure.

Artifact resolver requires exact source member order/metadata/manifest/hashes.

## Runner integration

Add immutable `ArtifactSet` produced only by validated resolver. It contains
outer artifact identity plus private staged regular image paths. Staging root is
outside repository and no-clobber. Extract with explicit writes, never
`ZipFile.extract*`. Reopen/no-follow or lstat checks must prevent symlink races
at input/output boundaries practical in stdlib.

One-row `Runner.run(..., artifacts=None)`:

- `None`: current local build paths and behavior unchanged;
- ArtifactSet: hash/record original archive and exact staged images; flash only
  those paths; row evidence includes version/commit/NCS/outer and member hashes;
- reject partially supplied, wrong target/board/role/order, mixed local/artifact
  tuple, changed file after validation, or artifact/image path inside repo;
- re-hash immediately before each flash helper invocation and fail on drift.

Flash helper internal environment, pairwise all-or-none:

- receiver: `FW_NRF54L15_CPUAPP_HEX`, `FW_NRF54L15_FLPR_HEX`;
- source: `FW_HIL_SOURCE_CPUAPP_HEX`, `FW_HIL_SOURCE_CPUNET_HEX`.

When overrides absent, existing fixed build paths remain byte-compatible. When
present, require both, absolute canonical regular non-symlink nonempty `.hex`
paths outside repository, then use exact paths in existing verified flash order.
No CLI exposes individual image paths.

RH4 matrix command:

```text
python3 scripts/hil-runner.py run-rh4-matrix \
  --fixture ... --binding ... --output-root ... --run-id ... --junit ... \
  --receiver-artifact /absolute/...nrf54l15...factory.zip \
  --source-artifact /absolute/...hil-source.zip
```

Resolve and stage both archives once before any child. Every child gets same
immutable ArtifactSet and revalidates hashes before flash. Aggregate schedule,
result, environment, children records, and manifest include artifact identity.
Copy original ZIP bytes into aggregate flat evidence as `receiver-artifact.zip`
and `source-artifact.zip`; verify copied hashes. No acceptance verdict string.

Existing `run`, `run-rh3-matrix`, RH2 default, and local image behavior stay
unchanged. No artifact options on those commands.

## Tests

Public subprocess/unit boundary coverage:

- deterministic source package byte identity and exact contract;
- malformed HEX, manifest, checksum, metadata, member order/name, duplicate,
  traversal, encryption, compression, size limit, CRC, symlink, empty/missing,
  output collision, and write failure cleanup;
- accepted real-format receiver fixture and source fixture resolve;
- archive/member hash drift rejection before child/flash;
- helper all-or-none absolute path checks and exact OpenOCD argv/flash order;
- RH4 fixed two-pass schedule receives same exact artifact identity every child;
- aggregate copies/hash/evidence; fail-fast and JUnit semantics unchanged;
- no hardware in fake tests;
- legacy local mode and FR1 packager tests unchanged.

## Verification

```bash
python3 scripts/test_package_firmware_release.py
python3 scripts/test_package_hil_source_artifact.py
python3 tests/hil/rh4_artifact_test.py
python3 tests/hil/rh3_matrix_test.py
python3 tests/hil/rh2_test.py
python3 -W error scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/rh2_hardware_test.py
python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil \
  scripts/package-hil-source-artifact.py scripts/test_package_hil_source_artifact.py
git diff --check
git status --short
```

No hardware. Stop/escalate on warning, ambiguous accepted FR1 contract,
security weakening, arbitrary image path exposure, architecture invention, or
test weakening. Return exact files, behavior, tests, deviations, blockers, and
no-commit status.
