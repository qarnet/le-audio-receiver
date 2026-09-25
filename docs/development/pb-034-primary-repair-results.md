# PB-034 primary-repository repair results

## Scope and current status

Work resumed directly in `/home/thomas-workstation/repos/le-audio-receiver`
on `feature/nrf54l15-only-continuation`, based on `6941c82`. The uncommitted
continuation files were moved from the temporary worktree; prior raw evidence
remains in place. Do not resume the old temporary worktree as an independent
implementation.

The product owner clarified that grounded production repairs and expanded
investigation are authorized. The earlier scope-only blocker is withdrawn;
its diagnostic observations remain retained. This supersedes the old PB-034
handoff's production-code non-goal where repair is needed to preserve the
intended audio/loss behavior. No absent-transmission fault was replaced with
empty SDUs, no scenario was removed, and no PCM threshold was loosened.

All results here are dirty-worktree validation, not clean-commit acceptance.
Final canonical coverage and the remaining migration items are still pending.

## Single-CIS transport diagnosis and configuration repair

Temporary plaintext TX/RX instrumentation proved actual payload omission,
not a false timestamp gap. In
`/tmp/opencode/pb034-primary-payload-20260924-01`, mono and Mode B 10 ms RX
sequence 5 carried corpus frame 6, omitting frame 5. The corresponding 7.5 ms
cases omitted frame 7 and frame 8 respectively. These traces are retained;
all temporary instrumentation was removed from source after diagnosis.

The SW Split low-latency policy computes flush timeout from total CIG work
(`zephyr/subsys/bluetooth/controller/ll_sw/ull_central_iso.c`), giving the
single-CIS workload less event-level recovery than two CISes. Mode A traces
showed delayed delivery followed by catch-up at a phase separated from the
mono gap by nine 70 ms ACL intervals. This supports scheduling conflict but
does not claim a controller hardware defect.

An isolated `BT_CTLR_PERIPHERAL_ISO_RESERVE_MAX=n` probe did not fix delivery;
that change was reverted. Selecting the supported SW Split
`BT_CTLR_CONN_ISO_RELIABILITY_POLICY=y` on the client fixed all single-CIS
delivery under unchanged BAP QoS, payloads and numerical acceptance.
Evidence: `/tmp/opencode/pb034-primary-reliability-20260924-01`.
Controller selection remains integrated SW Split, not SDC simulation.

## Absent-CIS receiver repair, with physical regression

The assembler previously dropped surviving left halves once its bounded
queue filled while right delivered no callbacks. Production now emits the
oldest surviving half plus mate-channel PLC at that same bound. A resolved
timestamp fence prevents a late real half or synthesized sequence sentinel
from resurrecting an already emitted event. Wrap-safe ordering, bounded
memory, independent CIS sequence numbers and tolerated short cross-CIS skew
remain intact. Session start-clear also reapplies the configured interval
after clearing assembler state so timestamp-free LOST callbacks advance
correctly after the real production enable/start sequence.

Before modifying production logic, a new public `modea_store()` regression
failed both on native_sim and on the session-bound physical XIAO source:

- `/tmp/opencode/pb034-modea-red-board-evidence`: 15 PASS / 1 FAIL.
- `/tmp/opencode/pb034-modea-green-board-evidence`: 16 PASS / 0 FAIL.
- `/tmp/opencode/pb034-modea-green-native`: 16 PASS / 0 FAIL.

The physical test invokes production assembler code with 18 absent mate
callbacks, both channel directions, both intervals and timestamp wrap. It is
an on-board module regression, not proof of a physical radio-loss injection.
Each physical run retains fresh probe enumeration, raw DP/AP/FICR fingerprint,
image SHA-256, OpenOCD load/verify output and raw UART bytes. Identity was
matched against the immutable PB-033 session manifest before serial open and
again before flash. No permanent probe-to-role mapping was introduced.

Additional real-session/liblc3 tests prove continued sink progress before the
missing CIS returns, exactly-once concealment with either contiguous or
gap-reporting resumed sequences, closed-admission behavior, and configured
LOST timestamp prediction after start-clear. The focused session suite passed
in `/tmp/opencode/pb034-session-green-native.log`.

## Correct target-native PCM provenance

