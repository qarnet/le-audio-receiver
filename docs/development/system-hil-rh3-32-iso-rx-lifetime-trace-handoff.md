# RH3-32 ISO RX lifetime trace software handoff

Status: focused software-only diagnostic. This phase adds bounded observability
for host ISO RX allocation and final-release lifetime. It does not run HIL,
flash hardware, change production behavior, or claim a root cause or repair.

## Goal

Explain why RH3-30 and RH3-31 both reached a retained
`BT_BUF_ISO_IN` allocation failure before the pending `0x206f` completion.
The next trace must distinguish a normal queued backlog from a missing final
release while preserving normal builds byte-for-byte.

## Evidence and decision

Both immutable physical runs reached the same valid post-arm state:

| Run | Host ISO RX pool | Scoped allocation | Outcome |
| --- | ---: | --- | --- |
| RH3-30 | 3 | `type=32 buffer_available=0` | `retained_iso_buffer_unavailable` |
| RH3-31 | 6 | `type=32 buffer_available=0` | `retained_iso_buffer_unavailable` |

RH3-31 used the reviewed six-entry pool and still failed at `session end` with
`missing receiver stream summary slot(s): [0]`, then timed out on `0x206f` and
halted. It disproves six buffers as sufficient headroom for this transaction.
It does not prove why all entries were held.

Installed NCS v3.3.0 establishes the relevant lifetime:

- `nrf/subsys/bluetooth/controller/hci_driver.c:522-548` allocates
  `BT_BUF_ISO_IN`, delivers it to `bt_hci_recv()`, and returns `-ENOBUFS` on
  allocation failure.
- `hci_driver.c:690-713` retains that ISO packet and stops fetching later HCI
  messages until a host ISO buffer is freed.
- `zephyr/subsys/bluetooth/host/hci_core.c:4500-4576,4635-4688` queues HCI ISO
  buffers to the dedicated BT RX work queue.
- `zephyr/subsys/bluetooth/host/iso.c:718-795` calls app `stream_recv()`
  synchronously, then `bt_conn_reset_rx_state()` releases the buffer.
- `zephyr/subsys/bluetooth/host/conn.c:391-399` calls `net_buf_unref()` for
  that final release. Current nRF54L15 ELF disassembly proves this call is an
  external reference and can be wrapped.
- `src/bt_bap.c:964-1054` calls `audio_stream_session_recv()` synchronously
  from `stream_recv()`; `src/audio_stream_session.c:485-741` calls
  `audio_sink_push()` synchronously. Current nRF54L15 configuration permits a
  bounded 20 ms post-start slab wait there. This is a hypothesis only, not a
  conclusion from RH3-31.

Chosen diagnostic: track every post-stream-start successful ISO RX allocation,
only the matching final `net_buf_unref()` release, high-water occupancy, and
whether an app ISO callback is active at disable and target allocation failure.
No normal allocation or release emits a log. At most four diagnostic logs are
expected per session: arm, disable snapshot, unavailable snapshot, and first
final free after unavailable.

## Scope

In scope:

- `Kconfig`
- `CMakeLists.txt`
- `src/sdc_hci_remove_iso_path_trace.c`
- `src/bt_bap.c`
- `scripts/hil/receiver.py`
- `tests/hil/rh2_test.py`
- `tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf` (new)
- `tests/unit/sdc_hci_remove_iso_path_trace/CMakeLists.txt`
- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- this handoff only for factual correction.

Out of scope:

- all production audio, I2S, ASRC, FLPR, Bluetooth, pool-count, queue-priority,
  controller, host-driver, source-fixture, runner, row, threshold, or build
  contract behavior;
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`, `prj.conf`, `STATUS.md`, public
  documentation, coverage baseline, and unrelated dirty files;
- hardware, HIL execution, flashing, reset, recovery, erase, serial, RF,
  pairing, `btattach`, `bap_central.py`, or `serial-mcp`;
- commit, push, merge, PR, tag, reset, stash, clean, or broad formatting.

## Exact implementation

### 1. Add additive HIL Kconfig and trace fragment

Keep existing mutually exclusive
`HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_PROBE` choice unchanged. Add this separate
additive option after that choice:

```text
config HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME
    bool "ISO RX lifetime"
    default n
    depends on HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION
    depends on !NET_BUF_LOG
