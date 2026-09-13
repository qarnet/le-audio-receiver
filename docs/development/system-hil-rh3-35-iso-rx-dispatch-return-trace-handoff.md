# RH3-35 ISO RX dispatch-return trace software handoff

Status: focused software-only HIL diagnostic. This phase distinguishes a
tracked ISO RX buffer still inside NCS `bt_conn_recv()` from one for which that
call returned without project `stream_recv()` starting. It does not run HIL,
flash hardware, change production behavior, choose a repair, or claim root
cause.

## Goal

Extend existing ISO RX lifetime disposition tracing with one monotonic stage:

```text
host_returned
```

At `disable` and `unavailable` snapshots, a live tracked slot must be counted
as exactly one of:

1. `undispatched`: diagnostic wrapper did not enter `bt_conn_recv()`.
2. `host_dispatched`: wrapper entered `bt_conn_recv()`, but snapshot observed
   neither a return from that call nor project `stream_recv()` start.
3. `host_returned`: wrapper returned from `bt_conn_recv()` without project
   `stream_recv()` starting.
4. `app_callback_seen`: project `stream_recv()` started.
5. `unclassified`: adjacent slot-stage observation raced allocation/final
   release.

`host_returned` is an observation about one wrapper call only. It does not
read `struct bt_conn`, inspect ISO reassembly fields, establish a queue
location or owner, prove a leak, or authorize production changes.

## Grounding

RH3-34 evidence is immutable:

```text
/tmp/opencode/hil-runs/rh3-20260824-34-sdc-hci-iso-rx-lifetime-disposition-trace/
```

It is valid schema-14 diagnostic evidence with no parser/validation error,
matching expected trace CPUAPP hash:

```text
2c5616a26ab425cf34f4c97c736f8a41270893f4c08db7065df5f655367f8361
```

The fresh Mode B `48_3_1` source reached `streaming`, receiver lifetime trace
armed at capacity three, and target SDC receive disposition again reported a
retained ISO RX buffer unavailable. Exact snapshots were:

```text
disable:
  outstanding=3 allocations=19485 final_unrefs=19482 callbacks_active=0
  undispatched=3 host_dispatched=0 app_callback_seen=0 unclassified=0

unavailable:
  outstanding=3 allocations=19485 final_unrefs=19482 callbacks_active=0
  undispatched=2 host_dispatched=1 app_callback_seen=0 unclassified=0
```

The one `host_dispatched` slot had entered the wrapper before the unavailable
snapshot, but RH3-34 cannot tell whether snapshot caught the call in progress
or whether `bt_conn_recv()` already returned without calling `stream_recv()`.
No first-final-unref marker appeared. This is evidence only, not a production
fault conclusion.

Installed NCS v3.3.0 behavior establishes exact value of the new stage:

```text
zephyr/subsys/bluetooth/host/iso.c:157
  hci_iso() calls bt_conn_recv(iso, buf, flags)

zephyr/subsys/bluetooth/host/conn.c:492-512
  bt_conn_recv() calls bt_iso_recv() for ISO then returns

zephyr/subsys/bluetooth/host/iso.c:653-795
  bt_iso_recv() may return before app recv for incomplete START/SINGLE data;
  app recv path invokes chan->ops->recv(), then bt_conn_reset_rx_state()
```

Therefore an observed `host_returned` live slot narrows the question to NCS
host behavior after `bt_conn_recv()` returned without app callback. It does not
prove the specific `bt_iso_recv()` branch because no private state is read.

## Scope

In scope:

- `src/sdc_hci_remove_iso_path_trace.c`
- `scripts/hil/receiver.py`
- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- `tests/hil/rh2_test.py`
- this handoff only for factual correction.

Out of scope:

- `Kconfig`, `CMakeLists.txt`, `src/bt_bap.c`, all HIL fragments, production
  audio/Bluetooth/source/controller/NCS code, pool count, queue priority,
  lifecycle, parser-runner rows/thresholds, build contract, `STATUS.md`,
  public docs, coverage baseline, and unrelated dirty work;
- hardware, HIL execution, flash, reset, recovery, serial, RF, pairing,
  `btattach`, `bap_central.py`, OpenOCD, or `serial-mcp`;
- commit, push, merge, PR, tag, reset, stash, clean, or broad formatting.

Repository `HEAD` is:

```text
c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1
```

Worktree is intentionally dirty. Do not disturb existing modifications or
immutable RH3 evidence. No hardware action is authorized in this phase.

## Exact implementation

### 1. Add one monotonic post-return stage

In `src/sdc_hci_remove_iso_path_trace.c`, under existing
`SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED` only,
change stage values to:

```text
UNCLASSIFIED = 0
UNDISPATCHED = 1
HOST_DISPATCHED = 2
HOST_RETURNED = 3
APP_CALLBACK_SEEN = 4
```

Keep fixed pointer slots, fixed atomic stage slots, and current pointer identity
matching. Do not add allocation, reference, private-field inspection, heap,
work, sleep, lock, spin wait, per-packet log, new wrapper, or Kconfig/CMake
link option.

In `__wrap_bt_conn_recv(struct bt_conn *conn, struct net_buf *buf,
uint8_t flags)` preserve current behavior exactly, with this order:

1. Before forwarding, monotonically advance a currently tracked equal pointer
   to `HOST_DISPATCHED`.
2. Call `__real_bt_conn_recv(conn, buf, flags)` exactly once with original
   arguments.
3. After that real call returns, monotonically advance a still-currently
   tracked equal pointer to `HOST_RETURNED`.

The existing stage advance must preserve a higher stage. Thus an
`APP_CALLBACK_SEEN` advancement made inside real `bt_conn_recv()` remains
`APP_CALLBACK_SEEN` after wrapper return. If final release cleared the slot
before return, post-return advancement does nothing. Do not dereference
`conn`, `buf`, or `flags` in either stage advancement.

