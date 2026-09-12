# RH3 nRF54L15 ISO link-quality diagnostic handoff

Status: host, firmware, and HIL-evidence implementation phase. Add one
read-only nRF54L15 controller-telemetry query to the existing runner-owned
active-stream tail. Do not run physical HIL in this phase.

## Goal

Expose the Bluetooth LE `Read ISO Link Quality` HCI counters for every active
sink CIS through a parseable `bt iso quality` shell command. Have the HIL
runner collect and retain that output while the source remains in its normal
scored-tail window.

This classifies controller-reported `crc_error_packets` and
`rx_unreceived_packets` beside existing app callback-status evidence. It does
not identify a radio root cause, relax a fault rule, or make nonzero counters
pass/fail acceptance criteria.

## Grounding

Fresh direct row `rh3-20260820-04-iso-status` passed with exact images and
clean receiver tail, but its application summaries showed:

```text
slot 0: rx_valid=119, rx_error=0, rx_lost=14300, rx_unknown=0, rx_no_ts=50
slot 1: rx_valid=0,   rx_error=0, rx_lost=14390, rx_unknown=0, rx_no_ts=14390
```

Source completed both streams with `sub=12644`, `cb=12644`, and `sc=12000`.
`BT_ISO_FLAGS_LOST` proves Zephyr host received HCI `BT_ISO_DATA_NOP` and
forwarded it through ASCS/BAP to `stream_recv()`. It cannot explain missing
HCI packets or distinguish controller unreceived versus CRC counters.

Installed NCS v3.3.0 sources establish this public telemetry route:

```text
bt_bap_ep_get_info(stream->ep, &ep_info)
  -> ep_info.iso_chan->iso
  -> bt_hci_get_conn_handle(iso_conn, &handle)
  -> bt_hci_cmd_send_sync(BT_HCI_OP_LE_READ_ISO_LINK_QUALITY, ...)
```

Use only public data/API:

- `zephyr/include/zephyr/bluetooth/audio/bap.h:845-883`:
  `bt_bap_ep_info`, `iso_chan`, `can_recv`, `bt_bap_ep_get_info()`.
- `zephyr/include/zephyr/bluetooth/iso.h:206-231`:
  public `bt_iso_chan.iso` pointer.
- `zephyr/include/zephyr/bluetooth/hci.h:68-125,127-134`:
  command allocation/send and public connection-handle lookup.
- `zephyr/include/zephyr/bluetooth/hci_types.h:2543-2558`:
  opcode `0x2075`, CP/RP packed layouts.
- `nrf/subsys/bluetooth/controller/hci_internal.c:608-614,1415-1422`:
  nRF54L15 SDC exposes and forwards this command when ISO is enabled.

Do not access `bt_conn` internals such as `chan->iso->handle`. Do not enable
SW Split `CONFIG_BT_CTLR_READ_ISO_LINK_QUALITY`: nRF5340 SW Split does not
ship this command enabled in NCS v3.3.0. Do not enable monitor UART, directly
access RADIO, or change controller/RF settings.

## Scope

In scope:

1. nRF54L15-only public HCI command wrapper owned by `bt_bap.c`.
2. `bt iso quality` shell command with stable, parseable output.
3. HIL receiver parser, fake transcript, runner-tail collection, and
   structured evidence retention.
4. Direct shell and fake-HIL regression coverage.
5. Both production builds and resolved build-contract validation.

Out of scope:

- physical HIL, flashing, serial, reset, pairing, manual HCI/RF action, raw
  HCI monitor, source build, or new run ID;
- periodic telemetry, callback-path HCI commands, controller changes,
  thresholds, warning exemptions, acceptance claims, PLC/Mode A/timing/I2S
  changes, or any direct RADIO access;
- nRF5340 telemetry support, Kconfig controller gates, or SW Split changes;
- changes to existing image/evidence identities, `STATUS.md`, unrelated dirty
  worktree files, commit, push, merge, PR, tag, or release.