```

Its help must state all of these facts:

- temporary nRF54L15 HIL-only diagnostic;
- tracks host ISO RX allocation/final-release occupancy;
- requires receive-disposition trace;
- relies on ordinary `net_buf_unref` linkage, so `NET_BUF_LOG` must remain off;
- normal builds keep it disabled.

Create this exact fragment:

```text
tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf
```

```text
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y
CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y
CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL_INF=y
CONFIG_LOG_RUNTIME_FILTERING=n
```

Do not set `CONFIG_BT_ISO_RX_BUF_COUNT` in this fragment. RH3-32 must retain
the normal production value three. Do not select snapshot or scheduler probes,
enable generic tracing, or enable `NET_BUF_LOG`.

### 2. Add one final-unref linker wrapper

When and only when
`CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y`, add:

```text
-Wl,--wrap=net_buf_unref
```

to the existing SDC trace target link options. Preserve every existing wrapper
and conditional. Do not wrap `net_buf_unref_debug`, `net_buf_destroy`,
`hci_driver_receive_process`, or an entire host object.

### 3. Implement bounded ISO RX lifetime tracking

Keep all implementation inside `src/sdc_hci_remove_iso_path_trace.c`, gated by
the new config or a native-test-only compile definition
`SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME`.

Use a fixed `atomic_ptr_t` slot array. Production capacity is exactly
`CONFIG_BT_ISO_RX_BUF_COUNT`; native test capacity is exactly three. Do not
allocate from heap, use a new work queue, schedule work, sleep, spin, or log
per packet.

Add these diagnostic-only functions, declared in `src/bt_bap.c` only under the
new config:

```c
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open(void);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_enter(void);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_exit(void);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot(void);
```

Their behavior is fixed:

1. `session_open()` clears every tracked pointer and every counter, arms once
   for current session, and emits exactly:

   ```text
   SDC LE Remove ISO Data Path ISO RX lifetime trace armed: capacity=<decimal>
   ```

2. `callback_enter()` increments total callbacks and active callbacks only
   while armed. `callback_exit()` decrements active callbacks only while armed.
   No callback marker is logged.

3. In `__wrap_bt_buf_get_rx()`, always call the real allocator first. For a
   successful `BT_BUF_ISO_IN` allocation while armed, record pointer in one
   empty atomic slot, increment allocation and outstanding counts, and update
   high-water count. This is independent of the existing one-transaction
   receive-disposition scope. A duplicate tracked pointer or no free tracking
   slot emits one diagnostic error and disables tracking; it must never change
   real allocation result or ownership.

4. In `__wrap_bt_buf_get_rx()`, when the real result is `NULL` for
   `BT_BUF_ISO_IN` in the existing target MPSL receive-disposition context,
   emit one snapshot with `reason=unavailable` before the existing allocation
   marker. It must not change current receive-disposition clearing or outcome.

5. `__wrap_net_buf_unref(struct net_buf *buf)` checks tracked pointers only
   before forwarding. A tracked head with pre-call `buf->ref == 1U` is a final
   release: atomically remove its slot, decrement outstanding, increment final
   unrefs, then call real unref exactly once. Never dereference it after real
   unref. A nonfinal ref or untracked pointer only forwards. This is a scoped
   diagnostic observation, not a general atomic refcount proof.

6. The first tracked final release after an unavailable snapshot emits exactly:

   ```text
   SDC LE Remove ISO Data Path ISO RX lifetime first free after unavailable: outstanding_before=<decimal> callbacks_active=<decimal> allocations=<decimal> final_unrefs=<decimal>
   ```

   Later releases emit no lifetime log.

7. `disable_snapshot()` emits one snapshot before command trace arm. Snapshot
   grammar is exactly:

   ```text
   SDC LE Remove ISO Data Path ISO RX lifetime snapshot: reason=<stream_start|disable|unavailable> capacity=<decimal> outstanding=<decimal> high_water=<decimal> allocations=<decimal> final_unrefs=<decimal> callbacks_active=<decimal> callbacks_total=<decimal>
   ```

   `session_open()` emits `reason=stream_start` only through the arm marker,
   not a duplicate snapshot. `disable_snapshot()` uses `reason=disable`.

Use atomic counters and atomic pointer exchange/CAS only. Snapshot fields may
represent adjacent concurrent operations; do not present them as a lock-held
transaction. `outstanding` must never be deliberately driven below zero or
above configured capacity.

### 4. Hook exact app lifecycle boundaries

In `src/bt_bap.c`:

1. In `stream_started()`, only on the existing successful `gate_opened` path,
   call `session_open()` after `audio_stream_session_start_clear()` and before
   `audio_stream_session_rx_open()`. This arms before first data admission.
2. In `stream_recv()`, call `callback_enter()` after `t0` is captured and call
   `callback_exit()` before both existing return paths, including gate-closed
   return. Preserve all existing timing, gate, callback-status, decode, push,
   and error behavior.
3. In `lc3_disable()`, call `disable_snapshot()` before current direct-SDC
   trace arm and before teardown transition. Preserve callback return value and
   existing trace-arm semantics.

### 5. Extend parser and native proof

Raise `parse_sdc_hci_remove_iso_path_trace()` schema from `12` to `13`.
Add structured arrays:

```text
iso_rx_lifetime_arm_markers
iso_rx_lifetime_snapshots
iso_rx_lifetime_first_free_after_unavailable
```

Every parsed record retains raw line, offsets, and existing before/after-arm
classification. Parse the exact grammars above. Keep all lifetime data optional
for existing trace configurations.

When a lifetime arm marker exists, validate:

- exactly one arm marker and capacity is positive;
- every snapshot has one known reason, positive matching capacity, bounded
  `outstanding` and `high_water`, and occurs after lifetime arm;
- at most one `disable`, one `unavailable`, and one first-free marker;
- first-free exists only after unavailable and has nonzero
  `outstanding_before` no greater than capacity;
- malformed or duplicate lifetime records are parser/validation errors.

Do not require a lifetime marker for legacy or receive-disposition-only traces.
Do not impose equality between concurrently sampled counters.

Extend native CMake with the exact lifetime test compile definition. Add two
behavioral tests:

1. successful ISO allocation, callback enter/exit, and pre-call final unref
   update tracked outstanding/high-water/allocation/final-release state;
   nonfinal and untracked unrefs forward without changing tracked occupancy;
2. full three-slot test fixture produces `reason=unavailable` snapshot at
   capacity, then exactly one first-free-after-unavailable observation when a
   tracked final unref occurs.

Use weak test-only observers or equivalent direct diagnostic hooks. Do not
assert private production memory layout. Both tests must prove public wrapper
forwarding plus bounded trace outcome.

Add RH2 parser tests for valid lifetime markers, malformed/duplicate markers,
and legacy no-lifetime compatibility. Preserve all prior schema behavior.

## Verification

Run sequentially from repository root. No hardware action is authorized.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-lifetime-rh3-32-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run

direnv exec . python3 -m pytest -q tests/hil/rh2_test.py
python3 -m py_compile scripts/hil/cli.py scripts/hil/runner.py scripts/hil/receiver.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check

nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-sdc-remove-iso-path-iso-rx-lifetime.conf"
```

