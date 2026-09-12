# System HIL RH1B dedicated source-firmware handoff

Status: implementation handoff for RH1B, completing RH1 source firmware.
RH0 host contracts, RH1A source core, and bounded abort transition are accepted.
RH2 hardware orchestration remains out of scope.

## Goal

Build a repository-owned nRF5340DK BAP unicast-client source fixture that:

- boots with persistent Bluetooth identity/settings and bonds;
- accepts strict RH1A JSON through one quiet serial shell command;
- selects one exact configured receiver address, never first-seen device;
- supports mono, Mode A, and Mode B for exact `48_3_1` and `48_4_1` shapes;
- sends deterministic RH1A preamble, exact scored count, and >=5 s valid tail;
- remains responsive to `status`, `stop`, and `idle` while worker runs;
- supports one explicit disconnect/reconnect segment;
- owns bounded cleanup after success, error, timeout, and stop;
- emits lossless HIL1 records compatible with RH0 tracker;
- builds cleanly for `nrf5340dk/nrf5340/cpuapp` with SW Split ISO-central
  CPUNET under NCS v3.3.0.

RH1B proves firmware compilation and host-simulated orchestration. It does not
flash or execute hardware. RH2 performs first two-device run.

## Grounding

- Plan: `docs/development/system-hil-milestones.md`.
- Core: `hil/source/core/`.
- NCS reference: `zephyr/samples/bluetooth/bap_unicast_client/`.
- Existing real BAP client evidence:
  `tests/bsim/client/src/bsim_client_main.c` and `bsim_tx.c`.
- Public APIs verified in NCS v3.3.0:
  - `bt_enable`, `settings_load`, `bt_foreach_bond`, `bt_unpair`;
  - `bt_conn_le_create`, `bt_conn_set_security`;
  - `bt_bap_unicast_client_register_cb` and `discover`;
  - `bt_bap_stream_config`, `qos`, `enable`, `connect`, `disable`, `release`,
    `send`; `bt_bap_stream_cb_register`;
  - `bt_bap_unicast_group_create/delete`;
  - `SHELL_CMD_ARG_REGISTER(..., 1, SHELL_OPT_ARG_RAW)`;
  - `shell_backend_uart_get_ptr` + `shell_fprintf` from thread context.
- Exact CPUNET base:
  `zephyr/samples/bluetooth/hci_ipc/nrf5340_cpunet_iso_central-bt_ll_sw_split.conf`.
  It pins central-only connected ISO, two streams, six TX buffers, 255-byte
  buffer, and 247-byte max SDU. Largest Mode B SDU is 240 bytes.
- Settings on nRF53 sysbuild use Partition Manager `settings_storage`, default
  8 KiB. Use ZMS and call `settings_load()` after `bt_enable()`.
- `bt_addr_le_from_str(display, "public"|"random", &addr)` performs exact
  display-to-on-air conversion. RH1A parser currently stores display-order
  bytes; reconstruct canonical display string before this API or reverse once
  into `bt_addr_le_t`. Do not compare display-order bytes directly.
- Client sink-direction TX teardown: `bt_bap_stream_stop()` is invalid. Stop TX,
  then disable each streaming sink ASE, wait completion, release, wait
  `stream_ops.released`, disconnect ACL, then delete group after all streams are
  detached.

## Files

Add:

- `hil/source/app/CMakeLists.txt`
- `hil/source/app/Kconfig`
- `hil/source/app/Kconfig.sysbuild`
- `hil/source/app/prj.conf`
- `hil/source/app/sysbuild.cmake`
- `hil/source/app/boards/nrf5340dk_nrf5340_cpuapp.conf`
- `hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf`
- `hil/source/app/src/main.c`
- `hil/source/app/src/hil_source_app.h`
- `hil/source/app/src/hil_source_app.c`
- `hil/source/app/src/hil_source_output.h`
- `hil/source/app/src/hil_source_output.c`
- `hil/source/app/src/hil_source_tx.h`
- `hil/source/app/src/hil_source_tx.c`
- `hil/source/app/src/hil_source_bap.h`
- `hil/source/app/src/hil_source_bap.c`
- `scripts/bin/fw-build-hil-source`
- `tests/unit/hil_source_app/CMakeLists.txt`
- `tests/unit/hil_source_app/prj.conf`
- `tests/unit/hil_source_app/testcase.yaml`
- `tests/unit/hil_source_app/src/test_hil_source_app.c`
- `tests/unit/hil_source_app/src/fake_hil_source_backend.c`
- `tests/unit/hil_source_app/src/fake_hil_source_backend.h`

Update:

- `scripts/test-all.sh` prospective inventory comment only: 38 Twister + 5
  exec-only + 23 Python = 66 unit children, 69 total.
- `docs/development/system-hil-milestones.md` only if exact implemented command
  or counter field needs factual clarification.

Do not update accepted historical counts elsewhere. Preserve user-owned
`docs/development/firmware-release-fr4-summary-wait-fix-handoff.md`.

## Non-scope

- No receiver code, BSim code, RH0/RH1A contract changes, capture, ALSA, BlueZ,
  PipeWire, `btattach`, flashing, reset, serial access, sudo, or hardware.
- No flash/recovery script. Build helper only.
- No `48_5_1`, CAP/CAS/TMAS/CSIS, broadcast, extra codecs, or fault rows.
- No TX timestamps in RH1B. Use bounded depth-two completion/backpressure. RH2
  hardware evidence decides whether timestamp scheduling is needed.
- No static probe mapping or compiled target probe serial.

## Application composition

`CMakeLists.txt` compiles all four RH1A core `.c` files plus app/output/TX/BAP
modules. Include `hil/source/core` directly. Production build has no test seam.

`main.c` only initializes output, app coordinator, Bluetooth/BAP backend, then
returns into Zephyr threads. Fatal init error emits one best-effort HIL1 status
when possible and returns nonzero; no restart loop.

## Kconfig and sysbuild

App `prj.conf`:

```conf
CONFIG_BT=y
CONFIG_BT_CENTRAL=y
CONFIG_BT_ISO_CENTRAL=y
CONFIG_BT_GATT_CLIENT=y
CONFIG_BT_GATT_AUTO_DISCOVER_CCC=y
CONFIG_BT_GATT_AUTO_UPDATE_MTU=y
CONFIG_BT_AUDIO=y
CONFIG_BT_BAP_UNICAST_CLIENT=y
CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT=2
CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT=0
CONFIG_BT_BAP_UNICAST_CLIENT_GROUP_STREAM_COUNT=2
CONFIG_BT_ISO_MAX_CHAN=2
CONFIG_BT_ISO_TX_MTU=255
CONFIG_BT_ISO_TX_BUF_COUNT=6
CONFIG_BT_SMP=y
CONFIG_BT_SMP_APP_PAIRING_ACCEPT=y
CONFIG_BT_SMP_ENFORCE_MITM=n
CONFIG_BT_KEYS_OVERWRITE_OLDEST=y
CONFIG_BT_SETTINGS=y
CONFIG_BT_PRIVACY=y
CONFIG_SETTINGS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y
CONFIG_SETTINGS_ZMS=y
CONFIG_BT_EXT_ADV=y
CONFIG_FPU=y
CONFIG_LIBLC3=y
CONFIG_SPEED_OPTIMIZATIONS=y
CONFIG_ASSERT=y
CONFIG_UTF8=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_CMD_BUFF_SIZE=512
CONFIG_SHELL_ARGC_MAX=3
CONFIG_SHELL_ECHO_STATUS=n
CONFIG_SHELL_VT100_COMMANDS=n
CONFIG_SHELL_METAKEYS=n
CONFIG_SHELL_TAB=n
CONFIG_SHELL_HISTORY=n
CONFIG_SHELL_WILDCARD=n
CONFIG_SHELL_MSG_CMD_NOT_FOUND=n
CONFIG_SHELL_HELP_ON_WRONG_ARGUMENT_COUNT=n
CONFIG_SHELL_PROMPT_CHANGE=n
CONFIG_SHELL_PROMPT_UART=""
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_NONE=y
CONFIG_SHELL_TX_TIMEOUT_MS=0
CONFIG_SHELL_PRINTF_BUFF_SIZE=768
CONFIG_LOG=n
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
CONFIG_BT_RX_STACK_SIZE=4096
CONFIG_HEAP_MEM_POOL_SIZE=4096
```

If any symbol conflicts in exact NCS v3.3.0, inspect definition and make
smallest supported adjustment. No Kconfig warning is acceptable. Do not set
`CONFIG_FLASH_PAGE_LAYOUT` unless resolver does not enable it.

Board conf may contain nRF5340-specific FPU/stack values if Kconfig warns when
kept in common conf.

`Kconfig.sysbuild` selects netcore board
`nrf5340dk/nrf5340/cpunet`. `sysbuild.cmake` mirrors upstream client sample,
adds hci_ipc, applies central ISO base conf plus repository overlay, and applies
`bt-ll-sw-split` snippet. Overlay may only tighten:

```conf
CONFIG_BT_MAX_CONN=1
CONFIG_BT_ISO_MAX_CHAN=2
CONFIG_BT_ISO_TX_BUF_COUNT=6
CONFIG_BT_CTLR_CONN_ISO_GROUPS=1
CONFIG_BT_CTLR_CONN_ISO_STREAMS=2
CONFIG_BT_CTLR_CONN_ISO_STREAMS_PER_GROUP=2
CONFIG_BT_CTLR_ISOAL_SOURCES=2
CONFIG_BT_CTLR_ISO_TX_BUFFERS=6
CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE=255
CONFIG_BT_CTLR_ISO_TX_SDU_LEN_MAX=247
```

Do not duplicate whole upstream conf into repo.

## Output ownership

Every HIL1 line, command response and async event, uses one queue and one writer
thread. No direct `printk`, `printf`, `shell_print`, or `shell_fprintf` outside
`hil_source_output.c`.

- Queue stores eight complete lines of 768 bytes plus length.
- `hil_source_output_submit(line, len, timeout)` rejects non-HIL1 prefix,
  embedded CR/LF/NUL, overflow, or inactive output; never truncates.
- Worker/runtime paths use bounded timeout 1 s. Queue timeout is a fatal runtime
  error and triggers abort where a run exists.
- Writer gets `shell_backend_uart_get_ptr()` and emits
  `shell_fprintf(sh, SHELL_NORMAL, "%.*s\n", len, line)`.
- `CONFIG_SHELL_TX_TIMEOUT_MS=0` makes shell lock wait forever, so accepted
  queued lines are not silently dropped.
- Empty prompt, no echo, no colors, no serial logs. Bare CRLF from shell command
  execution is unprefixed and ignored by RH0 parser.
- Monotonic timestamp is `k_uptime_get_32()`.

Provide backend injection table under `CONFIG_HIL_SOURCE_APP_TEST` only. Native
tests capture exact submitted lines without shell.

## Boot and identity

Initialization order:

1. output queue/writer;
2. `bt_enable(NULL)`;
3. `settings_load()`;
4. require `bt_is_ready()`;
5. register connection auth/auth-info callbacks and BAP client callbacks;
6. initialize app state and shell command.

Do not call `bt_ctlr_set_public_addr()`. Host settings creates/loads identity;
`CONFIG_BT_PRIVACY` persists random-static fallback on zero-FICR board. HELLO
reads identity with `bt_id_get()` and reports its canonical display address/type.

Pairing policy: Just Works only. `pairing_accept` returns success. Security
callback requires level >= L2 and error none. Bond must exist after successful
security; status records resulting bond count.

## Shell command and dispatch

Register:

```c
SHELL_CMD_ARG_REGISTER(hil, NULL, NULL, cmd_hil, 1, SHELL_OPT_ARG_RAW);
```

Require `argc == 2`; parse `argv[1]` with RH1A parser. Invalid input cannot
supply trustworthy IDs, so emit one non-HIL1 shell-free failure is forbidden.
Instead use reserved safe IDs `parse-error` for command and `unbound` for run,
kind `status`, command `status`, `ok:false`, error `parse_error`. Also include
parse-result name in trusted data. This record is diagnostic and not fed to an
active run tracker.

Dispatch rules under app mutex:

- `hello`: always accepted; emits status/hello data with firmware ID, protocol,
  identity, profile constants, signal constants, `active`, current state, and
  bond count.
- `configure`: only inactive and no immutable terminal snapshot. Convert parsed
  address to exact `bt_addr_le_t`, call RH1A state configure, store command/run
  IDs for future start, emit status success/failure.
- `start`: requires configured IDs' run ID equal request run ID. Active start is
  busy. On acceptance, RH1A start, save this start command ID as async command,
  emit start ack, enqueue worker. Handler returns immediately.
- `status`: request run ID must equal configured/active/snapshot run ID. Emit
  immutable snapshot under status request command ID. It never mutates state.
- `stop`: run ID must match. If active, set idempotent stop request and wake all
  waits; emit status accepted. If already terminal with matching run, emit
  status success unchanged. If configured but inactive/no terminal, emit
  success no-op.
- `idle`: if active, request stop and wait bounded 15 s for terminal fail; then
  ensure backend disconnected/group absent/TX stopped. If inactive, clean any
  stray backend resources. On success reset RH1A state and configured IDs, but
  do not erase bonds/settings. Emit status success after cleanup.
- `unpair`: only inactive. Convert exact address and call
  `bt_unpair(BT_ID_DEFAULT, &peer)`. No-key is success. Enumerate bonds and emit
  exact resulting count. Never call `bt_unpair(..., NULL)`.

