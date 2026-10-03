# PB-033 phase 3A handoff: session-bound runner integration

Status: implementation handoff prepared 2026-09-22 after phase 2 acceptance at
commit `ba06992`.

## Goal

Connect the immutable XIAO-pair session manifest from phase 1 and the
single-image XIAO source firmware from phase 2 to the existing one-row HIL
runner. One `hil-runner.py run` invocation must reuse an operator-created
session, revalidate both physical boards before every target action, select the
correct local images and source flash helper from the logical source board, and
retain complete session/revalidation evidence.

Phase 3A is host-only. It proves orchestration with fake process, serial, and
filesystem boundaries and preserves the existing nRF5340DK source path. It does
not flash, reset, open live serial ports, run Bluetooth, execute physical rows,
or claim PB-033 hardware acceptance. Physical build and proof form phase 3B
after this integration commit passes review.

## In scope

- Add optional `--session-manifest PATH` to the public one-row `run` command.
- Require a session manifest for the XIAO nRF54L15 source fixture and reject a
  session manifest for non-XIAO source fixtures.
- Load and copy the exact immutable session manifest into each run directory.
- Revalidate both session roles at setup, immediately before each serial open,
  immediately before each flash/reset helper, and immediately before the row's
  first clean-state or stream action.
- Use fresh tty paths returned by revalidation before opening descriptors, then
  fail if either tty path changes after its descriptor is open.
- Select local image inventory and source flash helper by exact logical source
  board while preserving current artifact-mode and nRF5340 behavior.
- Retain every successful revalidation snapshot plus all underlying command
  stdout/stderr in normal run evidence.
- Add public-boundary fake-lab and CLI regressions.
- Update PB-033 implementation notes with phase 3A evidence.

## Out of scope

- No hardware flash, reset, serial open, line control, Bluetooth, or stream.
- No physical result, run-directory acceptance, warning claim, or row pass.
- No new row or matrix command. The caller reuses one manifest across repeated
  `run --row ... --session-manifest ...` invocations.
- No change to row definitions, source protocol/HIL1 records, transport limits,
  receiver/source firmware, board configuration, devicetree, build helpers, or
  flash helpers.
- No RH4 archive/source-manifest schema change. XIAO source proof stays local;
  artifact mode remains the accepted nRF5340 dual-image contract.
- No nRF5340 fixture removal, default behavior change, or extra revalidation
  commands on legacy runs without a session manifest.
- No 7.5 ms, analog, RH4/FR4, release, or publication claim.

## Grounding evidence

- `scripts/hil/session.py` already owns strict `load_session()` and
  `revalidate_session()` APIs. Revalidation checks immutable manifest bytes,
  fixture/binding bytes, exact selected probes, DPIDR/AP/FICR identity,
  USB-parent/stable-udev identity, and returns current tty paths.
- `scripts/hil/runner.py` currently resolves identity once, then hardcodes four
  local images, nRF5340 source helper `fw-flash-hil-source`, and environment
  `FW_HIL_SOURCE_JLINK_SERIAL`.
- Current action order is identity, image hashes, source-tty preflight, both
  serial opens, source flash/reset, receiver flash/reset, boot markers/source
  hello, clean-state actions, then row streaming.
- `SerialConsole.path` retains the exact tty opened, so later revalidation can
  fail closed on post-open tty renumbering before a flash or row action.
- Logical board constants already exist in `scripts/hil/model.py`:
  `NRF5340_CPUAPP_BOARD` and `NRF54L15_CPUAPP_BOARD`.
- Phase 2 established local XIAO source image
  `build/hil-source-nrf54l15/zephyr/zephyr.hex` and helper
  `fw-flash-hil-source-54l15`, which requires
  `FW_HIL_SOURCE_NRF54L15_PROBE_SERIAL`.
- Receiver image/helper paths remain
  `build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex`,
  `build/nrf54l15/flpr/zephyr/zephyr.hex`, and `fw-flash-54l15` with
  `FW_NRF54L15_PROBE_SERIAL`.
- Phase 1 tests already prove standalone session creation/loading/revalidation,
  including manifest and identity drift plus safe tty renumbering. Reuse those
  APIs; do not duplicate their schema or identity comparison logic in runner.
- Read-only live preflight on 2026-09-22 still found two XIAO nRF54L15 AAC0
  probes and correlated UART interfaces. Live serial values are not part of
  this committed handoff and must remain external session evidence.

## Exact implementation decisions

### 1. Public CLI and runner API

Touch `scripts/hil/cli.py`.

Add run-only option:

```text
--session-manifest PATH
```

It is optional at argparse level so every existing mixed-fixture invocation is
unchanged. Forward it to `Runner.run()` as
`session_manifest_path=args.session_manifest`. Do not add it to `validate`,
`prepare`, `create-session`, any matrix command, or RH4 artifact execution.

