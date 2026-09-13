# System HIL RH2 two-device orchestration handoff

Status: implementation handoff. RH0 host contracts and RH1 source firmware are
accepted for no-hardware development. RH2 adds a dry-run-verifiable hardware
orchestrator, but this handoff forbids live probe, serial, flash, reset,
Bluetooth, sudo, erase, or recovery access.

## Goal

Add one standalone pytest command that can later own the exact two-device
fixture and run one short mono `48_4_1` source-to-receiver session. Before that
hardware session, prove all orchestration behavior with fakes:

- exact role and serial identity resolution;
- exact source and receiver image hashing, flash argv, and per-image verify;
- both consoles armed before either target reset;
- controlled receiver/source bond cleanup without blanket erase;
- source HIL1 command/response and asynchronous run tracking;
- live receiver status collection during source tail;
- bounded cleanup after pass, failure, timeout, SIGINT, and SIGTERM;
- retained flat evidence, `MANIFEST.md`, `SHA256SUMS`, and JUnit XML;
- nonzero process status on any failed boundary.

Public behavior proved by this phase: fake lab boundaries observe exact safe
ordering and no owned stream, serial descriptor, process, or fixture lock
survives any exit path. No hardware acceptance verdict is possible in RH2.

## Scope

Touch only HIL runner, dedicated source flash tooling, receiver identity shell
surface/tests, HIL tests, and this handoff's direct comments/docs:

- `scripts/hil/`;
- `scripts/hil-runner.py`;
- new `scripts/bin/fw-flash-hil-source`;
- `src/bt_shell.c`;
- `tests/unit/audio_shell/` and `tests/unit/bt_shell_pairing/` as needed to pin
  receiver identity output;
- new `tests/hil/rh2_test.py`, `tests/hil/rh2_hardware_test.py`, and local
  fake/fixture helpers;
- `tests/hil/fixture.local.example.json` only if comments or placeholders need
  clarification without changing schema;
- `scripts/test_hil_runner.py` only for RH0 regression caused by a public API
  extension;
- `scripts/test_inventory.py`, `scripts/test-all.sh`, or committed coverage
  baseline only if receiver shell tests necessarily change canonical inventory
  or measured production coverage. Do not lower any baseline.

## Non-scope

- No live hardware access in this implementation session.
- No creation of `tests/hil/fixture.local.json`.
- No actual flash, reset, serial open, Bluetooth connection, `btattach`, sudo,
  erase, recovery, DTR/RTS, audio capture, or audibility test.
- No RH3 120-second matrix, reconnect row, FLPR fault injection, release asset,
  analog, mono-capture, stereo, DAC, wiring, or publication work.
- No changes to source firmware protocol, codec signal, receiver audio path,
  pairing policy, or accepted RH1 state machine.
- No static probe-serial-to-board mapping in source, fixtures, docs, or tests.
- Preserve user-owned
  `docs/development/firmware-release-fr4-summary-wait-fix-handoff.md` and all
  unrelated dirty work.
- No commit, push, merge, amend, PR, or git-config change. Current HIL track is
  intentionally uncommitted as one user-owned worktree.

## Grounded current behavior

### Existing host layer

- `scripts/hil/model.py` strictly validates logical/binding schema version 1.
  RH0 currently says both probe backends are `nrf-probes`. Hardware/tool
  evidence invalidates that source-side assumption: `nrf-probes` enumerates
  CMSIS-DAP only, while source nRF5340DK uses onboard Segger J-Link. Correct
  physical binding in RH2 as described below. Serial identity remains stable
  udev properties, not `/dev/tty*`.
- `scripts/hil/protocol.py` parses `HIL1 ` lines and enforces exact run state,
  segment, abort, command-ID, counter-regression, and terminal rules.
- `scripts/hil/lifecycle.py` provides `CleanupStack`, token-checked fixture
  lock, safe output-root validation, and no-overwrite run-directory creation.
- `scripts/hil/evidence.py` currently supports flat run directories and
  transactional `MANIFEST.md`/`SHA256SUMS` finalization. Keep flat evidence in
  RH2; do not add subdirectories or recursive hashing.
