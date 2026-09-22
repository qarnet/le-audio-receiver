# PB-033 phase 1 handoff: session-bound same-family identity

Status: implementation handoff prepared 2026-09-22. Do not execute until
PB-032 PR #14 is human-merged and its commits are present on `main`.

## Goal

Implement host-only session creation and revalidation foundations for two XIAO
nRF54L15 boards. Operator explicitly assigns receiver and source probe serials
once. Tool writes one external, immutable-by-contract session manifest and can
re-resolve current tty paths from stable board identity without depending on
`/dev/ttyACM*` numbering.

Phase 1 proves session schema, safe file lifecycle, explicit same-family probe
discovery, full AP/FICR fingerprint retention, and drift detection with fake-lab
tests. It does not build, flash, reset, or stream from hardware.

## Dependency gate

Before editing:

1. Confirm PB-032 PR #14 is merged by checking GitHub and that commit
   `12b1ba64fdfde583f795c572fe4cf08b8024d398` is reachable from current
   `main`. Stop and report if not.
2. Rebase or recreate `feature/nrf54l15-hil-fixture-session` from updated
   `main` without force-pushing or amending any existing commit.
3. Verify PB-033 is `Ready`, then use Backlog.md CLI to move it to
   `In Progress` and set this three-phase implementation plan:
   1. session identity/manifest foundation;
   2. nRF54L15 source firmware plus build/flash tooling;
   3. runner integration, physical 10 ms proof, regressions, and PR gate.

Commands for task metadata must use `nix develop -c backlog ...`.

## In scope

- Generalize physical binding validation so source role may be either current
  nRF5340DK J-Link/nRF53 or new XIAO CMSIS-DAP/nRF54L15, based on exact logical
  board target.
- Replace HIL discovery and HIL environment-provenance calls to missing
  `nrf-probes` executable with current `nix-nrf probes` argv while preserving
  existing nRF5340 fixture behavior. Existing standalone flash helpers and the
  BlueZ/WirePlumber gate remain outside phase 1.
- Add explicit-probe, read-only nRF54L15 CMSIS-DAP fingerprinting that retains
  DPIDR, AP0 through AP3 IDRs, FICR INFO.PART, and raw INFO.VARIANT.
- Add strict session manifest creation, loading, exact-byte change detection,
  fixture/binding hash binding, and hardware/USB/tty revalidation APIs.
- Add public `hil-runner.py create-session` command.
- Add checked-in logical and example physical files for XIAO receiver plus XIAO
  source.
- Add public-boundary host tests for happy path and every required fail-closed
  case.
- Update PB-033 implementation notes with phase outcome and validation.

## Out of scope

- No source firmware C, Kconfig, CMake, devicetree, build helper, flash helper,
  image-path, package, or artifact-schema change.
- No `run`, matrix, runner flash, reset, serial-open, or streaming integration.
  Phase 3 will consume phase 1 APIs.
- No hardware write, flash, erase, reset, DTR/RTS, or Bluetooth action.
- No permanent probe-to-role registry and no committed live probe serial.
- No nRF5340 fixture removal or incompatible manifest/archive migration.
- No receiver behavior change.

## Grounding evidence

- Current schemas hardcode role contracts in `scripts/hil/model.py`:
  receiver `nrf-probes`/nRF54L, source J-Link/nRF53.
- Current discovery in `scripts/hil/discovery.py` invokes `nrf-probes` and uses
  `--find nrf54l`; `--find` is ambiguous when both XIAOs are attached.
- Current installed command is `nix-nrf 0.1.2`. Public syntax is
  `nix-nrf probes [SERIAL ...]` and `nix-nrf probes --find FAMILY`.
- Live read-only output identified two distinct boards as nRF54L15 AAC0 with
  DPIDR `0x6ba02477` and PART `0x00054b15`. Live serials remain session
  evidence only and must not appear in committed fixtures, examples, tests, or
  documentation as a permanent role mapping.
- Read-only explicit OpenOCD scans on both boards returned identical family
  signatures: AP0 `0x84770001`, AP1 `0x84770001`, AP2 `0x32880000`, AP3
  `0x00000000`, PART `0x00054b15`, raw VARIANT `0x41414330` (`AAC0`).
- XIAO tty interfaces expose VID `2886`, PID `0066`, interface `02`, and
  `ID_SERIAL_SHORT` equal to CMSIS-DAP probe serial. Current `/dev/ttyACM*`
  paths are observations only.