Expected focused test results: native SDC trace suite `13/13`, RH2 suite
`220 passed`, matrix checker `0 errors, 0 notes`. If the current test totals
differ for unrelated pre-existing work, report exact totals and stop rather
than changing assertions to force these numbers.

For the trace build, prove exact resolution:

```bash
trace_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION=y' \
  'CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME=y' \
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
"$toolchain_nm" -A "$trace_elf" | rg '__wrap_net_buf_unref|net_buf_unref|bt_conn_reset_rx_state'
"$toolchain_objdump" -d --disassemble=bt_conn_reset_rx_state "$trace_elf" | rg '__wrap_net_buf_unref'
```

The trace build must have no unapproved warning or Kconfig assignment
diagnostic. Only project-documented deprecation, SW Split experimental,
upstream ISO choice, and nRF54L15 watchdog empty-library diagnostics remain
non-actionable.

Then restore normal local build, without flashing:

Current normal build identity is reproducible and this hash is current for RH3-32 worktree;
`07fdbecd4d3e0eb01891fc31e6b2f5e3f29b703e1171fe40d839911dc9913dd0`
is historical RH3 evidence and is not hardware or artifact acceptance.

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

Return changed paths; exact test/build results; trace config and link proof;
normal restore hashes; diagnostic log grammar; parser schema/test additions;
and final `git status --short`. Do not run HIL, flash, or commit.
