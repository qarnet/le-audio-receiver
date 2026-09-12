# RH3 ModeA12 SDC timestamp-mode result

> [!WARNING]
> Historical run record. Preserve its measurements and immutable evidence, but
> treat its causal classification and proposed next fix as superseded by
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status: completed one-run fix-validation with a negative outcome that
isolates the failure to the removed host-side time gate. The first
ModeA12 hardware run (`rh3-modeb-sdc-tsmode-20260907`) kept delivery
near-perfect (`rx_valid=12643` of 12644) but failed PLC worse than
ModeA11: `plc=4098` above `1469` (5% of `decoded=29384`), `rx_lost=2049`
with the `plc = 2 x rx_lost` identity preserved. The run's own evidence
names the mechanism: the status record's `pin_adv` counter ended at
`2040` — the host-clock-offset-based send gate mis-timed on real hardware
(offset learned once from one readback; the stale-pin guard then fired on
roughly every sixth send, advancing pins past their events and producing
empty events). Timestamp-mode pinning itself was sound: every submitted
SDU was delivered and the stream stayed aligned to the controller
schedule. The follow-up fix removes the entire host-side time gate
(offset, lead/margin arithmetic, stale-pin guard) and lets the pins alone
pace the stream: early submission of a pinned SDU is safe per the SDC
documentation (the controller holds it for its pinned event; only a past
timestamp is flushed), and the outstanding target bounds the queue so
pinned SDUs always carry the arrival margin. This is ModeA13 under its
own handoff (`docs/development/system-hil-rh3-modea13-tsnogate-handoff.md`).
This run is not acceptance; the timestamp-mode change stays uncommitted
pending a passing row.

## Scope and immutable evidence

One runner-owned execution used the ModeA12 timestamp-mode source build
(uncommitted SDC rework plus the uncommitted TX timestamp-mode change)
and the normal current-HEAD receiver build:

```text
run ID: rh3-modeb-sdc-tsmode-20260907
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-tsmode-20260907/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-tsmode-20260907.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation
with `command.status=0`, the fixed row, and no diagnostic runner flag.
`result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`. The
runner alone resolved identities, flashed targets, captured consoles,
cleaned up, and finalized evidence.

## Prediction versus outcome

The ModeA12 handoff predicted PASS (`rx_lost` single digits, `plc <= 5%`
of decoded). The outcome falsified the host-side gate design while
validating the pin mechanism:

- Delivery stayed near-total (`rx_valid=12643/12644`, no rx_valid
  violation): timestamp pins never dropped or flushed an SDU.
- Empty events grew (1187 -> 2049) and PLC regressed (2374 -> 4098) in
  lockstep with the guard counter (`pin_adv=2040`): the gate itself
  manufactured the skips it was designed to prevent. The one-shot
  host-vs-controller clock offset made `pin_host` a systematically
  biased estimate; every gate miss triggered the stale-pin guard, which
  advanced pins past their events (empty events at the receiver) faster
  than the resync could heal.

Per the handoff's other-boundary arm this records the outcome, leaves
everything uncommitted, and proceeds to the classified follow-up fix
(ModeA13) rather than a blind retry: the failure is attributed to a
specific, named design element with its own counter, and the fix removes
that element.

## Preflight

Run HEAD was:

```text
0306ba8b72f61b801f4ec3a9fadd70b8c7807821
```

The run-time dirty tree contained exactly the ModeA10 SDC rework, the
ModeA12 timestamp-mode source changes (tx driver, backend ops, app
coordinator, fake backend, native tests, test prj.conf), the ModeA10-12
handoff and result documents, and the pre-existing untracked ModeA3
handoff. Free space was `161133404160` bytes, above the `80 GiB` gate.
Run-ID validation passed; fixture validation returned the known JSON.

## Build proof and flashed images

The ModeA12 software verification (all before the run):

- Native source-app Twister: `71/71` passed (three new ModeA12 tests:
  first-send-plain-then-pinned, pins-advance-one-interval,
  status-carries-pin-advances), run twice more for stability.
- Two pristine source builds byte-identical:
  source CPUAPP `80d621db941f3a5df2fad99c54ddf201d116792fff22e51ccaf1a027f6867422`
  (new: timestamp-mode app code), source CPUNET `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`
  (unchanged from ModeA10/11), merged CPUAPP
  `f9c7ed9af6b310e9c87b06444bd543cf49d0a88ddd9496d137a3d8432c7f3f97`,
  merged CPUNET `40b9aadac6268905477c2c98418429bd93fc3acb8c8a5b0a5160298ea6aebd3a`.
- Resolved app config proved `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`
  (no fragment), QoS unchanged; CPUNET config proved the SDC block.
- Host runner regression `tests/hil/rh2_test.py` + `rh3_matrix_test.py`:
  `257` passed.
- Build output contained only the documented dirty-tree notice and the
  global `__ASSERT()` notice.
- Receiver: normal current-HEAD build, CPUAPP
  `8b29fd44222b6a57b5762da79b51d50ae7cbe6d189bd056874cbd682f8376168`
  (APP_COMMIT-derived), FLPR
  `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`,
  resolved config `CONFIG_AUDIO_OFFLOAD_ASRC=y`,
  `CONFIG_BT_ISO_RX_BUF_COUNT=3`.

`images.json` is authoritative for the runner-flashed tuple (source app
`80d621db...`, source cpunet `19ffe5d4...`, receiver cpuapp
`8b29fd44...`, receiver FLPR `45ab8d15...`).

## Runner outcome and frozen limits

The source completed its full lifecycle with terminal `verdict="pass"`:
`streaming` at `monotonic_ms=19994`, active snapshot
`seq=17 sub=17 sc=0 sf=0 cb=17 out=0 pin_adv=0`, `scored_complete` at
`161839`, `teardown` at `166835`, terminal `pass`, final idle retaining
`seq=12644` with reset counters and `pin_adv=2040` on the completed
segment record. All 12644 SDUs were submitted and every callback fired.

The sole required receiver stream summary was:

```text
Stream[0] summary: SDUs=12643 decoded=29384 plc=4098 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=12643 rx_error=0 rx_lost=2049 rx_unknown=0 rx_no_ts=7
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 12643 | 29384 | 4098 | 0 | 0 | 0 | 0 | 12643 | 0 | 2049 | 0 | 7 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: plc=4098 above ceiling: need <= 1469 (5% of decoded=29384)
```

`rx_valid=12643` met its floor. All zero-gates met. No receiver runtime
warning, source warning, assertion, or shell error was retained on
either console.

## Analysis

| Observation | ModeA10 (no pins) | ModeA11 (no pins, depth 6) | ModeA12 (pins + host gate) |
| --- | ---: | ---: | ---: |
| `rx_valid` of 12644 | 12643 | 12638 | 12643 |
| `rx_lost` | 1189 | 1187 | 2049 |
| `plc` | 2378 | 2374 | 4098 |
| Streaming window (s) | 133.25 | 133.22 | 141.85 |
| Guard fires (`pin_adv`) | n/a | n/a | 2040 |

The `pin_adv=2040` counter (this run's own diagnostic field) attributes
the regression: every guard fire advanced a pin past its event, leaving
that event NULL (one `rx_lost`, two concealed frames). The guard fired
because the host-side gate compared controller-clock pins against a
once-learned host-clock offset; on real hardware that estimate was
biased enough that roughly every sixth send evaluated its pin as already
past the margin, firing the guard. The native suite (whose fake readback
returns a synthetic schedule) never exercised the bias. The fix follows
from the same SDC documentation that motivated the pins: submission
ahead of the pinned event is the intended mode, so no host-side time
gate is needed at all — remove the offset, the lead/margin gate, and the
stale-pin guard; keep only the pins (advance per send, monotonic resync
from readback) and the outstanding depth bound.

## Classification and stop point

Fixture defect: host-side time-gate design in the ModeA12 implementation
(named, counted by its own `pin_adv` field). Not a receiver defect; not
an SDC defect; not acceptance evidence. The classified follow-up fix
(ModeA13: remove the gate, keep the pins) proceeds under its own
handoff. The timestamp-mode source change stays uncommitted pending a
passing row.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all retained entries. The
root and external JUnit files were byte-identical. Runner-retained raw
identity evidence: receiver `serial=8EE9B3FF target=nRF54L15
DPIDR=0x6ba02477 PART=0x00054b15`; source J-Link `001050023938` (raw
scan in `source-jlink.txt`).

## Restoration

No receiver change to restore. The source fixture is flashed with this
run's tuple; the next run (ModeA13) rebuilds the app core with the
gate-free pin design. Do not rerun or reuse this ID.