Update CLI module usage text with a session-bound example. Existing exit-code,
signal, stdout, and one-line error behavior stays unchanged.

Extend `Runner.run()` with one trailing keyword:

```python
session_manifest_path=None
```

Do not alter existing positional arguments or defaults.

After fixture/binding validation and run-directory creation, enforce:

- source board `model.NRF54L15_CPUAPP_BOARD` requires a nonempty session path;
- source board `model.NRF5340_CPUAPP_BOARD` forbids a session path;
- unsupported source board still fails closed through existing schema/source
  target checks.

Use stable failure boundary `session` and perform no external command, serial
open, or flash before this compatibility check.

### 2. Injected session boundaries

Touch `scripts/hil/runner.py` and import `hil.session`.

Extend `RunnerDeps.__init__()` with optional `session_loader` and
`session_revalidator`. Defaults are `session.load_session` and
`session.revalidate_session`. Add narrow methods that invoke those callables;
tests may inject process-free fakes, while production uses phase 1 APIs and the
runner's existing discovery command ledger.

Reset a per-run successful-revalidation list in `Runner.run()`. Runner instances
must not leak a manifest or snapshots between calls.

### 3. Session load and evidence

Add one runner step after run-directory creation and compatibility validation:

1. Call injected/default loader with session path, fixture path, and binding
   path.
2. Copy `SessionManifest.raw_bytes` byte-for-byte to
   `<run-dir>/session-devices.json` through existing atomic evidence writer.
3. Re-hash copied bytes and require exact equality with `SessionManifest.sha256`.
4. Write `<run-dir>/session.json` with exact keys:
   - `path`: canonical external manifest path;
   - `sha256`;
   - `session_id`;
   - `created_at_utc`;
   - `fixture_id`;
   - `fixture_sha256`;
   - `binding_sha256`;
   - `roles`: receiver/source selected probe serials.

Never modify, chmod, delete, or replace external manifest. Both copied files
must enter final `SHA256SUMS` and `MANIFEST.md` naturally.

### 4. Revalidation checkpoints and evidence

Add one runner-owned revalidation helper. It calls injected/default
`revalidate_session()` with loaded manifest, exact fixture/binding paths,
validated binding, runner-ledgered discovery command boundary, and configured
sysfs root.

On each success, append one deterministic JSON object to
`session-revalidations.jsonl` and atomically rewrite that evidence file. Object
keys:

```json
{
  "before": "setup identity",
  "sequence": 0,
  "roles": {"receiver": {}, "source": {}}
}
```

`roles` uses the same normalized probe/serial shape already emitted by
`_identity_dict()`. No host timestamp belongs in these records. Sequence starts
at zero and increments by one.

Required successful checkpoint order:

1. `setup identity`
2. `receiver serial open`
3. `source serial open`
4. `source flash`
5. `receiver flash`
6. `row action`

Every call rechecks manifest and fixture/binding bytes through phase 1 API. All
`nix-nrf probes`, read-only CMSIS-DAP OpenOCD, and udev command results continue
into `commands.jsonl` through `_discovery_command`. Initial setup identity also
writes existing `identity.json`, `nrf-probes*.txt`, role udev evidence, and both
CMSIS-DAP text files. Later snapshots must not overwrite that initial evidence.

Map `HilSessionError` or `HilDiscoveryError` to stable runner boundary
`session revalidate: <before>`. A failed later revalidation must leave previous
success snapshots and command evidence intact and must prevent the guarded
action.

### 5. Serial-open integration

For session-bound runs only, `_step_open_consoles()` must:

1. revalidate at `receiver serial open`;
2. create/register/open receiver console using that fresh receiver tty;
3. revalidate at `source serial open`;
4. require returned receiver tty still equals `receiver_console.path`;
5. create/register/open source console using fresh source tty.

If an already-open role resolves to another tty, fail before opening the next
descriptor or issuing any target command. CleanupStack closes any descriptor
already opened. Legacy no-session flow retains current one-resolution behavior
and command order.

### 6. Local image selection

Replace global hardcoded local image tuple with exact source-board selection:

- nRF5340 source:
  - `source-app`: `build/hil-source/app/zephyr/zephyr.hex`
  - `source-cpunet`: `build/hil-source/hci_ipc/zephyr/zephyr.hex`
  - existing receiver cpuapp and FLPR images
- nRF54L15 source:
  - `source-app`: `build/hil-source-nrf54l15/zephyr/zephyr.hex`
  - existing receiver cpuapp and FLPR images
  - no source CPUNET or merged image

Pass validated fixture into image step. Keep order above in `images.json`.
Artifact mode remains legal only for nRF5340 source. Reject any artifact set for
nRF54L15 source at `hash images` before serial or target action. Do not change
`scripts/hil/artifacts.py` or archive schemas.

### 7. Flash and row-action integration

