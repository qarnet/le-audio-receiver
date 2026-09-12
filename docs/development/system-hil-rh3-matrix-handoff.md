# System HIL RH1 closeout and RH3 matrix handoff

Status: implementation handoff. Host-only work. No live hardware access or
acceptance claim.

## Goal

Close remaining RH1 review/configuration gaps, then add one fail-closed RH3
matrix coordinator that executes every checked-in RH3 row twice in fixed safe
order. Keep direct one-row execution and RH2 default behavior unchanged.

User-observable behavior:

- nRF5340 source CPUNET emits no banner, console, printk, or early-console
  traffic; source CPUAPP remains sole HIL1 serial-protocol owner;
- one explicit matrix command runs two complete passes, starts each pass from a
  fresh-pair row, runs all healthy rows before reconnect/fault rows, stops on
  first failure, and retains matrix-level plus row-level evidence;
- no matrix result can claim hardware acceptance unless every child row passed;
- host fake tests prove order, pass isolation, fail-fast behavior, aggregate
  JUnit/evidence, cleanup, and unchanged direct RH2 default.

## Scope

Touch only:

- `hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf`;
- `scripts/hil/` and `scripts/hil-runner.py`;
- `tests/hil/` and `scripts/test_hil_runner.py` when required;
- stale HIL module/wrapper comments;
- this handoff and a concise RH1/RH3 software-status note if needed.

Preserve all unrelated dirty work. Do not modify receiver audio firmware,
release packaging, analog capture, fixture binding, probe mappings, or
canonical coverage baseline.

## Non-scope

- No probe, serial, flash, reset, Bluetooth, sudo, erase, recovery, RF, or live
  HIL execution.
- No creation of `tests/hil/fixture.local.json`.
- No `TRANSPORT_RUNTIME_ACCEPTED`, RH4, mono-output, stereo-output, release, or
  publication claim.
- No arbitrary row list, repetition count, skip, warning-ignore, no-flash,
  image-path, command, erase, or recovery option.
- No commit, push, merge, amend, or PR. Current HIL track remains one
  user-owned uncommitted worktree.

## Grounded current state

### RH1 review fixes already present

`hil/source/app/src/hil_source_app.c`,
`hil/source/app/src/hil_source_bap.c`, and
`tests/unit/hil_source_app/src/test_hil_source_app.c` already contain third
review fixes from
`docs/development/system-hil-rh1b-review-fix3-handoff.md`:

- bonded reconnect updates `security_level_now` after
  `bt_conn_get_security()`;
- start ACK failure terminalizes without recursive mutex locking;
- idle error responses propagate emission failure;
- active parse-error emission runs under app mutex;
- temporary connection unrefs occur outside backend spinlock;
- native regressions exist for start ACK failure, idle response failure, and
  active parse errors.

Do not redesign those paths. Verification closes software review status.

### CPUNET console conflict

Current resolved `build/hil-source/hci_ipc/zephyr/.config` has:

```text
CONFIG_CONSOLE=y
CONFIG_NCS_BOOT_BANNER=y
CONFIG_UART_CONSOLE=y
CONFIG_STDOUT_CONSOLE=y
CONFIG_PRINTK=y
CONFIG_EARLY_CONSOLE=y
```

This conflicts with source quiet-console/HIL1 contract. NCS v3.3.0 grounding:

- `nrf/lib/boot_banner/Kconfig`: NCS banner selects printk/early console;
- `zephyr/boards/nordic/nrf5340dk/nrf5340dk_nrf5340_cpunet_defconfig`:
  CPUNET console defaults;
- `zephyr/samples/bluetooth/hci_ipc/src/main.c`: HCI transport uses IPC
  endpoint, not UART;
- `zephyr/samples/bluetooth/hci_uart/prj.conf` and
  `nrf/samples/openthread/coprocessor/extra_conf/rcp_hci.conf`: established
  silent-console configuration.

Keep `CONFIG_SERIAL=y`, IPC service, MBOX, HCI RAW, ISO, and SW Split settings.

### Existing row catalog and one-row runner

`scripts/hil/rows.py` owns immutable RH2/RH3 rows. `scripts/hil/runner.py`
executes one row and writes complete row evidence. `scripts/hil/cli.py --row`
selects one row; omitted `--row` remains `rows.RH2_ROW`.

RH3 fixed order per pass:

1. all `rows.RH3_HEALTHY_ROWS` in declared order;
2. `rows.RH3_RECONNECT_ROW`;
3. `rows.RH3_HANG_ROW`;
4. `rows.RH3_STALL_ROW`.

First healthy row is fresh pair, so every pass independently re-establishes
clean state through public unpair commands. Preserved-bond and follow-on rows
consume bond established by prior fresh rows. Do not reorder.

## Required implementation

### 1. Silence source CPUNET

Append exact settings to
`hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf`:

```conf
CONFIG_NCS_BOOT_BANNER=n
CONFIG_BOOT_BANNER=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_STDOUT_CONSOLE=n
CONFIG_PRINTK=n
CONFIG_EARLY_CONSOLE=n
```

Pristine source build must resolve all seven as not set with no assigned-value
or compiler warning. Do not disable `CONFIG_SERIAL`.

### 2. Freeze matrix contract

Add immutable matrix helpers in `scripts/hil/rows.py`:

- `RH3_PASS_ROWS`: healthy tuple plus reconnect, hang, stall;
- `RH3_PASS_COUNT = 2`;
- helper returning deterministic `(pass_index, row_index, row)` schedule, with
  pass indices 1 and 2 and row indices starting at 1.