- Installed `nix-nrf probes` already scans AP0 through AP3 internally, but its
  public table prints only DPIDR, PART, and printable VARIANT. Repository code
  therefore needs its own read-only explicit-probe OpenOCD call to retain raw AP
  values in session evidence.
- Exact nRF54L15 FICR addresses from installed tool:
  INFO.PART `0x00FFC31C`, INFO.VARIANT `0x00FFC320` after
  `chip.dap apcsw 0x01000000 0x01000000`.
- `tests/hil/rh2_test.py` already provides fake sysfs, udev, command runner,
  malformed-table, ambiguity, duplicate tty, wrong PART, OpenOCD warning, and
  partial-evidence patterns. Extend those patterns rather than introducing a
  second fake framework.

## Exact implementation decisions

### 1. Logical and physical fixture contracts

Touch `scripts/hil/model.py`.

Replace global role-only `PROBE_BACKENDS` and `PROBE_FAMILIES` assumptions with
an exact board/role contract table:

| Role | Logical board | Backend | Family | Probe udev required |
|---|---|---|---|---|
| receiver | `nrf54l15dk/nrf54l15/cpuapp` | `nrf-probes` | `nrf54l` | no |
| source | `nrf5340dk/nrf5340/cpuapp` | `jlink` | `nrf53` | yes |
| source | `nrf54l15dk/nrf54l15/cpuapp` | `nrf-probes` | `nrf54l` | no |

Keep backend value `nrf-probes` in physical JSON for schema compatibility; it
means CMSIS-DAP target fingerprinting. Production argv changes to
`nix-nrf probes`. Reject unsupported role/board combinations before discovery.
Pass corresponding `LogicalRole` into physical role parsing so backend/family
validation follows logical board target. Preserve every existing strict scalar,
unknown-key, udev, DTR/RTS, and role-set rule.

Add:

- `tests/hil/fixture-xiao-source.json`
  - fixture ID `local-xiao-nrf54l15-pair`
  - capture capability `none`
  - receiver board `nrf54l15dk/nrf54l15/cpuapp`, images `cpuapp`, `flpr`
  - source board `nrf54l15dk/nrf54l15/cpuapp`, image `cpuapp`
- `tests/hil/fixture-xiao-source.local.example.json`
  - both roles use backend `nrf-probes`, family `nrf54l`
  - both serial blocks use 115200 baud, DTR true, RTS false, empty udev map
  - no static serial or tty path
- `.gitignore` entry `/tests/hil/fixture-xiao-source.local.json`.

Do not change current `tests/hil/fixture.json` or
`tests/hil/fixture.local.example.json` semantics.

### 2. Discovery API and fingerprints

Touch `scripts/hil/discovery.py`.

Keep `parse_nrf_probes_table()` table grammar. Rename comments and errors only
where needed; tests may continue referring to table format as nrf-probes format.

Extend `ProbeIdentity` with:

- `product: str`
- `ap_idrs`: immutable map with exactly `ap0`, `ap1`, `ap2`, `ap3`
- `variant_raw: str`

Preserve existing `variant` display value for compatibility. CMSIS-DAP roles
carry printable table value such as `AAC0` and raw value such as
`0x41414330`. J-Link source may retain current raw value in `variant` and must
also populate `variant_raw` with same marker value.

Add `fingerprint_cmsis_dap(run_cmd, serial, timeout=60)`. Exact command shape:

- `openocd -f interface/cmsis-dap.cfg`
- explicit `adapter serial <serial>`
- SWD, 1000 kHz
- `gdb port disabled`, `tcl port disabled`, `telnet port disabled`
- generic `swd newdap chip cpu`, DAP creation, Cortex-M target creation
- repository-owned TCL emits unique marker prefix for DPIDR, AP0..AP3, PART,
  VARIANT
- `init`, scanner proc, `shutdown`
- no reset, halt, program, load, write-memory, recover, or line-control command

Use FICR addresses and AP CSW values listed under grounding evidence. Reject
nonzero status and every line matched by existing `OPENOCD_FAILURE_RE`. Require
all six raw marker values. AP3 value `0x00000000` is valid and must not be
treated as missing.

Generalize:

```python
resolve_fixture(
    binding,
    *,
    run_cmd,
    sysfs_root="/sys",
    explicit_probe_serials=None,
)
```

Rules:

- `explicit_probe_serials` is either `None` or exact map with receiver/source
  keys, nonempty safe serial strings, and distinct values.
- For explicit XIAO pair, invoke exactly one
  `nix-nrf probes <receiver-serial> <source-serial>` command in role order.
  Require exactly one table row for each requested serial, exact target
  `nRF54L15`, DPIDR `0x6ba02477`, and PART `0x00054b15`.
- Run one direct CMSIS-DAP fingerprint for each requested serial and cross-check
  DPIDR, PART, and raw-to-printable VARIANT against table values. Reject any
  disagreement.
- Resolve tty independently for each role by `ID_SERIAL_SHORT == probe.serial`
  plus configured udev constraints. Require exactly one tty per role, distinct
  tty nodes, and distinct USB parents.
- Existing mixed fixture with no explicit map still resolves receiver through
  `nix-nrf probes` plus `nix-nrf probes --find nrf54l`, then resolves source
  through current J-Link udev and OpenOCD path. Preserve old failure behavior.
- If receiver binding includes `ID_SERIAL_SHORT`, target that serial directly
  instead of family-only `--find`. This keeps current nRF5340 source fixture
  usable when another XIAO is attached.
- Raw evidence keys must name actual command but may retain current evidence
  filenames for backward compatibility. Update runner raw-evidence handling so
  successful and partial failures retain table output, targeted/find output,
  each CMSIS fingerprint output, J-Link output, and role udev records.

Update `scripts/hil/evidence.py` so environment provenance invokes
`["nix-nrf", "probes", "--help"]`. Keep existing serialized tool key if
renaming would break evidence readers; argv must show current command.

### 3. Session manifest module

Add `scripts/hil/session.py` with no hardware mutation and no imports from
`runner.py`.

Public constants:

```python
SESSION_SCHEMA_VERSION = 1
DEFAULT_SESSION_ROOT = "/tmp/opencode/hil-sessions"
SESSION_FILENAME = "devices.json"
```

Public APIs:

```python
create_session(
    fixture_path,
    binding_path,
    session_id,
    receiver_probe,
    source_probe,
    *,
    run_cmd,
    session_root=DEFAULT_SESSION_ROOT,
    sysfs_root="/sys",
    utc_now=None,
) -> SessionManifest

load_session(manifest_path, fixture_path, binding_path) -> SessionManifest

revalidate_session(
    manifest,
    fixture_path,
    binding_path,
    binding,
    *,
    run_cmd,
    sysfs_root="/sys",
) -> FixtureResolution
```

`SessionManifest` is immutable and retains canonical path, exact raw bytes,
SHA-256, session ID, fixture ID, fixture SHA-256, binding SHA-256, and role
records.

Session ID uses existing safe run-ID grammar. Probe serials use
`^[A-Za-z0-9_.:-]+$`. Reject duplicate serials before any external command.
Fixture and binding must parse through `model.py`; this command accepts only
the XIAO-pair logical contract in phase 1.

Manifest schema is exact and rejects unknown/missing/duplicate keys:

```json
{
  "schema_version": 1,
  "session_id": "pb033-proof",
  "created_at_utc": "RFC3339 UTC",
  "fixture": {
    "fixture_id": "local-xiao-nrf54l15-pair",
    "sha256": "64 lowercase hex"
  },
  "binding": {
    "sha256": "64 lowercase hex"
  },
  "roles": {
    "receiver": {
      "expected_firmware": {
        "role": "receiver",
        "boot_contract": "le-audio-receiver-v1"
      },
      "probe": {
        "backend": "nrf-probes",
        "family": "nrf54l",
        "serial": "operator-selected serial",
        "product": "table PROBE cell",
        "target": "nRF54L15",
        "dpidr": "0x6ba02477",
        "ap_idrs": {
          "ap0": "raw hex",
          "ap1": "raw hex",
          "ap2": "raw hex",
          "ap3": "raw hex"
        },
        "part": "0x00054b15",
        "variant": "AAC0",
        "variant_raw": "0x41414330"
      },
      "serial": {
        "path_observed": "/dev/ttyACM<number>",
        "usb_parent_observed": "canonical sysfs USB parent",
        "stable_udev": {
          "ID_BUS": "usb",
          "ID_VENDOR_ID": "2886",
          "ID_MODEL_ID": "0066",
          "ID_SERIAL_SHORT": "same probe serial",
          "ID_USB_INTERFACE_NUM": "02",
          "ID_USB_DRIVER": "cdc_acm",
          "ID_PATH": "current physical USB path"
        },
        "baud": 115200,
        "dtr": true,
        "rts": false
      }
    },
    "source": {
      "expected_firmware": {
        "role": "source",
        "firmware_id": "le-audio-hil-source-rh1",
        "protocol_version": 1
      },
      "probe": "same exact object shape as receiver",
      "serial": "same exact object shape as receiver"
    }
  }
}
```