Pass fixture/source-board information plus optional session revalidation
callback into `_step_flash()`.

For session-bound runs:

- After source `mark_rx()` and immediately before source helper, revalidate at
  `source flash`; require both current tty paths equal opened console paths.
- nRF54L15 source command is exactly `fw-flash-hil-source-54l15` with environment
  containing only `FW_HIL_SOURCE_NRF54L15_PROBE_SERIAL=<selected serial>`.
- After receiver `mark_rx()` and immediately before receiver helper, revalidate
  at `receiver flash`; require both current tty paths still equal opened paths.
- Receiver helper/environment stays `fw-flash-54l15` and
  `FW_NRF54L15_PROBE_SERIAL=<selected serial>`.

For no-session nRF5340 runs, preserve current helper, environment, artifact
overrides, order, logs, and behavior exactly.

After both flashes, require existing full receiver boot-marker gate and source
`hello()` gate. Then, before `_step_clean_state()` sends its first receiver or
source command, revalidate at `row action` and require both tty paths still match
opened descriptors. This one checkpoint guards one checked-in row invocation;
do not add per-shell-command rediscovery inside streaming.

The session manifest loader already validates expected receiver boot contract
and source firmware/protocol values. Existing boot-marker gate plus source
`hello()` exact identity validation remain authoritative. Do not weaken either.

### 8. Tests

Touch `tests/hil/rh2_test.py`, `scripts/test_hil_runner.py`, and
`tests/hil/hil_fakes.py` only as needed. Extend current harnesses; do not create
a parallel test framework.

Required observable coverage:

- CLI accepts `--session-manifest` only on `run` and forwards exact path.
- Existing no-session mixed fixture keeps current four images, nRF5340 helper,
  env, command order, and evidence.
- XIAO fixture without a manifest fails at `session` before any command, serial
  open, or flash.
- Non-XIAO source fixture with a manifest fails before external action.
- Session load copies exact manifest bytes, records manifest SHA/role serials,
  and final evidence hashes session files.
- Session-bound passing fake row emits exactly six successful checkpoint names
  in required order.
- Fake command/console event order proves each checkpoint precedes corresponding
  receiver/source open, source/receiver flash helper, and first clean-state
  serial write.
- Safe tty renumbering before serial open uses new paths.
- Tty path drift after open fails before source flash, receiver flash, or row
  action as applicable and closes owned consoles.
- Manifest, fixture/binding, probe/AP/FICR, USB, or udev drift surfaced by the
  injected/default revalidator prevents guarded action and retains prior
  evidence.
- XIAO local `images.json` contains exactly source app plus receiver cpuapp/FLPR;
  missing XIAO source image fails `hash images`.
- XIAO source flash uses exact helper/env and no J-Link/CPUNET artifact override.
- Boot/hello failure still blocks `row action`; row action cannot occur before
  both firmware gates.
- Existing cancellation, cleanup, artifact, matrix, discovery, and nRF5340
  source tests remain unchanged and passing.

Tests must assert public files, argv/env, serial paths/writes, lifecycle outcome,
and externally visible fake event order. Avoid assertions about private helper
call counts or implementation-only object identity.

## Verification

Run sequentially from repository root:

```bash
nix develop -c python3 scripts/test_hil_runner.py
nix develop -c python3 tests/hil/rh2_test.py
nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
nix develop -c fw-build-hil-source-54l15
nix develop -c fw-build-hil-source
nix develop -c backlog doctor
git diff --check
```

Inspect both source-build logs fully. Only repository-documented NCS
informational diagnostics may remain; no new compiler, Kconfig assignment,
CMake, OpenOCD, or host-tool warning is acceptable. No hardware command belongs
to phase 3A.

## Files allowed

- `scripts/hil/runner.py`
- `scripts/hil/cli.py`
- `tests/hil/rh2_test.py`
- `scripts/test_hil_runner.py`
- `tests/hil/hil_fakes.py` only when fake boundary support is needed
- PB-033 task file for implementation notes
- this handoff

Do not touch session schema/identity rules, source/receiver firmware, rows,
fixtures, build/flash helpers, matrix coordinator, release/artifact code,
product acceptance criteria, accepted historical evidence, or public docs.

## Commit and escalation

After verification, inspect status, complete diff, and recent log. Stage only
allowed files and commit phase 3A with a concise repository-style message
containing `PB-033`. Do not push, open/merge PR, amend, or add AI/tool
attribution.

Return files changed; public behavior; exact test/build results; warning audit;
commit hash/message; deviations; blockers; and phase 3B readiness.

Stop without committing partial work and report to Delegator if two materially
different fixes fail, runtime evidence contradicts this design, safe ordering
cannot be preserved, artifact compatibility would require schema changes, a
warning cannot be explained, scope must expand, assertions would need
weakening, or any target-changing command appears necessary.