- `scripts/hil/cli.py` exposes no-hardware `validate` and `prepare`. Preserve
  those commands and outputs unchanged.

### Exact source protocol

Wire input is one UTF-8 line:

```text
hil {"protocol_version":1,"command":"...","command_id":"...","run_id":"..."}\n
```

All commands require the common four fields. Exact additional fields:

- `unpair`: `peer_address`, `peer_address_type`;
- `configure`: `peer_address`, `peer_address_type`, `mode`, `profile`,
  `scored_sdu_count`, `signal_seed`, `reconnect_policy`;
- `hello`, `idle`, `start`, `stop`, `status`: no extras.

Address is uppercase `XX:XX:XX:XX:XX:XX`; type is `public` or `random`. RH2 row
uses `mode=mono`, `profile=48_4_1`, `scored_sdu_count=120`, fixed
`signal_seed=1218649181`, and `reconnect_policy=none`. This is a short
orchestration witness, not RH3 duration acceptance.

Source serial is intentionally quiet: empty prompt, no echo, no logs. `hello`
is its boot/readiness/identity boundary. Require firmware ID
`le-audio-hil-source-rh1`, protocol 1, active false, state idle, canonical
identity/type, and frozen constants. Source `status` queries during a run must
use a command ID distinct from start, while async ACK/state/terminal records
use start command ID. Diagnostic `boot/boot` and `parse-error/unbound` records
fail preflight/run; never feed them to active `HilRunTracker`.

### Receiver serial and identity

Receiver shell uses default `uart:~$ ` prompt and echo. Treat command output as
a bounded transcript window terminated by return of prompt after command echo.
Capture every raw byte to receiver log before parsing.

NCS v3.3.0 provides `bt_id_get()` in
`zephyr/include/zephyr/bluetooth/bluetooth.h` and public
`bt_addr_le_to_str()`/`BT_ADDR_LE_STR_LEN` in
`zephyr/include/zephyr/bluetooth/addr.h`. Add zero-argument `bt identity` in
`src/bt_shell.c`:

- call `bt_id_get()` after existing boot order has run `bt_enable()` then
  `settings_load()`;
- require at least one non-`BT_ADDR_LE_ANY` identity;
- print exactly `Identity: XX:XX:XX:XX:XX:XX (public|random)` for identity 0;
- on no usable identity, print shell error `Identity unavailable.` and return
  `-ENOENT`;
- no mutation, no controller command, no settings write.

Add `bt bonds` for the nRF54L15 feature-on pairing-control build only. It must
call `bt_foreach_bond(BT_ID_DEFAULT, ...)` and print exactly `Bond count: N`.
It is read-only. Feature-off builds return shell error `Bond inventory
unavailable.` and `-ENOTSUP`; RH2 never targets that path.

Pin exact output in both feature-off `audio_shell` and feature-on
`bt_shell_pairing` suites using a narrow test seam for identity retrieval if
needed. Do not mock string formatting itself. Existing `bt unpair` text stays
byte-identical. RH2 parses `bt identity`; it does not reuse historical address.

Receiver boot requires `BLE ready`, `settings_load() OK`, I2S readiness, FLPR
READY/ACK/offload/runtime readiness, and `Advertising as "LE Audio Receiver"`.
Any warning/error/assert/fault before row start fails. Clean-state command for
nRF54L15 requires exact `bt unpair` success:

```text
Pairing reset complete: bonds cleared; BONDING advertising active.
```

Live tail commands, each bounded and prompt-terminated:

```text
audio status
audio perf
flpr offload
flpr status
```

Require `Decode errors`, `I2S underruns`, `Stream resets`, and `Push failures`
all present and zero using `scripts/flpr_status.py` public parsers. Require
offload status present, state `ACTIVE`, `submit == success`, fallback/busy and
all fault/recovery-failure counters zero for 10 ms. Require FLPR handshake
Ready/ACKed/Healthy all yes with every error/loss/dup/ooo/missed counter zero.
After teardown require at least one parseable receiver stream summary:

