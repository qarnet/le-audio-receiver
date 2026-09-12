# RH3-34 ISO RX disposition trace software handoff

Status: focused software-only HIL diagnostic. This phase distinguishes where
the three outstanding host ISO RX buffers had progressed by disable time. It
does not run HIL, flash hardware, change production behavior, choose a repair,
or claim root cause.

## Goal

Extend existing ISO RX lifetime trace with bounded per-buffer progression
categories at `disable` and `unavailable` snapshots:

1. tracked allocation has not entered host `bt_conn_recv()`;
2. tracked allocation entered `bt_conn_recv()` but app `stream_recv()` has not
   started for it;
3. app `stream_recv()` started for tracked allocation;
4. transient slot-state race observed while taking a diagnostic snapshot.

This separates queued/pre-connection-dispatch candidates from host ISO
reassembly or post-callback candidates without retaining buffers, changing
ownership, delaying work, or reading private `struct bt_conn` fields.

## Grounding and boundary

Immutable RH3-33 evidence:

```text
/tmp/opencode/hil-runs/rh3-20260824-33-sdc-hci-iso-rx-lifetime-trace/
```

reached a valid fresh Mode B `48_3_1` stream with exact trace CPUAPP
`1ca02b604cb9baee0837aee72cb3aecb2f6f039120acea83444beafbc6864bd0`.
The parser passed schema 13 with no errors. At disable and at later target
allocation, it recorded exactly:

```text
capacity=3 outstanding=3 high_water=3 allocations=19488
final_unrefs=19485 callbacks_active=0 callbacks_total=19485
```

The subsequent SDC receive-disposition marker reported
`kind=rx type=32 buffer_available=0 target_busy=0x1`; no first final-unref
marker arrived before `0x206f` command timeout/fatal.

This proves full tracked ISO RX occupancy and no active project callback at
those samples. It does not prove queue position, per-buffer owner, leak,
callback-to-unref pairing, or a production bug.

Installed NCS v3.3.0 path:

```text
nrf/subsys/bluetooth/controller/hci_driver.c:522-547
  bt_buf_get_rx(BT_BUF_ISO_IN) -> recv_func
zephyr/subsys/bluetooth/host/hci_core.c:4554-4557,4635-4662
  bt_hci_recv queues ISO -> hci_iso
zephyr/subsys/bluetooth/host/iso.c:148-158
  hci_iso -> bt_conn_recv(iso, buf, flags)
zephyr/subsys/bluetooth/host/iso.c:718-795
  reassembly -> ASCS/app callback -> bt_conn_reset_rx_state
zephyr/subsys/bluetooth/host/conn.c:391-399
  final net_buf_unref(conn->rx)
```

`bt_conn_recv(struct bt_conn *, struct net_buf *, uint8_t)` is an internal,
non-static host symbol declared in
`zephyr/subsys/bluetooth/host/conn_internal.h:409-414`. Current nRF54L15 ISO
object has an undefined `R_ARM_THM_CALL bt_conn_recv` relocation from
`hci_iso`; GNU ld `--wrap=bt_conn_recv` can intercept it. Existing direct
ACL caller is harmless because the diagnostic only acts on pointer identities
already tracked from `BT_BUF_ISO_IN`; no tracked pointer is retained after its
final unref.

Do not inspect private `struct bt_conn` state, ISO connection fields, raw
HCI buffer type, or `buf->ref` in new progression code. Do not infer that
`undispatched` equals a specific queue location. It means only that this
wrapper did not observe `bt_conn_recv()` for that tracked pointer.

## Scope

In scope:

- `Kconfig`
- `CMakeLists.txt`
- `src/sdc_hci_remove_iso_path_trace.c`
- `src/bt_bap.c`
- `scripts/hil/receiver.py`
- `tests/hil/rh2_test.py`
- `tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf` (new)
- `tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt`
- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- this handoff only for factual correction.

Out of scope:

- all production audio, Bluetooth, source-fixture, controller, NCS,
  pool-count, queue-priority, teardown, parser-runner row/threshold, and build
  contract behavior;
- existing `tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf`,
  RH3-30 through RH3-33 evidence, `STATUS.md`, public documentation, coverage
  baseline, and unrelated dirty work;
- hardware, HIL execution, flash, reset, recovery, serial, RF, pairing,
  `btattach`, `bap_central.py`, or `serial-mcp`;
- commit, push, merge, PR, tag, reset, stash, clean, or broad formatting.

## Exact implementation

### 1. Add additive HIL config and new fragment

After existing `HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME`, add:

```text
config HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION
    bool "ISO RX lifetime disposition"
    default n
    depends on HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME
```

Help must state:

- temporary nRF54L15 HIL-only diagnostic;
- records bounded host-dispatch/app-callback progression for tracked ISO RX
  buffers at lifetime snapshots;
- wraps internal `bt_conn_recv` only for this exact NCS build;
- does not establish buffer ownership or queue location;
- normal builds keep it disabled.

Create only this new fragment:

```text
tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime-disposition.conf
```