Do not expose mutable lists or arbitrary schedule construction.

### 3. Matrix coordinator

Add focused `scripts/hil/matrix.py`. Keep `Runner` as one-row owner. Matrix
coordinator receives an injected row-run callable/factory so fake tests never
touch hardware.

Public production method inputs:

- fixture path;
- binding path;
- absolute output root outside repository;
- safe matrix run ID;
- external aggregate JUnit path;
- argv/status provenance.

Exact behavior:

1. validate fixture, binding, output root, and matrix run ID before child work;
2. create one matrix directory named by matrix run ID using existing no-clobber
   lifecycle rules;
3. copy exact fixture/binding bytes and write fixed matrix schedule before first
   child row;
4. each child row uses a deterministic safe ID derived from matrix ID, pass,
   and row index/name, and writes under a dedicated child output root outside
   matrix directory so current flat row evidence/finalizer stays unchanged;
5. invoke existing public one-row runner once per scheduled row;
6. never start pass 2 until pass 1 fully passed; never start reconnect/fault
   rows until all healthy rows in same pass passed;
7. stop on first failed/cancelled child; preserve completed child evidence and
   matrix partial result;
8. record for every attempted child: pass/index, row name, child run ID,
   outcome, first boundary, cleanup failures, child evidence path, child JUnit
   path, start/end UTC, and duration;
9. matrix outcome is passed only when every fixed child passed; cancelled child
   yields cancelled matrix, any failure/cleanup/evidence error yields failed;
10. write aggregate `schedule.json`, `children.jsonl`, `result.json`,
    `junit.xml`, requested external JUnit, `MANIFEST.md`, and `SHA256SUMS`;
11. aggregate JUnit contains one testcase per scheduled row attempted, named
    `passN.<row.name>`. Unattempted rows after fail-fast appear as skipped with
    explicit prior-child failure reason, so fixed expected schedule remains
    visible;
12. no acceptance verdict string. Result may state software execution outcome
    only (`passed`, `failed`, `cancelled`).

Use existing atomic evidence helpers and manifest semantics. No symlinks, nested
manifest traversal, overwrite, or deletion of child evidence. Matrix directory
may contain only aggregate files; child row directories retain existing flat
layout.

Resource ownership: each child `Runner.run()` owns and releases fixture lock,
consoles, processes, and source cleanup before returning. Matrix coordinator
must own no hardware descriptor and must not hold fixture lock across child
runs. This permits every child to prove complete cleanup and fresh discovery.

### 4. CLI

Add explicit subcommand:

```text
python3 scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /absolute/outside/repo \
  --run-id SAFE_MATRIX_ID \
  --junit /absolute/outside/repo/SAFE_MATRIX_ID.junit.xml
```

No `--row` or repeat option for matrix command. Existing `run` CLI and RH2
default stay byte-compatible. SIGINT/SIGTERM use same bounded cancellation
event; do not start another child after cancellation.

Process status: 0 passed, 1 failed, 130 cancelled, 2 CLI/setup error.

### 5. Tests

Extend `tests/hil/rh2_test.py` or add non-canonical
`tests/hil/rh3_matrix_test.py` with fake-only public-boundary coverage:

- exact two-pass schedule and fixed healthy-before-follow-on order;
- first child of each pass is fresh;
- unique deterministic child IDs and paths;
- pass 2 waits for complete pass 1;
- reconnect/fault rows never start after healthy failure;
- failure and cancellation stop further child execution;
- aggregate result/JUnit expose attempted and skipped rows correctly;
- child cleanup failure fails matrix;
- matrix finalization failure preserves child evidence and returns failure;
- output-root/run-ID/no-overwrite validation;
- direct `run` still defaults RH2 and explicit `--row` still works;
- no fake invokes subprocess, probe, serial, flash, Bluetooth, or hardware.

Prefer outcome assertions through public coordinator/CLI boundaries. Do not
assert private container identity or helper-call count except where exact child
schedule is public behavior.

### 6. Documentation reconciliation

- Update `scripts/hil-runner.py` and `scripts/hil/__init__.py` stale RH0-only
  descriptions.
- Add concise status note to this handoff or a new result note: RH0/RH1
  software complete after verification; RH2/RH3 fake orchestration complete;
  live RH2/RH3 and every acceptance verdict remain blocked pending explicit
  hardware run and real local binding.
- Do not mark milestone plan accepted or executed.

## Verification

Run from repository dev shell where required:

```bash
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-rh3-matrix
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-rh3-matrix
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-rh3-matrix
nix develop --command fw-build-hil-source
python3 tests/hil/rh2_test.py
python3 -W error scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/rh2_hardware_test.py
python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
git diff --check
git status --short
```

Inspect resolved app and child `.config`. CPUNET seven silent-console symbols
must all be not set. App shell/HIL1 settings must remain enabled. Treat every
compiler, Kconfig, build, test, and runtime diagnostic as failure unless
already documented as non-actionable repo-wide diagnostic.

## Escalation and recap

Stop after two materially different failed approaches, SDK contradiction,
missing design, warning, test weakening, architecture invention, scope
expansion, or any hardware requirement. Preserve work and report exact blocker,
attempts, evidence, git status, and one precise question.

Return files changed, behavior, test commands/results, resolved config facts,
deviations, blockers, and explicit no-commit status.