```text
Stream[N] summary: SDUs=N decoded=N plc=N decode_err=N i2s_underrun=N stream_reset=N empty_sdu=N
```

For RH2, exact SDU/decoded/PLC limits are evidence only except that summary
must exist and `decode_err`, `i2s_underrun`, and `stream_reset` must be zero.
RH3 freezes long-run packet/PLC limits.

### Images and flash backends

Hash and record these exact build-tree inputs before any flash:

- source app: `build/hil-source/app/zephyr/zephyr.hex`;
- source cpunet: `build/hil-source/hci_ipc/zephyr/zephyr.hex`;
- receiver cpuapp: `build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex`;
- receiver FLPR: `build/nrf54l15/flpr/zephyr/zephyr.hex`.

New `fw-flash-hil-source` mirrors `fw-flash-dongle` but uses HIL source paths,
programs cpunet first then cpuapp, applies `verify` to both, then `reset run`.
Use onboard J-Link through `interface/jlink.cfg`. Require explicit
`FW_HIL_SOURCE_JLINK_SERIAL`; no auto-detection in HIL helper. Validate serial
against `^[[:alnum:]_.:-]+$`. Never invoke `nrf-probes` from this helper:
repository evidence shows nRF5340DK source uses onboard Segger J-Link, while
`nrf-probes` enumerates CMSIS-DAP probes.

Add public fake-tool tests for exact source helper argv, net-before-app order,
both verifies, invalid override, missing artifacts, missing dev shell, and
OpenOCD nonzero propagation. These tests live in `tests/hil/rh2_test.py`; do
not name them `test_*.py`, which current canonical inventory would discover.
Do not add a canonical unit-suite child for standalone HIL orchestration.

Receiver flash command is existing `fw-flash-54l15`; it already loads and
immediately verifies cpuapp then FLPR and ends with reset run. Runner invokes
it only through injected command execution and records stdout/stderr/status.
Do not alter helper in RH2 unless a fake proves a concrete safety defect.

## Exact implementation shape

### 1. Correct physical probe schema

Update binding role probe shape before adding discovery. Preserve root
`schema_version: 1` because no durable physical binding has shipped and current
checked example contains placeholders. Exact role-specific shapes:

```json
"receiver": {
  "probe": {"backend":"nrf-probes","family":"nrf54l"},
  "serial": {"baud":115200,"udev":{}}
}
```

```json
"source": {
  "probe": {
    "backend":"jlink",
    "family":"nrf53",
    "udev": {
      "ID_VENDOR_ID":"REPLACE_WITH_HEX_VENDOR",
      "ID_MODEL_ID":"REPLACE_WITH_HEX_PRODUCT",
      "ID_SERIAL_SHORT":"REPLACE_WITH_STABLE_SERIAL"
    }
  },
  "serial": {"baud":115200,"udev":{}}
}
```

Probe udev uses same strict key/value rules as serial udev, except tty-path
checks are irrelevant. Require vendor/model plus `ID_SERIAL_SHORT` or
`ID_PATH`. Static probe serial remains forbidden as a dedicated field; serial
is discovered from current matching USB device. Update example and RH0 public
schema tests. Require source probe and source serial matched udev records to
share exact nonempty `ID_SERIAL_SHORT`; this proves both interfaces belong to
same onboard J-Link composite device. Do not weaken to product-name matching.

### 2. Discovery module

Add `scripts/hil/discovery.py` with frozen result records and injectable command
runner/sysfs root.

`resolve_fixture(binding)` must:

1. run plain `nrf-probes` once, retain exact stdout/stderr/status as raw
   identity evidence, and parse table rows without accepting truncated or
   unknown fields;
2. resolve receiver with `nrf-probes --find nrf54l`, require exactly one token,
   and prove plain table row says target `nRF54L15`, PART `0x00054b15`, plus
   nonempty DPIDR and VARIANT;
