# RH3 ModeA16 grid-bound result

> [!WARNING]
> Historical run record. Preserve the measured grid bound and receiver
> counters, but do not infer an exhausted host design space or an SDC defect.
> HCI completions do not count aired SDUs. The current interpretation and next
> gate are in
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status: completed one-run fix-validation with the pre-committed
outcome: the grid bound HELD in every status record
(`pin_last - rb_last <= 20000 us`, within the 40000 us bound; the active
snapshot shows GAP=0), yet the receiver recorded only 168 valid SDUs of 12644
(`rx_valid=168`, `plc=28810` above `1457`, `rx_lost=14405`, `rb_cnt=12642`
readbacks with HCI-level completions for every submission, `sf=0`).
Per the ModeA16 handoff this is the "bound held, delivery still
collapsed" arm, pre-classified as: the flush mechanism is not (only)
buffer pressure, and the next candidates were controller-side. That
historical classification is superseded: the corrected 128 MHz
controller-clock source now passes Mode A and Mode B. The stop point remains
part of the immutable run history, not the current next action.

## The five-run evidence chain (ModeA12-16)

| Run | Host chain | Pins on grid? | Bound | rx_valid |
| --- | --- | --- | --- | ---: |
| ModeA12 | host-clock gate + guard | yes (delivered 12643) | guard fired 2040x | 12643 |
| ModeA13 | none, send-driven rb | (unknown) | none | 167 |
| ModeA14 | none, completion-driven rb | yes (GAP=2 intervals at end) | none | 167 |
| ModeA15 | none (diagnostic) | yes (GAP=2, rb grid proven) | none (free-run proven) | 167 |
| ModeA16 | completion-driven rb + 4-event grid bound | yes (GAP<=2 everywhere) | HELD (<=20000 us) | 168 |

Recorded facts (all from runner-retained raw evidence):

1. The readback advanced by a 9999.05 us mean across 12642 samples, with no
   wrap observed.
2. The ModeA15/16 pin values retained the expected small interval relation to
   those readbacks.
3. The four-event future-pin bound held, while the receiver-delivery signature
   remained near ModeA13-15.
4. The host received an HCI completion for every ISO send (`sf=0`), while the
   receiver recorded only the first ~168 valid SDUs (~1.7 s). These counters do
   not establish which later SDUs aired or the controller's buffer-return cause.

Historical conclusion at the time: after ~1.7 s of streaming the receiver
stopped recording valid SDUs while the host continued to receive HCI
completions. The result then treated controller configuration or pinned-SDU
handling as the next candidates, including the FT/nse/bn retransmission
structure (FT=16
subevents = 20 ms window; nse=3; bn=1), the CIG reserved time
(`BT_CTLR_SDC_CIG_RESERVED_TIME_US`, default 1300), the SDC ISO TX HCI
buffer counts, or a pinned-SDU flush path in SDC itself.

## Scope and immutable evidence

One runner-owned fix-validation execution used the ModeA16 grid-bound
source build and the normal current-HEAD receiver build:

```text
run ID: rh3-modeb-sdc-rbbound-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-rbbound-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-rbbound-20260907.junit.xml
```

`environment.json` records the sole authorized `run` invocation with
`command.status=0`. `result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, `cleanup_failures=[]`.

## Raw fields (the run's evidence)

| Status record | seq | rb_last | rb_cnt | pin_last | GAP |
| --- | ---: | ---: | ---: | ---: | ---: |
| active (streaming) | 29 | 19848090 | 29 | 19848090 | 0 us |
| final idle | 12644 | 145978090 | 12642 | 145998090 | 20000 us |

The GAP never exceeded 2 intervals (20000 us), well inside the 4-event
bound: the host queued at most a couple of future-pinned SDUs at any
time, exactly the ModeA12 delivery regime's depth - and the collapse
persisted.

Receiver summary:

```text
Stream[0] summary: SDUs=168 decoded=29146 plc=28810 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=168 rx_error=0 rx_lost=14405 rx_unknown=0 rx_no_ts=7
```

Runner-validated frozen-limit failure:

```text
stream summary slot 0 transport limits: rx_valid=168 below floor: need >= 11379 (90% of 12644 submitted); plc=28810 above ceiling: need <= 1457 (5% of decoded=29146)
```

## Build proof and software verification

Native Twister `70/70` three times; two pristine source builds
byte-identical (source CPUAPP
`f9a5144a9e53a40c6e3c15ee8e55a93432145847f8327c101f8d8d53bad09c8c`,
CPUNET unchanged `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`);
resolved app config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`; only
the documented notices. Receiver: normal current-HEAD `2a944fe` build
(CPUAPP `a7691e31560c6ea6d0ce1fd25abc0fc1808770af82f08ee987f8c66c804a85d9`,
FLPR `45ab8d15...`, `CONFIG_AUDIO_OFFLOAD_ASRC=y`,
`CONFIG_BT_ISO_RX_BUF_COUNT=3`). `images.json` is authoritative for the
flashed tuple. No warnings on either console; integrity checks passed.

## Historical stop point and options

> [!NOTE]
> This option list records the decision context at the time. It is not current
> execution guidance. The DevZone path is withdrawn, and the next action is the
> full twice-run RH3 matrix with the corrected source.

At the time, the host-side timestamp-mode line was treated as exhausted and the
remaining levers were classified as controller-side or fixture-level:

1. Controller-config diagnostic: one run varying the SDC ISO
   retransmission/buffer structure on the netcore overlay
   (`BT_CTLR_SDC_ISO_TX_PDU_BUFFER_PER_STREAM_COUNT`, CIG reserved
   time, or the FT/nse/bn selection inputs through the BAP QoS latency
   parameter - the current 20 ms latency with RTN 5 selected nse=3,
   FT=16). Each is a bounded single-variable run like ModeA15.
2. DevZone escalation: the five-run chain is a complete, evidence-backed
   question about SDC central ISO pinned-SDU airing on nRF5340
   hci_ipc (NCS v3.3.0); Nordic's answer may name the mechanism
   directly.
3. Fixture redesign review: drop timestamp mode; return to the ModeA10
   ToA configuration (delivered 12643/12644 with ~1200 empty events)
   and attack the ~1200 empty events from the controller-config side
   (the empty events there were schedule-interleave artifacts, and the
   controller knobs above may address them without pins at all).
4. Restore the ModeA12 host-gate variant (delivered 12643/12644, guard
   fires 2040 times): worst PLC of the delivered configurations, still
   far from passing; not recommended without new understanding.

Everything stays uncommitted pending the decision. Preserve this
evidence root; do not rerun this ID.
