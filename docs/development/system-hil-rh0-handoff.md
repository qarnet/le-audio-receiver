# System HIL RH0 implementation handoff

Status: implementation handoff for RH0 only. Follow
`docs/development/system-hil-milestones.md`. This phase must not access, flash,
reset, pair, connect to, or stream from hardware.

## Goal

Add the host-only foundation for the future two-device HIL runner:

- reproducible pytest and pyserial dependencies;
- strict logical-fixture and local-binding schemas;
- capture capability values `none`, `mono`, and `stereo`, with checked-in
  default `none`;
- versioned `HIL1` source-record parser and run-state tracker;
- immediate-registration cleanup ownership, exclusive fixture locking, safe
  run-directory creation, and fail-closed evidence finalization;
- a no-hardware CLI validation/prepare boundary;
- public-boundary regression tests run by the existing canonical Python-child
  mechanism.

RH0 proves resource lifecycle and schemas without attached hardware. It does
not implement source firmware, probe/serial discovery, flashing, serial I/O,
pytest hardware fixtures, streaming, receiver-status parsing, or audio capture.

## Grounding

- `docs/development/system-hil-milestones.md`, especially Runner decision,
  Fixture lifecycle, Resource and identity contract, Source serial protocol,
  and RH0.
- `scripts/test_inventory.py` discovers every immediate `scripts/test_*.py`
  file as one canonical Python child and invokes it directly, not through
  pytest.
- `scripts/test-all.sh` runs Python children with `scripts/` on `PYTHONPATH`.
  New host regression test must therefore remain executable as a standalone
  stdlib `unittest` program even though pytest is pinned for later HIL tests.
- `flake.nix` already supplies Python packages through
  `pkgs.python3Packages`; add `pytest` and `pyserial` there. Existing
  `flake.lock` pins nixpkgs, so no lockfile change should be needed.
- Project safety requires runtime probe discovery through `nrf-probes`; never
  add a static probe serial mapping.
- Current working tree already contains user-owned untracked
  `docs/development/firmware-release-fr4-summary-wait-fix-handoff.md`. Do not
  edit, stage, delete, or absorb it.

## Files

Add:

- `scripts/hil/__init__.py`
- `scripts/hil/model.py`
- `scripts/hil/protocol.py`
- `scripts/hil/lifecycle.py`
- `scripts/hil/evidence.py`
- `scripts/hil/cli.py`
- `scripts/hil-runner.py`
- `scripts/test_hil_runner.py`
- `tests/hil/fixture.json`
- `tests/hil/fixture.local.example.json`

Update:

- `.gitignore`
- `flake.nix`
- `scripts/test-all.sh` current inventory comment only

Do not update accepted historical gate counts in `STATUS.md`, `AGENTS.md`,
historical result docs, or release evidence. Adding `scripts/test_hil_runner.py`
changes working-tree inventory from 62 to 63 unit children and the prospective
gate composition from 65 to 66, but no 66-child clean-tree canonical run exists
yet. `scripts/test-all.sh` must truthfully describe its dynamic working-tree
inventory as 35 Twister + 5 exec-only + 23 Python = 63 unit children, 66 total.

## Exact model and schemas

Use `SCHEMA_VERSION = 1`. Reject non-UTF-8, invalid JSON, non-object root,
unknown keys, missing keys, wrong scalar types, duplicate semantic role names,
empty strings, and booleans where integers are expected. Never silently fill a
required field.

### Capture capability

`CaptureCapability` is a string enum with exactly:

- `none`
- `mono`
- `stereo`

Checked-in `tests/hil/fixture.json` selects `none`. Capture evidence is absent
when capability is `none`; no fake capture role or success record is created.

### Logical fixture

`tests/hil/fixture.json` has this shape:

```json
{
  "schema_version": 1,
  "fixture_id": "local-nrf54l15-receiver",
  "capture_capability": "none",
  "roles": {
    "receiver": {
      "kind": "zephyr_dut",
      "board": "nrf54l15dk/nrf54l15/cpuapp",
      "images": ["cpuapp", "flpr"]
    },
    "source": {
      "kind": "zephyr_dut",
      "board": "nrf5340dk/nrf5340/cpuapp",
      "images": ["cpuapp", "cpunet"]
    }
  }
}
```

Required roles are exactly `receiver` and `source`; optional `capture` is
forbidden when capability is `none` and required when capability is `mono` or
`stereo`. Zephyr roles require nonempty board and a nonempty unique image list.
Capture role, when present, uses `kind: "alsa_capture"`, omits board/images,
and carries `channels` equal to 1 for mono or 2 for stereo.

Expose frozen dataclasses and `load_logical_fixture(path)` in
`scripts/hil/model.py`.

### Physical binding

Binding root has exact keys `schema_version`, `fixture_id`, and `roles`.
Fixture ID must equal logical fixture ID. Role set must exactly match logical
role set.

Each Zephyr role binding has:

```json
{
  "probe": {
    "backend": "nrf-probes",
    "family": "nrf54l"
  },
  "serial": {
    "baud": 115200,
    "udev": {
      "ID_VENDOR_ID": "REPLACE_WITH_HEX_VENDOR",
      "ID_MODEL_ID": "REPLACE_WITH_HEX_PRODUCT",
      "ID_PATH": "REPLACE_WITH_STABLE_USB_PATH",
      "ID_USB_INTERFACE_NUM": "REPLACE_WITH_INTERFACE"
    }
  }
}
```

Source family is `nrf53`; receiver family is `nrf54l`. Probe backend/family
must match logical role and no probe serial field is allowed. Serial baud is a
positive integer. `udev` is a nonempty string-to-string map containing
`ID_VENDOR_ID`, `ID_MODEL_ID`, and at least one of `ID_SERIAL_SHORT` or
`ID_PATH`; reject `DEVNAME` and any value beginning `/dev/tty` so volatile node
numbers cannot become identity. Additional `ID_*` keys are allowed for exact
interface selection; other keys are rejected.

Capture binding is not needed in checked-in default/example. Schema support for
future capture binding may be deferred, but parser must emit a clear unsupported
error rather than accepting incomplete mono/stereo data.

Expose frozen dataclasses and `load_physical_binding(path, logical_fixture)`.
`tests/hil/fixture.local.example.json` is valid schema with explicit
`REPLACE_WITH_*` values. Add `/tests/hil/fixture.local.json` to `.gitignore`.

## HIL1 protocol parser

`scripts/hil/protocol.py` owns protocol syntax and state, with no serial I/O.

Raw source records are one UTF-8 line beginning `HIL1 ` followed by one JSON
object. Unprefixed lines return `None` to caller so warning scanning can own
them. A malformed prefixed line raises `HilProtocolError`; never treat it as a
normal log line.

Every HIL1 object has exact required keys:

- `protocol_version`: integer 1
- `kind`: `ack`, `state`, `terminal`, or `status`
- `firmware_id`: nonempty string
- `monotonic_ms`: nonnegative integer
- `command_id`: nonempty string
- `run_id`: nonempty string
- `segment`: nonnegative integer
- `data`: object

`state` records additionally require `data.state` from:

1. `idle`
2. `configured`
3. `connecting`
4. `secured`
5. `discovered`
6. `qos`
7. `streaming`
8. `scored_complete`
9. `teardown`

Within first segment, states follow list exactly with no skip, repeat, or
backward move. Segment starts at 0. A later segment increments by exactly one,
may start only at `connecting` after previous segment reached `teardown`, then
follows `connecting` through `teardown` without skip. This supports explicit
reconnect without permitting arbitrary state regression.

`terminal` requires `data.verdict` exactly `pass` or `fail`; it occurs once per
run after current segment reached `teardown`. After terminal, reject all
unsolicited `ack`, `state`, and `terminal` records. `status` is a query response
and may use a different command ID before or after terminal; it never advances
or rewinds tracked state. All records for one tracker keep same `run_id` and
`firmware_id`; asynchronous `ack`, `state`, and `terminal` records keep start
command ID. Monotonic time and segment never regress.

Expose:

- `parse_hil1_line(line) -> HilRecord | None`
- `HilRunTracker(run_id, command_id)`
- `HilRunTracker.accept(record)`

Use custom `HilProtocolError` with stable concise messages tested by substring,
not exact full exception repr.

## Lifecycle and lock ownership

`scripts/hil/lifecycle.py` must remain hardware-independent.

### CleanupStack

- `register(name, callback)` adds one callback and rejects registration after
  close begins.
- `close()` runs every callback exactly once in reverse registration order.
- One callback failure never prevents later callbacks.
- Aggregate failures in `CleanupFailure` with callback names and original
  exceptions.
- Repeated close is idempotent and does not rerun callbacks.
- Context exit invokes close for normal return, exception, and
  `KeyboardInterrupt`. Cleanup failure must not hide original body failure;
  expose both in a structured exception or exception group supported by
  project Python version.

### FixtureLock

- Lock root is caller-provided output root plus `.locks/`.
- File name is SHA-256 of fixture ID, not raw user text.
- Acquire atomically with `os.open(..., O_CREAT | O_EXCL | O_WRONLY, 0o600)`.
- File contains JSON token, fixture ID, PID, host, and creation time; flush and
  fsync before acquisition returns.
- Existing file fails closed with `FixtureBusy`; do not infer staleness or
  remove it automatically.
- Release unlinks only when on-disk token still equals owner token. Changed or
  malformed lock raises cleanup failure and leaves file untouched.
- Immediate cleanup registration occurs as soon as acquisition succeeds.

### Output-root and run-directory safety

Mirror `scripts/test-all.sh` safety semantics:

- output root must be absolute;
- reject `/`, home directory, repository root, any path inside repository, and
  any ancestor containing repository;