3. resolve source onboard J-Link by exact probe udev map, require exactly one
   USB device and obtain current USB serial. Run an injected read-only OpenOCD
   J-Link fingerprint using that explicit serial. It must emit retained marker
   lines for DPIDR, AP0..AP3 IDRs, FICR INFO.PART at `0x00FF020C`, and VARIANT
   at `0x00FF0210`; require nRF53 CTRL-AP signature, PART `0x00005340`, nonempty
   DPIDR/VARIANT, and zero OpenOCD warning/error. Do not pass receiver
   CMSIS-DAP serial to J-Link;
4. enumerate candidate tty nodes under `/sys/class/tty`, run
   `udevadm info --query=property --name <node>` through injected runner, require
   exact equality for every configured udev key, and require exactly one tty per
   role;
5. reject same tty, same USB parent, missing/duplicate candidates, unknown
   probe targets, unresolved source J-Link, or identity drift;
6. return role objects containing canonical tty path, baud, probe backend,
   current probe serial, target/family, DPIDR/PART/VARIANT when available, and
   complete matched udev properties.

Current `nrf-probes` implementation at
`/nix/store/...-nrf-probes/bin/.nrf-probes-wrapped` provides grounding for
read-only marker shape and addresses, but do not import Nix-store internals.
Implement repository-owned minimal Tcl marker commands through OpenOCD's
`interface/jlink.cfg` and `target/nordic/nrf53.cfg`. If exact source
probe-to-console correlation cannot be proven from shared `ID_SERIAL_SHORT`,
stop and escalate. Do not use J-Link auto-detection as identity proof.

### 3. Serial ownership

Add `scripts/hil/serial_io.py`:

- `SerialConsole` owns one `pyserial.Serial` and one reader thread;
- open with exact resolved path, baud, 8N1, no flow control, timeout at most
  100 ms, and exclusive mode when supported;
- register close immediately; close requests reader stop, closes descriptor to
  unblock read, joins with bounded timeout, then reports failure if thread lives;
- append raw received bytes to one role-specific flat evidence file and publish
  complete decoded lines to a condition-protected queue;
- decode for parsing as strict UTF-8; undecodable bytes fail row but raw bytes
  remain retained;
- expose `wait_line(predicate, timeout)`, `command_receiver(text, prompt,
  timeout)`, and source raw-line iteration; all waits poll cancellation no
  slower than 100 ms;
- never pulse DTR/RTS. Construct `serial.Serial(port=None)`, set baud/8N1,
  `dtr=False`, `rts=False`, timeouts, then assign port and call `open()` so
  requested deasserted states exist before backend open. Fake must prove no
  true/assert pulse or post-open state change. If pyserial/backend cannot honor
  this sequence, escalate.

Console capture must be opened and reader-ready before either flash helper can
run. Fake event ledger tests must prove this ordering.

### 4. Source client

Add `scripts/hil/source_client.py` using `SerialConsole` and existing protocol
parser. It owns monotonically unique command IDs under one safe run ID.

- Serialize compact JSON with ASCII only and one newline. Reject a full shell
  line longer than current firmware shell capacity: `len("hil " + json) < 512`.
- For synchronous commands, wait for exactly one matching kind `status` record
  and require `data.command`, `ok:true`, `error:"ok"`; unrelated HIL1 records
  route to active run tracker or fail if unsolicited.
- `hello()` validates identity/constants and returns immutable snapshot.
- `idle()`, `unpair()`, `configure()` and `stop()` enforce exact response.
- `start()` registers cleanup that issues bounded `stop` before writing START,
  sends command, requires ACK quickly, then drives `HilRunTracker` until
  `scored_complete` or terminal.
- At `scored_complete`, provide one callback/hook so runner executes receiver
  tail status commands while source remains active. Then continue to terminal.
- Require terminal PASS and final `status` snapshot: inactive, teardown,
  verdict pass, first_errno 0, expected mode/profile/reconnect, stream_count 1,
  scored counter exactly 120, send failures 0, outstanding 0, connected false,
  group false, security level at least 2, security error 0, one sink ASE, and
  bond count 1.
- Timeout/cancel calls `stop` once, waits bounded for error-abort terminal, then
  runs `idle`; cleanup failure changes row verdict to fail.

### 5. Warning scan and receiver parser

Add `scripts/hil/receiver.py` or equivalent focused module:

