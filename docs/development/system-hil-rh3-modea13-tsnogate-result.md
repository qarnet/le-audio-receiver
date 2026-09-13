# RH3 ModeA13 timestamp-mode no-gate result

> [!WARNING]
> Historical run record. Preserve its measurements and immutable evidence, but
> treat its causal classification and proposed next fix as superseded by
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status: completed one-run fix-validation with a negative outcome. At the time,
the result attributed the collapse to a send-driven timestamp readback before
the first SDU completion; ModeA14 later falsified that causal classification.
Removing the host-side time gate collapsed delivery from
`12643` to `167` valid SDUs (`rx_lost=14420`, `plc=28840`, both frozen
floors violated), with completions firing near-instantly
(`cb=24 sub=24 out=0` at the active snapshot). HCI completions proved buffer
return, not the proposed past-timestamp flush mechanism. The classified fix
followed
the canonical Nordic pattern this implementation deviated from: read the
assigned timestamp only after the first SDU's sent completion
(`nrf/samples/bluetooth/iso_time_sync/src/iso_tx.c`: first SDU plain,
`iso_sent` -> `iso_tx_time_stamp_get` -> subsequent SDUs pinned to
`assigned + interval`; nrf5340_audio uses the same VS readback on the
same nRF5340+SDC platform). ModeA14 implements completion-guaranteed
base learning under its own handoff. Not acceptance; everything stays
uncommitted.

## Scope and immutable evidence

One runner-owned execution used the gate-free timestamp-mode source build
and the normal current-HEAD receiver build:

```text
run ID: rh3-modeb-sdc-tsnogate-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-tsnogate-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-tsnogate-20260907.junit.xml
```

`environment.json` records the sole authorized `run` invocation with
`command.status=0`. `result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`.

## Prediction versus outcome

The ModeA13 handoff predicted empty events collapsing to single digits
(`rx_lost` small, `plc` near the mono baseline, window back to 126.4 s).
The outcome was the opposite collapse: `rx_valid=167` below the floor
`11379`, `plc=28840` above `1458` (5% of `decoded=29174`), and the
stream window stayed stretched (`streaming` 20058 -> `scored_complete`
161627, 141.6 s). Per the handoff's FAIL arm this records the outcome
and stops for redesign review, which the deeper evidence then closed:
the completion pacing observed (`cb == sub` with `out=0` inside the
first second) proves the controller freed HCI buffers without waiting
for ISO events, i.e. the pinned timestamps were evaluated as already
past and the SDUs were flushed ("if the timestamp is in the past, the
SDU will be flushed and will not be sent on air" — nrfxlib SDC
isochronous_channels documentation). The base timestamp was learned
from a readback issued immediately after the first send, before any SDU
had aired; on this hardware that value does not represent the scheduled
event of the queued SDU, so every subsequent pin was computed from a
wrong base and landed in the past.

The ModeA12 run's own numbers corroborate the re-anchoring theory: its
host-clock offset (`host_us - assigned` at the same early readback)
absorbed the bad base value, which is why its pins stayed approximately
valid (12643 delivered) while its gate arithmetic (built on the same
offset) oscillated (`pin_adv=2040`).

## Build proof and flashed images

Software verification before the run: native source-app Twister `70/70`
passed; two pristine source builds byte-identical (source CPUAPP
`7f9de7fc367f13be694b3353bde06b75c163e3d2b35354a6e31ee00c39ae0c18`,
CPUNET `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`);
resolved app config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`; only
the documented dirty-tree and global `__ASSERT()` notices. Receiver:
normal current-HEAD `6467e84` build, CPUAPP
`3e12402d90cff3a66d278edde2df6c5e083e76820f7825750979d437426baec5`, FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`,
resolved config `CONFIG_AUDIO_OFFLOAD_ASRC=y`,
`CONFIG_BT_ISO_RX_BUF_COUNT=3`. `images.json` is authoritative for the
flashed tuple.

## Runner outcome

Source lifecycle completed with terminal `verdict="pass"`; all 12644
SDUs submitted and completed (`sub=12644`, `sf=0`, `cb=12644`).

```text
Stream[0] summary: SDUs=167 decoded=29174 plc=28840 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=167 rx_error=0 rx_lost=14420 rx_unknown=0 rx_no_ts=7
```

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: rx_valid=167 below floor: need >= 11379 (90% of 12644 submitted); plc=28840 above ceiling: need <= 1458 (5% of decoded=29174)
```

ISO tail: `nse=3`, `cig_sync_us=4146`, `c_max_pdu=240`, `c_phy=2`,
`retransmitted=0`, `crc_error=0`, `rx_unreceived=14015`,
`duplicate=2`. No warnings or errors on either console; integrity and
raw identity checks passed.

## Classification and stop point

Fixture defect: timestamp base learned from a semantically undefined
pre-air readback (deviation from the iso_time_sync reference pattern).
The classified fix (ModeA14: completion-guaranteed base learning, then
per-send pins with no host-side gate) proceeds under its own handoff.
If ModeA14 fails with a new or unexplained signature, the timestamp-mode
line reaches the plan's consecutive-failure stop point and requires
user redesign review. Everything stays uncommitted pending a passing
row. Preserve this evidence root; do not rerun this ID.