- canonicalize before decisions;
- run ID matches `[A-Za-z0-9][A-Za-z0-9._-]{0,63}`;
- create `<output-root>/<run-id>` with no overwrite and no symlink traversal;
- setup failure after lock acquisition releases owned lock;
- never delete run evidence during cleanup.

## Evidence finalization

`scripts/hil/evidence.py` finalizes one prepared run directory:

- reject symlinks, non-regular files, and paths escaping run directory;
- hash retained regular files with SHA-256;
- atomically write `SHA256SUMS` in sorted relative-path order;
- atomically write concise `MANIFEST.md` containing fixture ID, run ID,
  capture capability, outcome (`prepared` or `failed` in RH0), and evidence
  file list;
- exclude `SHA256SUMS` and `MANIFEST.md` from their own hash list;
- an error writes failed manifest when possible, preserves prior evidence, and
  returns/raises failure; no partial success;
- no file is written outside selected run directory except owned lock.

## No-hardware CLI

`scripts/hil-runner.py` is a thin executable wrapper around
`scripts/hil/cli.py`.

Commands:

```text
hil-runner.py validate --fixture PATH --binding PATH
hil-runner.py prepare --fixture PATH --binding PATH --output-root PATH --run-id ID
```

`validate` parses and cross-validates files, prints one JSON object with
fixture ID and capture capability, and performs no filesystem mutation.

`prepare` validates, checks safe output root, acquires lock, creates run
directory, copies exact logical/binding JSON bytes into it, writes a prepared
record, finalizes `MANIFEST.md` and `SHA256SUMS`, releases lock, and prints one
JSON object containing run directory and outcome. It touches no hardware.

CLI failures print one line prefixed `hil-runner: error: ` to stderr, no
traceback, and return 2. Success returns 0 with no stderr.

## Tests

`scripts/test_hil_runner.py` uses stdlib `unittest`, temporary directories, and
the public module/CLI boundaries. It must be directly executable.

Cover at minimum:

1. Checked-in logical fixture parses as capability none with exact two roles.
2. Valid temporary binding cross-validates.
3. Unknown/missing/wrong-type schema fields fail closed.
4. Fixture ID mismatch and role-set mismatch fail.
5. Static probe serial, volatile `/dev/tty*`, insufficient udev identity, wrong
   probe family, and invalid baud fail.
6. Capture capability/role mismatch fails.
7. Unprefixed log returns None; malformed prefixed line fails.
8. Valid HIL1 single segment and reconnect sequence passes.
9. Missing fields, wrong run/firmware/command, time regression, state
   repeat/regression/skip, segment skip, duplicate terminal, and post-terminal
   unsolicited output fail.
10. In-run and post-terminal status with new command ID pass without advancing
    tracked state.
11. Cleanup order is LIFO; every callback runs despite failure; repeated close
    is idempotent; setup/body failure and KeyboardInterrupt still clean up.
12. Lock exclusion, normal release, foreign-token non-removal, and setup failure
    release.
13. Unsafe output roots/run IDs, existing run directory, and symlink escape
    fail without deleting anything.
14. Evidence hashes and manifest are deterministic/sorted; symlink/non-regular
    evidence fails; failed finalization preserves prior files.
15. CLI validate and prepare success have exact exit/stdout/stderr shape.
16. CLI malformed config, unsafe output, duplicate run, and lock conflict fail
    cleanly with exit 2.

Tests assert public results and filesystem/process outcomes, not private helper
calls or internal data-structure identity.

## Verification

Run:

```bash
nix develop --command python3 scripts/test_hil_runner.py
nix develop --command pytest --version
nix develop --command python3 -c 'import serial; print(serial.VERSION)'
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --python
python3 scripts/test_inventory.py --json
python3 scripts/test_bluez_wireplumber_gate.py
python3 scripts/test_bluez_wireplumber_phase3_gate.py
git diff --check
git status --short
```

Expected inventory count after new test child: 63. Do not run hardware commands.
Do not run full canonical gate from dirty worktree; report it as not run rather
than weakening clean-tree provenance.

## Constraints

- No hardware access, serial opening, `nrf-probes`, flashing, reset, DTR/RTS,
  Bluetooth, ALSA, or sudo commands.
- No production firmware changes.
- No audio analyzer implementation in RH0.
- No static probe serial mapping.
- No blanket erase/recovery path.
- No swallowed cleanup error or missing evidence accepted as success.
- No dependencies beyond pytest and pyserial additions already decided.
- Do not alter existing tests to hide failures or repin baselines.
- Do not commit, push, merge, open a PR, or edit git configuration. User asked
  for implementation but did not request a commit.

## Escalation

Stop and report to Delegator before inventing architecture if repository or
runtime evidence contradicts this handoff, if two materially different attempts
fail on same blocker, if clean handling would require weakening tests, or if
scope appears to require hardware access. Preserve partial worktree and provide
exact errors, attempts, diff/status, and one focused question.

## Return recap

Return files changed, public behavior added, tests/commands and exact results,
inventory count, blockers, handoff deviations, and suggested RH1 follow-up.