## Exact application API

### `src/bt_bap.h`

Add `<stddef.h>` and `<stdint.h>` as needed. Define this public snapshot:

```c
struct bt_bap_iso_link_quality {
	size_t slot;
	uint16_t handle;
	uint32_t tx_unacked_packets;
	uint32_t tx_flushed_packets;
	uint32_t tx_last_subevent_packets;
	uint32_t retransmitted_packets;
	uint32_t crc_error_packets;
	uint32_t rx_unreceived_packets;
	uint32_t duplicate_packets;
};

int bt_bap_iso_link_quality_get_active(struct bt_bap_iso_link_quality *snapshots,
				       size_t capacity, size_t *count);
```

Contract:

- thread context only; HCI command is synchronous and must never run from ISO
  receive callback, ISR, or a lifecycle lock hold;
- caller supplies non-NULL `snapshots`, nonzero `capacity`, and non-NULL
  `count`, else `-EINVAL`;
- initializes `*count` to zero before work;
- nRF54L15 returns snapshots only for active sink CISes, ordered by stable
  application sink slot. No active CIS returns `-ENOTCONN`;
- nRF5340 returns `-ENOTSUP` without issuing an HCI command;
- invalid/ended CISes that report `-ENOTCONN` during active-slot enumeration
  are skipped. Any other endpoint, handle lookup, HCI, malformed-response, or
  capacity error aborts with exact errno and no success claim;
- output contains no cached handles. Reacquire endpoint information and HCI
  handle for each query.

### `src/bt_bap.c`

Implement the API using existing static `sinks[MAX_SINK_ASE]` only.

For nRF54L15:

1. For every static sink with an endpoint, call
   `bt_bap_ep_get_info(sinks[i].ep, &ep_info)`.
2. Require only `ep_info.dir == BT_AUDIO_DIR_SINK`, non-NULL
   `ep_info.iso_chan`, and non-NULL `ep_info.iso_chan->iso`. Do not use
   `ep_info.can_recv`: installed NCS sets it only under `CONFIG_BT_AUDIO_TX`,
   which is intentionally absent from this RX-only sink build.
3. Obtain a current CIS handle only through
   `bt_hci_get_conn_handle(ep_info.iso_chan->iso, &handle)`.
4. Allocate a command buffer with `bt_hci_cmd_alloc(K_FOREVER)`, encode
   `struct bt_hci_cp_le_read_iso_link_quality` using
   `sys_cpu_to_le16(handle)`, then call `bt_hci_cmd_send_sync()` with
   `BT_HCI_OP_LE_READ_ISO_LINK_QUALITY` and response pointer.
5. Require a non-NULL response of at least
   `sizeof(struct bt_hci_rp_le_read_iso_link_quality)`, zero response status,
   and returned little-endian handle equal to queried handle. Return
   `-ENOTSUP`, `-EMSGSIZE`, or `-EBADMSG` respectively when those conditions
   fail, always unref a non-NULL response exactly once.
6. Convert all seven 32-bit counters via `sys_le32_to_cpu()` and fill one
   snapshot with current slot and handle.
7. Do not log warnings/errors from this helper; exact errno reaches shell.

For non-nRF54L15 builds, compile a no-command `-ENOTSUP` implementation. Do
not call it from Bluetooth callbacks or while `lifecycle_lock` is held.

### `src/bt_shell.c`

Under `CONFIG_SOC_NRF54L15` only, register nested shell command:

```text
bt iso quality
```

Use `CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT` snapshots as capacity. On success print
exactly:

```text
--- ISO link quality ---
  Stream[<slot>] handle=0x<4-uppercase-hex> tx_unacked=<n> tx_flushed=<n> tx_last_subevent=<n> retransmitted=<n> crc_error=<n> rx_unreceived=<n> duplicate=<n>
```