### 2. Extend exact output grammar

Expand only disposition snapshot output to:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> host_returned=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Use one five-element bounded local count array or equivalent bounded local
values. Count each scanned live slot in exactly one stage. Preserve regular
lifetime snapshot text, order, counters, and all other trace text. The
disposition line still immediately follows existing regular snapshot output.

### 3. Parser schema 15 with schema-14 marker compatibility

In `scripts/hil/receiver.py`:

1. Raise `parse_sdc_hci_remove_iso_path_trace()` schema from `14` to `15`.
2. Parse new `host_returned` field as an integer for new grammar.
3. Continue accepting RH3-34 schema-14 disposition marker grammar that omits
   `host_returned`. Retain `host_returned: None` in those parsed records.
4. Preserve raw line, offsets, classification, reason, and all existing fields.
5. A malformed supplied `host_returned` field is a parser error. Do not treat
   `host_returned=bad` as a legacy omission.
6. With lifetime arm present, validate each present numeric category is at most
   capacity. Include `host_returned` in aggregate sum only when present. New
   grammar uses all five categories; legacy RH3-34 grammar uses original four.
   In either grammar, sum must not exceed capacity.
7. Preserve all current lifetime-arm, reason uniqueness, matching regular
   snapshot, ordering, and legacy-no-disposition behavior.

Do not require a new `host_returned` field for old evidence or alter old raw
records. Existing no-disposition traces must still return an empty disposition
array.

### 4. Native behavioral proof

Modify existing native test support in
`tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`.
Do not add a new native test method or alter test count.

Add one test-only control for `__real_bt_conn_recv` to invoke existing
diagnostic `callback_enter(buf)` and `callback_exit()` before the real stub
returns. It must retain and expose original wrapper argument forwarding exactly
as existing test does.

Modify existing
`test_iso_rx_lifetime_disposition_tracks_progression` to prove all new public
outcomes in one snapshot:

1. Allocate three tracked ISO RX buffers.
2. Leave first buffer undispatched.
3. Dispatch second through `__wrap_bt_conn_recv` with test callback control
   disabled, so wrapper return makes it `host_returned`.
4. Dispatch third through `__wrap_bt_conn_recv` with test callback control
   enabled, so app callback advancement occurs before real call returns and
   stays `app_callback_seen` after return.
5. Take disable snapshot and assert exact counts:

```text
undispatched=1
host_dispatched=0
host_returned=1
app_callback_seen=1
unclassified=0
```

Also preserve assertions for real wrapper forwarding, zero active callbacks,
zero final unrefs, and no tracking error. Reset test control in existing
observation reset path.

### 5. Parser behavioral proof

Extend existing RH2 parser test methods only. Keep RH2 method count `220`.

Update default valid lifetime disposition fixture to new five-field grammar and
prove records retain `host_returned` values and offsets. Within an existing
test, derive a schema-14 RH3-34-style variant by removing only
` host_returned=<decimal>` from valid lines; prove it has no parser/validation
error and records expose `host_returned is None`.

Extend existing malformed/duplicate/bounds test to prove:

- malformed supplied `host_returned` is rejected;
- new-field per-category over-capacity is rejected;
- aggregate count above capacity is rejected when every individual field is at
  most capacity;
- old four-field aggregate semantics remain accepted when within capacity.

Update every current direct-SDC parser schema assertion from `14` to `15`.
Do not add test methods or weaken any existing assertion.

## Verification

Run sequentially from repository root. No hardware action is authorized.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-dispatch-return-rh3-35-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected results: native SDC trace suite `15/15`; RH2 `220 passed`; matrix
`0 errors, 0 notes`. If totals differ, report exact totals and stop. Do not
weaken tests to force a count.

Build one trace image only, without hardware:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf"

trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK is not set' \
  '# CONFIG_NET_BUF_LOG is not set' \
  '# CONFIG_TRACING is not set' \
  '# CONFIG_LOG_RUNTIME_FILTERING is not set'; do
  rg -Fx "$expected" "$trace_config"
done

toolchain_nm=$(rg '^CMAKE_NM:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
toolchain_objdump=$(rg '^CMAKE_OBJDUMP:FILEPATH=' build/nrf54l15/le-audio-receiver/CMakeCache.txt | cut -d= -f2-)
trace_elf=build/nrf54l15/le-audio-receiver/zephyr/zephyr.elf
"$toolchain_nm" -A "$trace_elf" | \
  rg '__wrap_bt_conn_recv|bt_conn_recv|__wrap_net_buf_unref|net_buf_unref|hci_iso'
"$toolchain_objdump" -d --disassemble=hci_iso "$trace_elf" | \
  rg '__wrap_bt_conn_recv'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | \
  rg '__wrap_net_buf_unref'
```

Trace build must have no actionable warning or Kconfig assignment diagnostic.
Do not run production build-contract checks against temporary trace build.
Record receiver CPUAPP and FLPR SHA256 values. Do not use trace image for
hardware in this phase.

Then restore normal local receiver build without hardware:

```bash
nix develop --command fw-build-54l15

normal_config=build/nrf54l15/le-audio-receiver/zephyr/.config
rg -Fx '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' "$normal_config"
rg -Fx 'CONFIG_BT_ISO_RX_BUF_COUNT=3' "$normal_config"
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

## Return report

Return changed paths; exact test/build results; stage, parser schema/grammar,
and compatibility behavior; trace config/link/disassembly proof; trace image
hashes; normal restoration proof; diagnostics; final `git status --short`; and
explicit no-hardware/no-commit status. Stop and escalate if wrapper safety,
parser compatibility, warning, or test requirement requires design invention
or production behavior change.
