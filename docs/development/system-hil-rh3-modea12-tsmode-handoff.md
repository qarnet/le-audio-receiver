# RH3 ModeA12 handoff: SDC timestamp-mode ISO TX provisioning

Status: approved source-fixture rework + one fix-validation hardware run.
Implements the SDC-documented preferred data-provisioning mode (timestamps)
in the HIL source host, eliminating the NULL-event mechanism that both
ModeA10 (`plc=2378 = 2 x rx_lost=1189`, `rx_valid=12643/12644`) and ModeA11
(`plc=2374 = 2 x rx_lost=1187`, queue depth 3->6 changed nothing) isolated.
The receiver and the SDC netcore configuration are untouched.

## Root cause (settled from SDC documentation)

SDC pins every TX SDU to a specific ISO event (learned sequence-number
mapping, or time-of-arrival for an untimestamped first packet with sn=0,
which is by definition time-of-arrival per
`nrfxlib/softdevice_controller/doc/isochronous_channels.rst`: "To use this
mode, set the sequence number to 0 and do not add a timestamp"). With
retransmissions configured, "In case data is missing, the controller sends
NULL data every ISO event" (same doc, time-of-arrival section): if the SDU
pinned to an event misses the controller's arrival margin
(`HCI_ISO_TX_SDU_ARRIVAL_MARGIN_US` = 1000 us,
`nrfxlib/softdevice_controller/include/sdc_hci.h:79-83`), that event
transmits NULL and the SDU is carried as a retransmission into a later
event. Queued future SDUs cannot backfill the missed slot, which is exactly
why outstanding depth 3 vs 6 changed nothing (1189 vs 1187 empty events) and
why the stream stretched 5.4% while all 12644 SDUs were ultimately delivered.
The completion-paced host over the nRF53 IPC delivers each refill with
millisecond jitter; each SDU whose refill lands inside the per-event margin
punches one NULL event = one receiver LOST callback = two concealed Mode B
frames.

SDC's documented preferred mode is timestamps:
"In the timestamp mode, timestamps must be provided in the Time_Stamp
parameter... This is the preferred way of providing data to the controller
and guarantees the highest degree of control." Each SDU is pinned by the
host to `assigned_ts + k * SDU_interval`, provisioned ahead of the event.
Reference implementation: `nrf/samples/bluetooth/iso_time_sync/src/iso_tx.c`
(central: first SDU untimestamped; `iso_sent` completion ->
`hci_vs_sdc_iso_read_tx_timestamp` -> next SDU sent with
`bt_iso_chan_send_ts(assigned_ts + interval)`, scheduled
`HCI_ISO_TX_SDU_ARRIVAL_MARGIN_US` (+ 1000 us nRF53 IPC allowance) before
the pinned event). Nordic's production nrf5340_audio host uses the same VS
timestamp readback over the same hci_ipc transport
(`bt_le_audio_tx.c:149-163`), proving the API path works on nRF5340 cpuapp.

Late-timestamp caveat (doc, same section): "if the timestamp is in the past,
the SDU will be flushed and will not be sent on air" - a late pinned SDU is
DROPPED, not shifted. The design below therefore gates each send on a
host-side lead deadline so a pinned timestamp is never already past, and
keeps the completion-paced refill (outstanding target 3) as flow control.

## Scope

### In scope

1. `hil/source/app/src/hil_source_tx.c/.h`: add TS-aware send
   (`hil_source_tx_send_ts`) and timestamp readback
   (`hil_source_tx_read_tx_ts`), both production-only (real backend). The
   fake backend (`tests/unit/hil_source_app/src/fake_hil_source_backend.c`)
   gains matching behavior-preserving fakes (see test plan).
2. `hil/source/app/src/hil_source_app.c/.h`: per-stream timestamp state and
   the send-time gate (details below). No changes to stage caps, lockstep,
   scored counters, stop/abort, drain, or the HIL1 protocol.
3. `hil/source/app/src/hil_source_output.h` (backend ops): new op or
   extended signature (decided below).
4. Native test updates in `tests/unit/hil_source_app` pinning the new
   public-boundary behavior.
5. Software verification (Twister, host runner regression, builds,
   determinism) and ONE fix-validation hardware run:
   `rh3-modeb-sdc-tsmode-20260907`, row `rh3.fresh_mode_b_48_4_1`.
6. Result doc + resume-state + commits per outcome.

### Out of scope

- Any receiver change.
- Any netcore (SDC cpunet overlay/sysbuild) change; the SDC rework stays as
  built in ModeA10/11 (uncommitted until a passing row).
- Changing `HIL_SOURCE_TX_OUTSTANDING_TARGET` default (stays 3; it is flow
  control now, not a timing lever).
- Rows/limits/matrix changes; RH3-7p5 work; PSN_IGNORE/SW-split fallbacks.

## Exact implementation

### 1. Backend ops (`hil_source_output.h`)

Current ops table has `tx_send(uint8_t stream_idx, uint16_t seq, const
uint8_t *sdu, size_t len)`. Add ONE op (do not change the existing
signature; the first send per stream stays untimestamped):

```c
/* Timestamp-pinned send (SDC timestamps mode). ts is the controller-clock
 * CIG event start in microseconds; the controller transmits this SDU in
 * that ISO event. Returns 0 or a negative errno. */
int (*tx_send_ts)(uint8_t stream_idx, uint16_t seq, const uint8_t *sdu,
		  size_t len, uint32_t ts);

/* Read the controller-assigned TX timestamp (us) of the most recently
 * completed SDU on a stream. Returns 0 and sets *ts, or a negative errno
 * (before the first completion). */
int (*tx_read_tx_ts)(uint8_t stream_idx, uint32_t *ts);
```

Fake backend: `fake_tx_send_ts` records the last ts per stream (for test
assertions) and returns 0; `fake_tx_read_tx_ts` returns a scripted sequence
of timestamps (configurable per test; default: fixed base + interval * call
count). Existing tests that only use `tx_send` are unaffected.

### 2. TX driver (`hil/source_tx.c/.h`)

```c
int hil_source_tx_send_ts(uint8_t stream_idx, uint16_t seq,
			  const uint8_t *sdu, size_t len, uint32_t ts);
int hil_source_tx_read_tx_ts(uint8_t stream_idx, uint32_t *ts);
```

- `hil_source_tx_send_ts`: identical buffer handling to
  `hil_source_tx_send`, then
  `bt_bap_stream_send_ts(stream, buf, seq, ts)`
  (NCS v3.3.0 extension, `zephyr/include/zephyr/bluetooth/audio/bap.h:1327`).
- `hil_source_tx_read_tx_ts`: get the ISO conn handle, then the SDC VS
  command, exactly the iso_time_sync/nrf5340_audio pattern:

```c
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/hci_vs_sdc.h>
#include <sdc_hci.h>

int hil_source_tx_read_tx_ts(uint8_t stream_idx, uint32_t *ts)
{
	struct bt_bap_stream *stream = /* attached stream */;
	const struct bt_iso_chan *chan = stream->iso; /* via bt_bap_stream_iso_chan_get if available; else stream->iso per BAP internals guarded by __ASSERT */
	sdc_hci_cmd_vs_iso_read_tx_timestamp_t params;
	sdc_hci_cmd_vs_iso_read_tx_timestamp_return_t rsp;
	uint16_t conn_handle;
	int err;

	err = bt_hci_get_conn_handle(chan->iso, &conn_handle);
	if (err) return err;
	params.conn_handle = conn_handle;
	err = hci_vs_sdc_iso_read_tx_timestamp(&params, &rsp);
	if (err) return err;
	*ts = rsp.tx_time_stamp;
	return 0;
}
```

Check the exact accessor for the ISO channel from a BAP stream at
implementation time: prefer `bt_bap_stream_iso_chan_get()` if present in
v3.3.0 headers, else `stream->iso` (BAP embeds `struct bt_iso_chan *iso`;
verify in bap.h). `CONFIG_BT_HCI_VS=y` is already set (source prj.conf line
81) and `BT_LL_SOFTDEVICE_HEADERS_INCLUDE` defaults y on cpuapp
(`!HAS_BT_CTLR`), so `hci_vs_sdc.c` compiles into the app image
automatically. Resolve the conn handle ONCE per segment (cache it) to keep
the send path allocation-free.

Note: `bt_hci_get_conn_handle` is interrupt-unsafe context? No: it takes
`const struct bt_conn *` and reads a field; but calling HCI sync commands
(`hci_vs_sdc_iso_read_tx_timestamp` uses `bt_hci_cmd_send_sync`) is
thread-context-only. Both call sites are thread context (the app worker and
the completion callback path via `hil_source_app_tx_sent` runs in the BT
RX thread -> must NOT send sync HCI there). Design accordingly: the readback
happens in the TX worker loop, never in the callback (see state machine).

### 3. App coordinator (`hil_source_app.c`)

New per-stream state (reset in `hil_app_tx_run` alongside `tx_seq` etc.):

```c
static bool tx_ts_valid[HIL_SOURCE_MAX_STREAMS];      /* base learned */
static uint32_t tx_ts_next[HIL_SOURCE_MAX_STREAMS];   /* next pinned event, controller us */
static int64_t tx_ts_offset_us[HIL_SOURCE_MAX_STREAMS]; /* host_us - controller_us, resynced per completion */
```

Flow changes inside the send loop:

1. First SDU of a stream (tx_submitted[i] == 0): send via plain
   `tx_send` (untimestamped, sn=0). SDC assigns it an event (time of
   arrival). Mark `tx_ts_valid[i] = false`.
2. After EVERY completion (`hil_source_app_tx_sent`), set a flag
   `tx_ts_readback_due[i] = true` (callback context safe; no HCI there).
3. At the TOP of each loop iteration for each stream with
   `tx_ts_readback_due[i]`: call `tx_read_tx_ts(i, &assigned)`. On success:
   - If `!tx_ts_valid[i]`: this `assigned` is the event of the FIRST SDU.
     Set `tx_ts_next[i] = assigned + interval_us`,
     `tx_ts_offset_us[i] = (int64_t)k_uptime_get() * 1000 - assigned`,
     `tx_ts_valid[i] = true`.
   - Else: re-sync the offset:
     `tx_ts_offset_us[i] = host_us_now - assigned` only if the pinned
     schedule agrees (see step 5 guard; drift correction, not pin rewrite).
   - Clear `tx_ts_readback_due[i]`.
   On failure (`-ENOTCONN` during teardown): if `tx_ts_valid` not yet true,
   leave as-is; the loop's existing stop/error paths own teardown.
4. Send gate (replaces nothing, ADDS to `can_send`):
   `can_send &= (streams' ts gate open)`. For a stream with
   `tx_ts_valid[i]`: the next send is allowed when
   `host_us_now + LEAD_US >= tx_ts_next[i] - MARGIN`, i.e. wait until the
   pinned event is within LEAD. Send AT MOST one SDU per pinned event per
   stream (after sending, `tx_ts_next[i] += interval_us` - the pin advances
   only on send, preserving 1-SDU-per-event alignment).
   Constants: `MARGIN = HCI_ISO_TX_SDU_ARRIVAL_MARGIN_US (1000) +
   1000 (nRF53 IPC allowance) = 2000 us`. `LEAD = 2000 us` (send when the
   pinned event is <= 4 ms away; small lead keeps jitter margin without
   bursting). Define both in `hil_source_app.h` with comments.
   The wait uses the EXISTING `hil_app_tx_wait_until(deadline)` with the
   deadline derived from `tx_ts_next[i] - MARGIN - LEAD` converted through
   `tx_ts_offset_us[i]`; combine with the existing progress-timeout
   deadline (take the MIN).
5. Send call: with `tx_ts_valid[i]`, use `tx_send_ts(i, seq, sdu, len,
   tx_ts_next[i])`; else plain `tx_send` (only the very first SDU). After a
   successful send: `tx_ts_next[i] += interval_us`.
6. Guard against stale pins (burst-after-lull, doc: past-timestamp =
   FLUSHED): before sending, if
   `host_us_now + MARGIN > tx_ts_next[i]` the pin is already too close or
   past. This is a schedule overrun: log one `LOG_WRN` with the values,
   ADVANCE the pin to the next future event
   (`tx_ts_next[i] += interval_us` until it is >= MARGIN ahead) and send on
   the new pin. The skipped events will transmit NULL (concealment at the
   receiver) - this is the bounded degradation path, must stay rare, and
   the warning makes it observable in evidence scans (warning scan treats
   LOG_WRN as failure text in the runner scan? VERIFY: runner scans
   `<wrn>` lines as failures outside named windows - this warning would
   fail the row. DECISION: use `LOG_INF` for the pin-advance notice with a
   distinctive token `TS-PIN-ADVANCE` so evidence retains it without
   tripping the warning gate; a TRUE schedule overrun is visible in the
   counters anyway).

Mode A two-stream note: both streams learn their own base; the stage
lockstep already pairs sends; each stream's gate may open at slightly
different times - the existing stage alignment (`hil_app_frame_stage`
equality check) already serializes pair submission, and the per-stream
`tx_ts_next` advance happens inside the same loop iteration. Keep pins
independent per stream (CIS offsets may differ; correctness first).

`interval_us` source: the configured profile (10000 for `48_4_1`, 7500 for
`48_3_1`); it is already encoded in the QoS presets; read it once per
segment into a local (do not hard-code a second time).

### 4. Native test plan (`tests/unit/hil_source_app`)

Behavior to pin (public boundary: the app's send-decision outputs visible
to the fake backend):

1. First SDU per stream goes out untimestamped (fake records
   `tx_send_ts == false` for the first call).
2. After the fake's `read_tx_ts` reports a base, all later sends are
   timestamped with monotonically advancing pins
   (`base + k * interval`), one pin per send, regardless of completion
   timing bursts.
3. Pin-advance guard: script a readback sequence such that a pin is in the
   past; observe the `TS-PIN-ADVANCE` info token (test asserts via the fake
   that no send ever used a timestamp earlier than the recorded base and
   that pins skip the past event).
4. Outstanding target still enforced (send gate respects
   `tx_outstanding < target`).
5. Stop/abort during TS mode: existing universal cleanup tests still pass
   with the fake readback scripted; no sync HCI is called from callback
   context (the fake's `tx_sent` path exercises the same flag mechanism).
6. All 68 existing tests keep passing (mechanical migration only where the
   new ops are added to the ops table).

### 5. Hardware run

Preflight identical to ModeA10/11 (dirty tree = SDC rework + this handoff +
ModeA12 docs + pre-existing ModeA3 handoff; disk gate; run-ID validation;
fixture validate).

Build: source with NO fragment (target stays 3), double-build determinism;
resolved config must prove the SDC block AND
`CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`. Receiver: normal current-HEAD
build (hash derived at execution time).

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modeb-sdc-tsmode-20260907 \
  --junit /tmp/opencode/hil-runs/rh3-modeb-sdc-tsmode-20260907.junit.xml \
  --row rh3.fresh_mode_b_48_4_1
```

### Prediction (falsifiable)

Timestamp-mode provisioning pins exactly one SDU per ISO event; the NULL
mechanism is gone. Expected: `rx_valid >= 11379` (likely 12644 minus
start-up transients, single digits of rx_lost), `plc <= 1383` (5% of
decoded ~25288; healthy mono baseline showed plc=12; SDC dongle history
plc=2). PASS unblocks the full RH3 matrix.

### Classification arms

- PASS: timestamp mode validated as the fixture's production provisioning.
  Write the ModeA12 result doc, update resume-state, commit the SDC rework +
  TS-mode source change + docs as:

    fix(hil): switch fixture to SDC timestamp-mode ISO provisioning

  Report readiness for the full RH3 matrix (`run-rh3-matrix`, new run ID
  `rh3-matrix-<date>-2`, 14 child runs, outer timeout 10800000 ms).

- FAIL plc still = 2 x rx_lost with near-total delivery: the NULL mechanism
  persists despite timestamp mode. Record, leave uncommitted, STOP for
  redesign review (this would be the second same-shape failure of the SDC
  line and the plan's two-failure stop point; next candidates: VS
  readback semantics mismatch, bn/FT retransmission structure, ACL/CIG
  interleave skipping events - each needing new bounded evidence).

- FAIL new signature (e.g. SDU flush-drops visible as rx_valid floor
  violation, TS-PIN-ADVANCE storms, connection loss): record, classify,
  one bounded fix-validation or STOP for user decision depending on
  cleanliness.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single fix-validation
run only. Status 0/1/130 immutable. Preserve all prior evidence roots,
including `/tmp/opencode/hil-runs/rh3-modeb-sdc-txout6-20260907/`.

## Result documentation

`docs/development/system-hil-rh3-modea12-tsmode-result.md` (canonical):
scope, root-cause summary, prediction vs outcome, preflight, build proof,
resolved-config proof, per-slot summary, limits verdict, TS-PIN-ADVANCE
token count (expect 0 or single digits), FLPR active, QoS, ISO tail,
integrity, raw identity, restoration, stop point.