with exactly:

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_LOG_RUNTIME_FILTERING=n
```

Do not set `CONFIG_BT_ISO_RX_BUF_COUNT`; it remains normal value three. Do
not modify RH3-32 fragment, select snapshot/scheduler probes, enable generic
tracing, or enable `NET_BUF_LOG`.

### 2. Add one conditional linker wrapper

When and only when new disposition config is enabled, add exactly:

```text
-Wl,--wrap=bt_conn_recv
```

to existing SDC trace link options. Preserve all existing wrapper conditions.
Do not wrap `hci_iso`, `bt_hci_recv`, `bt_conn_reset_rx_state`, `net_buf_ref`,
or any object file.

### 3. Add bounded monotonic per-slot progression

Keep implementation in `src/sdc_hci_remove_iso_path_trace.c`, gated by new
config or new native-only definition:

```text
SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION
```

Use existing fixed pointer-slot array. Add one fixed atomic stage array with
same capacity. Define four diagnostic stages:

```text
UNCLASSIFIED = 0
UNDISPATCHED = 1
HOST_DISPATCHED = 2
APP_CALLBACK_SEEN = 3
```

No heap, no work queue, no sleep, no spin wait, no per-packet log, no buffer
reference, and no production behavior change.

Required behavior:

1. `session_open()` and test reset clear every stage to `UNCLASSIFIED` with
   existing pointer/counter reset.
2. Successful tracked `BT_BUF_ISO_IN` allocation initializes its slot to
   `UNDISPATCHED`. A snapshot racing allocation/final release may observe
   `UNCLASSIFIED`; retain that as a diagnostic category, not an error.
3. Add `__wrap_bt_conn_recv(struct bt_conn *conn, struct net_buf *buf,
   uint8_t flags)`. Before forwarding, find only a currently tracked pointer
   equal to `buf` and monotonically advance its stage to `HOST_DISPATCHED`.
   Do not dereference or inspect `conn`, `buf`, or `flags`; then call real
   function exactly once with exact original arguments.
4. Change diagnostic `callback_enter` declaration and call to accept the
   existing `stream_recv()` `buf` argument. When armed, monotonically advance
   matching tracked slot to `APP_CALLBACK_SEEN`, then preserve existing total
   and active callback accounting. `callback_exit` stays no-argument and
   preserves existing decrement behavior.
5. Final tracked unref clears its matching slot stage only after existing
   final-release pointer claim. Preserve current final-unref logic and one real
   unref forwarding exactly.
6. Emit no progression log on allocation, dispatch, callback, or final unref.
   Immediately after each existing lifetime snapshot, emit one bounded line:

```text
SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=<disable|unavailable> undispatched=<decimal> host_dispatched=<decimal> app_callback_seen=<decimal> unclassified=<decimal>
```

Each category is a count of live slots observed in its current monotonic stage.
Categories are mutually exclusive for a scanned slot. The scan is not a
lock-held transaction: all values are adjacent atomic observations and do not
have to equal lifetime `outstanding` exactly. Each field and their sum must
remain bounded by configured capacity.

### 4. Parser schema 14

In `scripts/hil/receiver.py`:

1. Raise `parse_sdc_hci_remove_iso_path_trace()` schema from `13` to `14`.
2. Add optional array:

```text
iso_rx_lifetime_disposition_snapshots
```

3. Parse exact grammar above, retaining raw line, offsets, classification,
   reason, and four integer fields.
4. Treat a malformed marker as parser error. Existing no-disposition traces
   remain valid and return empty array.
5. Add disposition records to lifetime-arm presence validation. With a
   lifetime arm, validate all of:
   - record follows lifetime arm;
   - reason is `disable` or `unavailable`;
   - at most one record per reason;
   - every field is at most lifetime capacity;
   - sum of four fields is at most lifetime capacity;
   - matching regular lifetime snapshot with same reason exists and precedes
     disposition record.

Do not require counts to equal lifetime `outstanding`, impose an ordering
against direct-SDC arm, or require disposition records for legacy lifetime
traces.

### 5. Native and parser behavioral proof

In native CMake, add new disposition test compile definition. In native test
file, add weak observer/stub support only as needed for public wrapper outcomes:

- `__real_bt_conn_recv` records calls and exact argument values without
  dereferencing test `conn`;
- disposition-snapshot observer records reason and four counts.

Add one native behavioral test. It must:

1. Reset trace and observations, open lifetime session, and allocate three
   tracked ISO buffers through existing allocation wrapper.
2. Leave first buffer undispatched.
3. Call `__wrap_bt_conn_recv(NULL, second, 0U)`, asserting real wrapper
   forwarding once with exact arguments.
4. Call `__wrap_bt_conn_recv(NULL, third, 0U)`, then existing diagnostic
   `callback_enter(third)` and `callback_exit()`.
5. Call disable snapshot and assert one disposition record with exact
   `undispatched=1`, `host_dispatched=1`, `app_callback_seen=1`, and
   `unclassified=0`; assert callback active is zero; assert no final-unref or
   tracking error observation.

Do not inspect production slot arrays in tests. Existing tracked/untracked
unref, full-pool, and unarmed-forwarding tests remain unchanged in intent.

Extend existing RH2 lifetime fixture/tests, without adding separate test
methods, to prove:

- valid disable/unavailable disposition markers parse with offsets and schema
  `14`;
- malformed, duplicate, missing-parent, and over-capacity disposition records
  produce parser/validation errors;
- legacy traces return empty disposition array;
- every current direct-SDC parser schema expectation changes from `13` to `14`
  (twelve current assertions, including runner trace evidence assertion).

Keep RH2 test count `220`; extend existing lifetime test methods rather than
adding new Python test methods.

## Verification

Run sequentially from repository root. No hardware action is authorized.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-disposition-rh3-34-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Expected focused results: native SDC trace suite `15/15`; RH2 `220 passed`;
matrix `0 errors, 0 notes`. If totals differ, report exact totals and stop. Do
not weaken assertions to force a count.

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

The trace build must have no actionable warning or Kconfig assignment
diagnostic. Do not run production build-contract check against trace build.

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

Return changed paths; exact test/build results; parser schema/grammar and
validation additions; trace config/link/disassembly proof; trace image hashes;
normal restoration proof; diagnostics; final `git status --short`; and
explicit no-hardware/no-commit status. Stop and escalate if link wrapping,
pointer-stage safety, parser compatibility, or a warning requires design
invention or production behavior changes.