- strict regex for identity and stream summary;
- prompt-bounded receiver shell commands;
- parsers for FLPR handshake block and existing audio/offload parsers;
- warning scanner over both source unprefixed lines and receiver raw log.

Fail on `LOG_WRN`, `LOG_ERR`, `FATAL`, assertion, fault, stack overflow,
`i2s_nrfx: Next buffers not supplied on time`, `Cannot write in state`, ISO
sequence discontinuity, unexpected recovery, malformed HIL1, command timeout,
or unexplained shell error. Keep allowlist empty for healthy RH2 row. Normal
OpenOCD informational output is not firmware log input, but any OpenOCD line
containing warning/error/recovery/verify failure fails flash boundary.

### 6. Runner and pytest boundary

Add `scripts/hil/runner.py` with injected interfaces for command execution,
discovery, serial factory, clock/wait, and signal cancellation. Production
method runs exactly one row:

1. validate fixture/binding/output root/run ID;
2. acquire fixture lock;
3. create run directory and immediately retain exact fixture/binding bytes;
4. resolve identities and write raw identity/environment evidence;
5. hash exact image inputs and write image provenance;
6. preflight source tty ownership with `lsof -- <tty>`; any holder, including
   `btattach`, fails before serial or flash;
7. open both consoles and wait until both reader-ready events are set;
8. invoke source and receiver flash helpers, retaining command argv, output,
   status, and duration;
9. wait for receiver boot markers and source `hello`;
10. run receiver `bt identity`, derive live peer address/type, receiver
    `bt unpair`, receiver `bt bonds` and require exact count zero, source
    `idle`, source `unpair` for exact receiver identity, then repeat
    hello/identity checks to prove both bond counts zero;
11. configure/start one short mono row;
12. at scored-complete collect receiver live status blocks;
13. await source terminal/final status, receiver stream summary, source idle;
14. scan all logs and validate counters;
15. close resources through one `CleanupStack`, finalize evidence, release lock.

Implement `run` CLI:

```text
python3 scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /absolute/outside/repo \
  --run-id SAFE_ID \
  --junit /absolute/outside/repo/SAFE_ID.junit.xml
```

CLI `run` is production hardware path and must have no `--skip-verify`,
`--ignore-warning`, `--no-flash`, arbitrary command, arbitrary image path, or
erase/recovery flag. Existing `validate`/`prepare` remain no-hardware.

Default fake suite entry point is `tests/hil/rh2_test.py`. Production fixture
entry point is separate `tests/hil/rh2_hardware_test.py`, skipped unless
explicit environment flag `HIL_RUN_HARDWARE=1` is set. When enabled, require
all CLI paths above from environment or arguments and call public runner once.
Neither filename may match canonical inventory's `test_*.py` pattern. Fake
tests run by default with no hardware.

### 7. Evidence and JUnit

Keep each run directory flat. Suggested mandatory names:

- `fixture.json`, `binding.json`, `environment.json`, `identity.json`;
- `nrf-probes.txt`, `receiver-udev.txt`, `source-udev.txt`;
- `images.json` with path, size, SHA-256, logical image, build-info identity;
- `source-flash.log`, `receiver-flash.log`;
- `source-console.bin`, `receiver-console.bin`;
- `source-records.jsonl`, `receiver-status.txt`;
- `commands.jsonl` with argv, start/end UTC, monotonic duration, exit status;
- `result.json`, `junit.xml`, `MANIFEST.md`, `SHA256SUMS`.

Put requested external JUnit path and an identical `junit.xml` inside run
evidence. Emit one testcase named `rh2.short_mono_48_4_1`; failure message names
first failed boundary and cleanup failures. XML must be well formed and escaped.

Extend evidence outcome to `passed`, `failed`, or `cancelled` for `run`, while
preserving `prepared` behavior. Finalization occurs after cleanup result is
known; cleanup failure forces `failed`. If setup fails after run directory
creation, retain all evidence available and finalize failed. If finalization
itself fails, return nonzero and do not delete or overwrite primary evidence.
`MANIFEST.md` verdict must never say `TRANSPORT_RUNTIME_ACCEPTED` in RH2.