The prior handoff incorrectly assumed startup PLC could not affect a later
loss burst. Pinned liblc3 `48bbd3eacd36e99a57317a0a4867002e0b09e183`
retains its PLC PRNG seed across valid frames: `plc.c` resets seed only at
decoder setup; `lc3_plc_suspend()` resets count/attenuation, not seed.
Eight startup PLC frames advance seed 24607 to 2719 (3200 recurrence steps).
The subsequent 48 valid frames do not erase this difference. Later loss PLC
and recovery MDCT overlap therefore require a distinct zero-start reference.

Both new generated references come from independent host replay of the exact
shared recipe using pinned liblc3 and the established generator flags, not
from captured receiver PCM. All legacy files remain byte-identical:

| New reference | SHA-256 |
| --- | --- |
| `stateful_48k_10ms_skip20_start0_l.pcm` | `cead2e59efb32c78cb87818c710ca727082fd9bb9137bb8255b4f1e37d9be024` |
| `stateful_48k_10ms_loss48x18_start0_r.pcm` | `40204f38b2a359c3d4348131b5900bc1d065fda4423173c6eee5839c1ddf3fbd` |

The generator now rejects generated recipes sharing one output filename.
Strict regeneration passed without rebase. Host and physical ARM calibration
each passed 46 exact records, including a new negative comparison that rejects
using the legacy loss reference for zero-start decoder history:

- `/tmp/opencode/pb034-native-calibration-with-mutation-20260924.json`
- `/tmp/opencode/pb034-arm-calibration-evidence/uart.bin`
  (`PB031_ARM_PASS metrics=46`).

## Stage 1 and negative-path diagnostic

`/tmp/opencode/pb034-primary-canonical-20260924-04.log` records all 17
scenarios / 26 runs passing the strict parser. Log root of the same name
without `.log` retains all peer logs. TX hash pins and fixture bytes are
unchanged. Native startup recipes have zero startup PLC; measured prefix
decoder totals are 68 for first-stop and 55 for release, disconnect,
reconnect segment 1 and duplicate-release.

NCS BAP emits a warning for nonconsecutive send PSNs even though its send
contract requires advancing PSN through omitted SDU intervals. The deliberate
18-event loss test now requires exactly one client diagnostic:

```text
<wrn> bt_bap_stream: Unexpected seq_num diff between 47 and 66 for <stream pointer>
```

This is a checked negative-path observation, not a blanket warning exception.
Wrong gap, role, count, scenario, or any unrelated warning remains fatal.
Parser regressions cover all those cases. Parser suite: 146 PASS / 0 FAIL.

An earlier full unit-phase run was 71 PASS / 1 FAIL / 72 TOTAL, solely due
to a stale one-CIS recipe mirror in the parser (108/26 instead of 100/18).
That mirror was corrected and focused parser validation passed. A complete
unit rerun and final canonical gate remain required.

## Diagnostic limitations and remaining work

The first native red-test build exposed Nix `_FORTIFY_SOURCE` at `-O0`.
The green standalone run removed only incompatible fortify hardening tokens;
normal repository unit runner already scopes this host-only workaround.
The native fake-entropy test banner does not describe production entropy.
No compiler warning was accepted as a production result.

The source board currently runs the ARM calibration image and must be restored
to the source fixture before physical HIL. The receiver was not reflashed in
this repair. Physical stream validation, final coverage accounting, current
documentation updates, and PB-019/PB-035 through PB-038 remain outstanding.

## Matrix witness accounting correction, 2026-09-24

The dirty-tree matrix checker initially reported two stale Mode A witness
names after the approved absent-CIS repair. `tests/test-matrix.json` now cites
`test_oversized_reject_no_mutation` for `MODEA_ACTION_DROP`: an oversized frame
is rejected without changing pending state, and a later valid pair emits.
The former queue-overflow drop transition is no longer expected behavior;
`queue-full->survivor-emitted-with-mate-plc` cites
`test_absent_channel_preserves_survivor_and_recovers`. That public-boundary
test covers either absent channel, 7.5/10 ms intervals, timestamp wrap,
18 absent callbacks, 30 emitted survivors, 18 PLC halves and zero drops.
No test was retired and no transport or PCM limit changed. HCI JSON blocks
were indented to match neighboring matrix entries, without whole-file format.

Validation: `nix develop -c python3 scripts/check-test-matrix.py` reports
0 errors / 0 notes; `nix develop -c python3
tests/unit/test_matrix/test_check_test_matrix.py` passes 45 tests;
`git diff --check` passes. These checks fix manifest accounting; they do not
replace the pending complete unit/coverage/clean-commit gate.