Implementation may encode both role objects normally; quoted shorthand above
only avoids repeating identical schema in this handoff.

`stable_udev` must contain exactly listed keys and all values must be nonempty.
`path_observed` is evidence, never identity. `ID_PATH` and
`usb_parent_observed` freeze physical USB location for one session.

Creation sequence:

1. Parse fixture/binding and validate IDs plus distinct serial syntax.
2. Hash exact fixture and binding bytes.
3. Run explicit read-only discovery and form full normalized payload in memory.
4. Validate session root. Default root may be created only directly under
   existing canonical `/tmp/opencode`; reject symlinks and non-directories.
   Custom root must be absolute, existing, canonical, external to repository,
   and non-symlink.
5. Create `<root>/<session-id>` once with mode `0700`; existing path fails.
6. Create `devices.json` using `O_CREAT|O_EXCL|O_NOFOLLOW` where available,
   serialize sorted deterministic JSON plus final newline, fsync file, chmod
   read-only `0400`, and fsync directory. Never overwrite or update it.
7. Return loaded immutable object. Do not delete successful manifest.

Loading sequence:

- Require absolute canonical regular file, no symlink, basename
  `devices.json`, and parent basename equal session ID.
- Read exact bytes once, compute SHA-256, strictly parse schema, and compare
  exact fixture/binding bytes and hashes.
- `SessionManifest.assert_unchanged()` reopens and rehashes same non-symlink
  path; any byte, type, replacement, permission, path, or disappearance drift
  raises `HilSessionError`.

Revalidation sequence:

1. Call `assert_unchanged()` before any external command.
2. Rehash fixture and binding and compare manifest hashes.
3. Resolve both explicit probe serials through `discovery.resolve_fixture()`.
4. Compare every normalized probe field, complete AP map, USB parent, and exact
   stable udev map. Ignore only current tty node name/path. Return fresh
   `FixtureResolution` so later phase can open current tty path safely.
5. Fail on any missing/extra field, hardware drift, USB-location drift,
   duplicate role, missing tty, or ambiguity.

### 4. CLI

Touch `scripts/hil/cli.py` and its module documentation.

Add:

```text
hil-runner.py create-session \
  --fixture PATH \
  --binding PATH \
  --session-id ID \
  --receiver-probe SERIAL \
  --source-probe SERIAL \
  [--session-root PATH]
```

`--session-root` defaults to `/tmp/opencode/hil-sessions` and exists for tests
and isolated labs. Command calls `session.create_session()` with production
read-only process runner and prints one sorted JSON line:

```json
{
  "fixture_id": "local-xiao-nrf54l15-pair",
  "manifest": "/absolute/root/session-id/devices.json",
  "roles": {
    "receiver": "selected serial",
    "source": "selected serial"
  },
  "session_id": "session-id"
}
```

Success returns 0 and writes no stderr. Existing CLI error wrapper emits one
`hil-runner: error: ...` line and status 2. Existing command argument shapes
must remain unchanged.

### 5. Tests

Touch `scripts/test_hil_runner.py`, `tests/hil/rh2_test.py`, and
`tests/hil/hil_fakes.py`. Test observable outcomes, command argv, generated
manifest bytes/mode, returned current tty path, and failure before later
processes. Do not test private helper call counts.

Required coverage:

- New checked-in fixture and example parse; old fixture/example still parse.
- Unsupported board/backend/family and J-Link udev rules remain fail-closed.
- Real public argv inside `scripts/hil/` is `nix-nrf probes`; no executable
  `nrf-probes` call remains in phase 1 HIL modules. Backend/schema strings,
  evidence filenames, standalone flash helpers, and the BlueZ/WirePlumber gate
  may retain `nrf-probes` in this phase.
- CMSIS fingerprint command is explicit, read-only, warning-clean, uses current
  `gdb port`/`tcl port`/`telnet port` syntax, and parses AP3 zero correctly.