Print one line per returned snapshot. On any API failure print exactly:

```text
ISO link quality unavailable: <negative errno>
```

and return that errno. Do not expose this command on nRF5340.

Extend `AUDIO_SHELL_TEST` declarations only where required. In
`tests/unit/audio_shell_nrf54`, force
`CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT=2` for the test translation unit if it is
not otherwise resolved.

## HIL evidence model

### `scripts/hil/receiver.py`

Add strict parser for exact header and exact stream lines above. Return a
structured object with `header_seen` plus ordered stream records. Add a pure
validator that requires:

- header present;
- exactly `row.stream_count` records;
- slots exactly `0..row.stream_count - 1`, each once;
- every handle/counter a parsed nonnegative integer.

Do not reject nonzero link-quality counters. They are diagnostic evidence, not
healthy-row exemptions or acceptance thresholds.

### `scripts/hil/runner.py`

Add `bt iso quality` after existing live-tail commands. Collect its transcript
only while source is active during the existing post-`scored_complete` tail,
before session teardown. Parse and validate it, retain it under:

```text
summary.receiver_tail[0].receiver.iso_link_quality
```

Return normal strict failure if command output, count, slot coverage, or parser
grammar is missing/malformed. Preserve all existing tail validation and
warning-scanner behavior unchanged.

### `tests/hil/hil_fakes.py` and `tests/hil/rh2_test.py`

Add a fake `bt iso quality` transcript matching exact grammar. Update every
full fake receiver wire and expected shell-write sequence to supply it.

Add tests proving:

1. exact parser output and malformed/missing/duplicate-slot rejection;
2. a Mode A fake row retains two link-quality snapshots in `summary.json`;
3. nonzero `crc_error` and `rx_unreceived` values remain retained evidence and
   do not themselves fail the row;
4. missing expected Mode A slot fails receiver tail with exact boundary.

### Shell tests

Extend `tests/unit/audio_shell/src/fake_audio_shell_deps.[ch]` with controlled
fake implementation of `bt_bap_iso_link_quality_get_active()` only for shell
boundary tests. In
`tests/unit/audio_shell_nrf54/src/test_audio_shell_nrf54.c`, execute real
`bt iso quality` command and prove exact multi-stream output plus exact errno
error output. Do not assert internal call counts or implementation layout.

## Verification

Run sequentially from repository root. Keep host test build directories outside
repository and retain them for diagnosis:

```bash
shell54_build="$(mktemp -d /tmp/le-audio-receiver-iso-quality-shell54.XXXXXX)"
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d "$shell54_build" tests/unit/audio_shell_nrf54 -p -t run
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
nix develop --command fw-build-5340
nix develop --command fw-build-54l15
nix develop --command python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
sha256sum \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex
python3 -m py_compile scripts/hil/runner.py scripts/hil/receiver.py tests/hil/hil_fakes.py tests/hil/rh2_test.py
```

Treat all compiler warnings, assigned-value warnings, and unlisted CMake/DTS
diagnostics as failures. Existing documented NCS diagnostics only may appear:
partition-manager/sysbuild deprecations, informational `__ASSERT()`/ISO
experimental notices, SW Split choice gap, nRF5340 HCI IPC advanced-features
notice, nRF54L15 intentional FLPR reserved-memory/stock-RRAM DTS diagnostics,
silence those baselines.

Do not run full canonical gate, `fw-build-hil-source`, physical HIL, flash
helpers, OpenOCD, serial tools, or manual hardware commands in this phase.

## Executor rules

Implement only this handoff. Preserve all unrelated dirty worktree changes and
existing immutable HIL evidence. Do not commit. Return changed files, exact
test/build results, new nRF54L15 image SHA-256 values, final `git diff --check`,
final `git status --short`, deviations, and blockers. Stop rather than guessing
if public NCS API evidence contradicts this route, a new unclassified warning
appears, or the change requires controller/RF/threshold behavior.