Unknown enum cannot occur after parser. Any backend/runtime error name is
stable, not raw log text.

## Status and HELLO data

Use generic RH1A envelope only with locally formatted trusted JSON. Add compact
formatter helpers in app module, not user-controlled JSON concatenation.

Status must include:

- command and `ok`/error;
- active, state, segment, aborted/cause, stop requested, verdict, first errno;
- mode/profile/reconnect policy and scored target when configured;
- stream count and, for each configured stream: sequence number, submitted,
  scored submitted, send failures, sent callbacks, outstanding;
- connection flag, security level/error, discovered sink ASE count, group flag;
- disconnect reason and first ASCS response code/reason;
- bond count.

HELLO additionally includes exact frame samples, octets, SDU sizes, preamble
samples, tail frame counts 667/500, carrier frequencies, seed default, and
source identity.

Formatter overflow is failure, never partial output.

## Worker lifecycle

One worker thread owns all blocking BAP operations and resource teardown. Shell
and callbacks never run lifecycle directly. Worker accepts at most one start.

Normal segment sequence and records:

1. state `idle` (segment 0 only, already entered by start);
2. state `configured` (segment 0 only);
3. state `connecting`;
4. direct `bt_conn_le_create` to exact configured peer, wait <=20 s;
5. set L2 security, wait <=20 s, state `secured`;
6. discover only `BT_AUDIO_DIR_SINK`, wait <=10 s, require enough endpoints,
   state `discovered`;
7. configure streams + group + QoS with each operation <=10 s, state `qos`;
8. enable/connect and wait all sink streams `started`, state `streaming`;
9. run TX preamble, scored, tail;
10. after exact scored successful submissions on every stream, state
    `scored_complete` before first tail SDU;
11. after tail submissions, ordinary state `teardown`, clean resources;
12. if reconnect policy once and segment 0 clean, call next segment, emit
    `connecting`, repeat steps 4-11 with fresh streams/group/encoders;
13. terminal pass after clean final teardown and cleanup.

At every bounded wait, check stop flag at least every 100 ms. Use helper waiting
on event semaphore with deadline, not one uninterruptible 20 s take.

Error/timeout/stop path:

- record first errno when applicable;
- set abort cause error/timeout/stop;
- stop TX immediately;
- emit exact abort teardown state once;
- run universal cleanup best effort;
- terminal fail only after cleanup attempt;
- cleanup error does not suppress prior error and still yields fail;
- no reconnect after abort.

## BAP ownership and callbacks

Static storage: two `bt_bap_stream`, two sink endpoint pointers, one unicast
group pointer. Register stream ops with public `bt_bap_stream_cb_register`, not
direct `.ops` assignment.

Callbacks only record result under backend lock and signal sem/event:

- conn connected/disconnected/security;
- GATT MTU update (diagnostic, not mandatory gate beyond bounded wait);
- client listener config/qos/enable/disable/release response codes/reasons;
- discovery endpoint/completion;
- stream configured/qos_set/enabled/connected/started/stopped/disconnected/
  released/sent.

Reject callback for wrong connection or unknown stream as first error. Ignore
late callback only after generation changed during cleanup; count it in status.

Mode presets:

- mono: standard 48_3_1/48_4_1 MONO;
- Mode A: FL and FR standard presets on two sink ASEs;
- Mode B: one FL|FR codec config with per-channel octets and QoS SDU doubled,
  exactly matching `tests/bsim/client/src/bsim_client_main.c`.

Create one CIS pair per TX stream, sequential packing. Do not discover/configure
source-direction ASEs.

## TX ownership and pacing

One CIG TX worker, not callback encoding. Fixed net_buf pool:

- six buffers, each `BT_ISO_SDU_BUF_SIZE(255)`;
- reserve `BT_ISO_CHAN_SEND_RESERVE`;
- outstanding target two per stream;
- independent uint16 sequence per stream, starts zero each segment;
- increment sequence and RH1A submitted counters only on successful send;
- on send failure caller unrefs buffer, increments send failure, records error,
  aborts run;
- `sent` callback decrements outstanding, increments sent callback, wakes TX;
  callback for zero outstanding is error;
- disconnect may not produce sent callbacks. TX stop clears logical outstanding
  only after generation changes; stale callbacks counted, not applied.

Lockstep:

- mono/Mode B: queue until outstanding reaches two;
- Mode A: only generate/send a pair when both streams are below two. Encode one
  frame per semantic stream, send L then R back-to-back. If second send fails,
  abort whole run. Never let one stream advance to next semantic frame alone.

Stage frame counts per stream:

| Profile | Preamble | Scored | Tail |
|---|---:|---:|---:|
| 48_3_1 | 192 | configured target | 667 |
| 48_4_1 | 144 | configured target | 500 |

Tail frame count guarantees >=5 s. Transition to scored only after every stream
has submitted full preamble. Transition to tail only after every stream has
submitted exact scored target. Stop filling after tail target; wait up to 5 s
for outstanding to reach zero before normal teardown. No extra SDU after cap.

TX progress timeout: if no successful send or sent callback for 2 s while
streaming and work remains, abort timeout.

## Cleanup

Universal cleanup is idempotent and generation-guarded:

1. stop TX generation and wake worker;
2. for each attached streaming/enabling sink stream, disable and wait bounded;
3. release every attached stream and wait `released`; release-on-IDLE may detach
   synchronously, so inspect `stream->ep` before waiting;
4. disconnect exact connection if present and wait bounded;
5. after every `stream->ep == NULL`, delete group;
6. unref app-owned connection exactly once;
7. clear endpoint pointers/semaphores/callback result storage for next segment.

Continue cleanup after individual failure. Return first cleanup errno. Never
delete bonds in cleanup.

## Native app tests

`hil_source_app.c` uses a backend ops table only under
`CONFIG_HIL_SOURCE_APP_TEST`. Native suite compiles coordinator/output formatter
with fake backend and RH1A core; it does not compile real Bluetooth backend/TX.

Cover at minimum:

1. boot init order and fatal-stop behavior;
2. hello exact constants and identity/bond fields;
3. configure/start/status success and ID correlation;
4. invalid parse reserved diagnostic IDs;
5. busy/mismatched run/missing config rejection;
6. start handler returns before worker completes;
7. full mono, Mode A, Mode B lifecycle for both profiles with exact ordered
   HIL1 records and counters;
8. reconnect has segment 1 starting at connecting with fresh sequence counters;
9. stop during connect/security/discovery/qos/streaming/tail yields abort cause
   stop, universal cleanup, terminal fail;
10. each timeout boundary yields timeout cause and cleanup;
11. backend/send/cleanup failures yield error cause, first-error preservation,
    terminal fail, and all cleanup operations attempted in order;
12. status during active run does not mutate and uses query command ID;
13. output queue validation/overflow and formatter truncation fail closed;
14. idle idempotence, exact-peer unpair only, and terminal reset behavior;
15. Mode A lockstep and no sends beyond exact stage caps;
16. late/stale callback generation ignored but counted.

Tests assert public lines, backend operation ledger, state/counters, and cleanup
outcomes, not private helper calls.

## Build helper

`fw-build-hil-source` follows `fw-common.sh`, requires shell, runs:

```bash
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine \
  -d build/hil-source hil/source/app -- "$@"
```

Result contract:

- `build/hil-source/merged.hex`;
- `build/hil-source/merged_CPUNET.hex`;
- app and hci_ipc `zephyr.elf`/`zephyr.hex`.

No flash helper in RH1B.

## Verification

Run:

```bash
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_app -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-app-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_control -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-control-rh1b-twister
env NIX_HARDENING_ENABLE="" west twister -T tests/unit/hil_source_signal -p native_sim/native/64 --inline-logs --outdir /tmp/hil-source-signal-rh1b-twister
nix develop --command fw-build-hil-source
nix develop --command python3 -W error scripts/test_hil_runner.py
python3 scripts/test_inventory.py --count
python3 scripts/test_inventory.py --json
python3 -m compileall -q scripts/hil scripts/test_hil_runner.py scripts/generate_hil_source_sine_lut.py
git diff --check
git status --short
```

Expected inventory 66. Inspect build logs and resolved configs. Report every
diagnostic. Do not run full canonical dirty-tree gate or hardware commands.

## Constraints

- NCS v3.3.0 only.
- Warnings fail; documented global non-actionable experimental/deprecation
  diagnostics must be identified, not called clean warnings.
- No dynamic allocation in RH1A core; Zephyr/Bluetooth internal allocation and
  fixed net_buf pool are allowed.
- No output loss, silent truncation, swallowed cleanup error, blanket erase, or
  broad unpair.
- Do not commit, push, merge, amend, open PR, or edit git config.

## Escalation and recap

Stop after two materially different failed attempts, contradictory NCS
behavior, unexplained warning, or need to invent architecture. Return blocker,
attempts, exact logs/evidence, status, one question, and smallest hypothesis.

On success return files, public behavior, exact native/build results, resolved
image/config facts, warnings, deviations, blockers, and RH2 follow-up.
