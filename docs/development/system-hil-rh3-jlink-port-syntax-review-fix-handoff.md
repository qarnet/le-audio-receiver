# RH3 J-Link port-syntax review fix handoff

Status: completed host-only review repair after immutable `rh3-20260822-07-modeb-selected-layout` evidence. No physical HIL occurred in this repair phase.

## Goal

Remove three avoidable OpenOCD deprecation messages from future read-only source
J-Link fingerprinting without changing identity checks, flash behavior, HIL row
semantics, or existing evidence.

The retained RH3-07 source fingerprint contains:

```text
DEPRECATED! use 'gdb port', not 'gdb_port'
DEPRECATED! use 'tcl port' not 'tcl_port'
DEPRECATED! use 'telnet port', not 'telnet_port'
```

`scripts/hil/discovery.py` formerly passed legacy underscore commands. OpenOCD
itself names exact replacement spelling. This is host-tool diagnostics repair,
not a controller, firmware, RF, or flash warning-policy change.

## Exact implementation

### `scripts/hil/discovery.py`

In `fingerprint_jlink()`, replace only these three `-c` values:

```text
gdb_port disabled    -> gdb port disabled
tcl_port disabled    -> tcl port disabled
telnet_port disabled -> telnet port disabled
```

Keep argv order, explicit source J-Link serial, SWD transport, adapter speed,
target config, `J_LINK_FINGERPRINT_TCL`, `init`, `fwj_scan`, `shutdown`, marker
parsing, `OPENOCD_FAILURE_RE`, return-code behavior, and raw evidence behavior
unchanged.

### `tests/hil/rh2_test.py`

Add one focused public-boundary regression in `TestDiscovery` or equivalent.
Call real `discovery.fingerprint_jlink()` through a scripted fake command
runner. Assert:

1. exact returned argv contains `gdb port disabled`, `tcl port disabled`, and
   `telnet port disabled` in current positions;
2. no returned argv element contains `gdb_port`, `tcl_port`, or `telnet_port`;
3. valid `hil_fakes.fingerprint_output()` still yields status `0`, no failures,
   and expected fingerprint markers.

This proves externally issued OpenOCD argv, not private implementation shape.

### State docs

Update factual current-state wording only in:

- `docs/development/system-hil-resume-state.md`;
- `docs/development/system-hil-rh3-software-status.md`.

State that future host J-Link fingerprints use current port-command syntax and
that old immutable RH3 evidence still retains deprecation messages. Do not edit
historical record details, run counts, hashes, or `STATUS.md`.

## Scope limits

Allowed files only:

```text
scripts/hil/discovery.py
tests/hil/rh2_test.py
docs/development/system-hil-resume-state.md
docs/development/system-hil-rh3-software-status.md
```

Out of scope:

- OpenOCD flash helpers, page-tail erase diagnostics, `OPENOCD_FAILURE_RE`,
  flash command policy, source/receiver images, build, HIL execution, serial,
  Bluetooth/RF, probes, reset, pairing, configuration, `STATUS.md`, commit,
  push, merge, PR, tag, and release;
- rewriting immutable RH3-07 tool logs or claiming they no longer occurred.

## Verification

Run sequentially from repository root. No hardware command or build.

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/discovery.py \
  tests/hil/rh2_test.py \
  tests/hil/capture_runner_test.py
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
git diff --check
```

`rh2_test.py` must include one new exact-argv regression above prior `153`.
Treat any warning, unexpected test-count result, compiler issue, or failure as a
blocker. Do not run a physical HIL row to test this change.

## Executor return format

Return changed files, exact command replacements, new test name and externally
observable assertion, every verification result/count, documentation wording,
final `git status --short`, and confirmation of no hardware/build/commit. Report
blockers or deviations rather than changing scope.