- Duplicate requested serials fail before command runner receives any call.
- Explicit pair resolves two exact nRF54L15 rows and distinct correlated ttys.
- Missing selected probe, extra/missing table row, wrong target, DPIDR/PART/
  VARIANT disagreement, malformed marker, OpenOCD warning/error/nonzero, missing
  tty, duplicate tty, shared tty, and shared USB parent fail.
- `create_session()` writes exact path, mode `0400`, complete schema, exact
  fixture/binding hashes, AP IDs, stable udev map, role contracts, and no
  committed/live tty assumption.
- Existing session ID/path or symlink root/manifest fails without overwrite.
- Strict manifest parser rejects malformed UTF-8/JSON, duplicate keys, unknown
  keys, wrong types, unsafe paths, and role-set mismatch.
- Manifest byte mutation/replacement/disappearance fails before discovery.
- Fixture or binding drift fails before discovery.
- Probe/AP/FICR/stable-udev/USB-parent drift fails revalidation.
- Tty node renumbering with unchanged selected serial, USB parent, and stable
  udev identity succeeds and returns new current `/dev/tty*` path.
- CLI success JSON and one-line failure shape.
- Existing mixed nRF54 receiver plus nRF5340 J-Link source discovery tests pass
  with updated argv and evidence.
- Validate serial text returned by `nix-nrf probes --find nrf54l` against
  `PROBE_SERIAL_RE` before using it in the OpenOCD Tcl command.
- Build and strictly validate the complete manifest payload before creating the
  immutable session directory, so an invalid generated payload cannot consume
  a session ID.

## Verification

Run sequentially from repository root:

```bash
nix develop -c python3 scripts/test_hil_runner.py
nix develop -c python3 tests/hil/rh2_test.py
nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
nix develop -c backlog doctor
git diff --check
```

Do not run hardware command in this phase. Treat every warning as failure.

## Files allowed

- `.gitignore`
- `scripts/hil/model.py`
- `scripts/hil/discovery.py`
- `scripts/hil/session.py` (new)
- `scripts/hil/cli.py`
- `scripts/hil/evidence.py`
- `scripts/hil/runner.py` only for normalized identity/raw-evidence fields
- `scripts/test_hil_runner.py`
- `tests/hil/hil_fakes.py`
- `tests/hil/rh2_test.py`
- `tests/hil/fixture-xiao-source.json` (new)
- `tests/hil/fixture-xiao-source.local.example.json` (new)
- PB-033 task file for status, plan, notes, and evidence
- this handoff only if correction is required

Do not touch source firmware, build/flash scripts, release/artifact code,
accepted historical evidence, public product documentation, or unrelated tests.

## Commit and escalation

After all verification passes, inspect `git status`, complete diff, and recent
log. Stage only allowed files and commit phase 1 with concise repository-style
message containing `PB-033`. Do not push, open/merge PR, amend, or add
AI/tool attribution.

Return files changed, public behavior, tests and exact results, commit hash and
message, deviations, and blockers.

Stop without committing partial work and report to Delegator if two materially
different fixes fail, repository/tool evidence contradicts this design, a
warning cannot be explained, scope must expand, assertions would need
weakening, or any target-changing command appears necessary.

## Review repair gate

Review of commit `0c4bb3c` found session-boundary defects that must be repaired
before phase 1 acceptance:

1. Parse fixture and binding from the exact byte snapshots that are hashed and
   retained. Add byte-oriented parser entry points in `scripts/hil/model.py`,
   keep existing path loaders as wrappers, and use the byte entry points from
   `scripts/hil/session.py`. Re-read and compare fixture/binding bytes after
   discovery but before creating the session directory so concurrent input
   drift fails without consuming the session ID.
2. Require exact JSON integer types for `schema_version` and serial `baud`;
   reject floats that compare equal to integers.
3. Validate session IDs with full-match semantics. A trailing newline or any
   character outside the existing run-ID grammar must fail before discovery.
4. Reject custom session root `/` and every root that is either inside the
   repository or an ancestor containing the repository. Use path-component
   comparison, not string concatenation that special-cases `/` incorrectly.
5. Add public-boundary regressions for input drift before directory creation,
   float numeric fields, trailing-newline session ID, root `/`, permission-only
   manifest drift, non-regular or symlink replacement, relative manifest path,
   wrong basename, and parent/session-ID mismatch.
6. Commit this corrected handoff with the repair. Do not amend `0c4bb3c`; create
   one focused follow-up commit and update PB-033 notes with final test counts.