Environment provenance: repo HEAD and dirty status, NCS version/path, west,
Python, pytest, pyserial, OpenOCD, `nrf-probes`, host/kernel, UTC timestamps,
and exact command argv/status. Dirty tree is recorded, not accepted as exact
release provenance.

## Tests

All RH2 tests call public boundaries with fake tools/sysfs/serial and temporary
output outside repo. Minimum named behavior:

1. unique receiver CMSIS-DAP, source J-Link, and two tty endpoints resolve;
2. missing/duplicate/wrong PART/variant, shared USB parent, shared tty, changed
   udev property, malformed `nrf-probes`, and source J-Link ambiguity fail before
   open/flash;
3. source flash helper exact argv: cpunet program+verify before cpuapp
   program+verify, then reset/shutdown; invalid/missing/tool failure propagates;
4. serial readers both armed before first flash/reset ledger event;
5. pyserial open/read/decode/thread-close failures preserve logs and release all
   ownership;
6. source command JSON exact fields/length, hello constants, ACK/state/terminal,
   final counters, and unrelated/duplicate/malformed records;
7. receiver identity/prompt/status/summary parsing, missing fields, malformed
   numbers, and each warning signature;
8. passing fake row emits all evidence, PASS JUnit, hashes, no lock/process/
   descriptor/thread leak;
9. failure after every side effect emits failed evidence, FAIL JUnit, nonzero
   CLI status, and full ordered cleanup;
10. source timeout, receiver timeout, flash nonzero, SIGINT, and SIGTERM call
    stop/idle boundedly and retain evidence;
11. cleanup failure changes an otherwise passing row to failed and names all
    cleanup errors;
12. `HIL_RUN_HARDWARE` absent means no probe/serial/flash command can execute;
13. existing `validate` and `prepare` outputs/regressions remain exact.

Tests must assert public filesystem, process argv/status, serial transcript,
exit code, and JUnit/result behavior. Do not assert private callback counts or
private data structures except fake event ledger ordering visible at boundary.

## Verification

Run without hardware:

```bash
nix develop --command pytest -q tests/hil/rh2_test.py
nix develop --command python3 -W error scripts/test_hil_runner.py
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/audio_shell -p native_sim/native/64 --inline-logs --outdir /tmp/hil-rh2-audio-shell
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/bt_shell_pairing -p native_sim/native/64 --inline-logs --outdir /tmp/hil-rh2-bt-shell-pairing
nix develop --command python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil/rh2_test.py tests/hil/rh2_hardware_test.py
nix develop --command python3 scripts/test_inventory.py --count
nix develop --command python3 scripts/test_inventory.py --json
nix develop --command fw-build-54l15
nix develop --command fw-build-hil-source
git diff --check
git status --short
```

Do not run full canonical gate on dirty tree; coverage child requires clean
provenance. Do not run any live `hil-runner.py run` command.

Inspect both pristine build logs and resolved configs. Zero actionable compiler,
Kconfig, CMake, linker, boot, OpenOCD-fake, Python, ResourceWarning, or pytest
warnings. Existing documented NCS informational diagnostics remain classified,
not normalized as new warnings.

## Escalation

Stop and report before guessing when:

- source J-Link cannot be correlated uniquely to stable source USB/serial
  binding without adding a static probe mapping;
- pyserial cannot open source shell without implicit DTR/RTS reset;
- receiver prompt framing is ambiguous in fake recorded transcripts;
- exact RH1 source firmware output contradicts host parser;
- evidence finalization cannot retain failure evidence transactionally;
- two materially different attempts fail for same blocker;
- fix requires source protocol change, test weakening, architecture invention,
  hardware access, destructive action, or scope expansion.

Preserve partial work, do not commit, and return exact blocker, attempts,
commands/errors, diff/status, one precise question, and smallest hypothesis.

## Recap

Return files changed, public behavior, exact tests/builds and results, warnings,
inventory count, deviations, blockers, final git status, and explicit no-live-
hardware/no-commit confirmation.
