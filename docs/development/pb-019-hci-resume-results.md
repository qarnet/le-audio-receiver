# PB-019 HCI qualification continuation

## Scope

Continuation runs in the primary repository on
`feature/nrf54l15-only-continuation`, based on `6941c82`. Existing pending
changes and historical run directories remain intact. This record is
diagnostic evidence, not full migration, clean-commit, or release acceptance.

## Interrupted run classification

The immutable `/tmp/opencode/pb019-xiao-h4-standard-qos-120s` run contains
five completed 120-second cases. Mode B reconnect was interrupted and has no
completed CLI result or terminal stream summary. It remains partial.

Mode A reconnect loss is real, not integer-percentage rounding or startup-only:

- Sampled global PLC increased from 78 to 194 while rendered channel frames
  increased from 1082 to 21082: 116 additional PLC frames over 20000 frames.
- Terminal left `rx_valid=12004 rx_lost=36`; right
  `rx_valid=11899 rx_lost=193`. Fresh Mode A had left 12005/35 and right
  12022/70. Each slot's callback total is unchanged between those cases.
- Terminal global `decoded=24080 plc=194`: 0.80565 percent PLC. The right
  slot's later `decoded=0 plc=0` is the already-reset global counter, not
  evidence of zero right-channel loss. `src/bt_bap.c` logs and clears these
  global counters on the first stopped stream.
- Decoder errors, I2S underruns, stream resets, empty SDUs, RX error and
  unknown flags are zero. Midstream assembler overflow messages occurred on
  the old receiver image, which predates the pending PB-034 repair.

The frozen limits in `scripts/hil/receiver.py:validate_stream_transport`
are 90 percent valid delivery per stream and at most 5 percent PLC. Those
numerical limits are not exceeded: right valid/12000 is 99.1583 percent.
Including the CLI's 36-frame release tail in the denominator still gives
98.862 percent. This comparison is not canonical HIL acceptance: the temporary
BlueZ probe does not implement the standalone source's scored/preamble/tail
accounting or all mandatory qualification checks.

### Transport boundary evidence

In the same run's `btmon.log`, Mode A reconnect uses handle 17 for left and
handle 4 for right. Both have 10 ms ISO interval, 2M PHY, BN 1, NSE 3, flush
timeout 2, and reported transport latency 15412 us. Requested RTN 5 is not a
claim that established NSE equals 5.

At capture-relative 423.834090 through 423.834094, six completion events
return together. Next TX pair follows at 423.834118/423.834122. Near the
midstream loss window, previous TX ends at 472.838246, six credits return at
472.854145 through 472.854149, and next TX starts at 472.854172: a 15.926 ms
dispatch gap, then 23 us after final credit return. Similar batching exists
in the fresh case. Over capture seconds [424, 543), the host records 23800
outgoing packets and 11900 completions per CIS: no sustained average-rate
collapse.

This proves host-visible credit batching, not its exact causal connection to
each lost receiver SDU. Number Of Completed Packets does not prove on-air
delivery. The capture lacks per-SDU controller-ingress timestamps and actual
link-quality results for the affected reconnect. RF loss and scheduling
deadline loss are not uniquely separable from this evidence. No SDC-defect
claim follows.

## QoS and regression

Retain the pending standard 48_4_1 timing: 10000 us interval, RTN 5,
20 ms maximum transport latency, and unchanged 40000 us presentation delay.
Installed NCS v3.3.0
`zephyr/include/zephyr/bluetooth/audio/bap_lc3_preset.h` defines those exact
values in `BT_BAP_LC3_UNICAST_PRESET_48_4_1`. Mono/Mode A retain 120-byte
SDUs; Mode B retains 240-byte SDUs. This changes negotiated transport QoS,
not acceptance limits or codec/payload semantics.

Before updating the stale test expectation, the endpoint suite failed only
on `5 != 2`. Public D-Bus response tests now check standard timing for FL,
FR and stereo shapes; the endpoint suite passes 57 tests. Evidence:
`/tmp/opencode/pb019-resume-endpoint-20260924.log`.

## Supervision

New physical diagnostics run in system-manager transient services as the
ordinary user, with 420-second runtime cap, 15-second stop timeout,
`KillMode=control-group` and private umask. Root `sudo` descendants remain
inside the same supervisor. The first launch lacked `/run/wrappers/bin` in
PATH and failed before HCI attachment; its evidence is retained separately.
The corrected launch supplies the host wrapper PATH explicitly. Each probe
rechecks raw DP/AP/FICR identity against the immutable two-XIAO session before
serial interaction. No static probe-to-role mapping is introduced here.

## Completed physical continuation

All following rows use the retained async-r5 HCI firmware and pre-PB-034
receiver firmware. No board was flashed in these runs. Each run retains fresh
probe fingerprints, raw H4 Reset/Read BD_ADDR replies, Linux adapter identity,
source CLI logs, btmon and raw receiver UART. The tests use the dynamically
attached lab adapter, not an assumed hci0 or tty number.

- `/tmp/opencode/pb019-resume-modea-20260924-r2`: fresh and bonded Mode A,
  each 12000 nominal frames over 120 seconds, both CLI exits zero. Fresh
  terminal global PLC 88/24080; reconnect 62/24080. Reconnect sampled PLC
  stayed 62. Fresh quality snapshots at btmon 34.485849/34.514852 report
  zero unacked and CRC-error packets on both CISes, with flush counts 0 and
  12 respectively. This strengthens the scheduling/flush investigation,
  without retroactively attributing the older reconnect's entire loss.
- `/tmp/opencode/pb019-resume-modeb-20260924`: fresh and bonded Mode B,
  both completed 12000 nominal frames over 120 seconds and CLI exit zero.
  Fresh valid 12001, PLC 60/24062; reconnect valid 11955, PLC 170/24080.
  This completes the previously interrupted case in a new run, not by
  modifying or relabeling the interrupted evidence.

### Complete six-case repeat

`/tmp/opencode/pb019-resume-six-20260924` completed all six cases, each
12000 nominal frames over 120 seconds, each CLI exit zero. System-manager
supervisor exited successfully after 13 min 46 s. Terminal counters:

| Case | Valid SDUs (left/right for Mode A) | Global decoded | Global PLC |
| --- | --- | --- | --- |
| Mono | 12007 | 12040 | 33 |
| Mono reconnect | 11983 | 12040 | 57 |
| Mode A | 12005 / 11983 | 24080 | 109 |
| Mode A reconnect | 12005 / 12022 | 24080 | 70 |
| Mode B | 11964 | 24080 | 152 |
| Mode B reconnect | 12007 | 24080 | 66 |

Every terminal summary has zero decoder errors, I2S underruns, stream resets,
empty SDUs, RX errors and unknown flags. Raw UART contains no warning/error
log lines. All rows meet the unchanged numerical delivery/PLC limits; none
is claimed loss-free. Mode A reconnect's sampled PLC remained 70. Other rows
still show small midstream PLC increments, recorded rather than rounded away.

These physical results qualify the prototype's six requested streaming and
reconnect cases numerically. They do not establish supported helper lifecycle
tests, immutable build provenance for a newly rebuilt candidate, final
receiver-image acceptance, complete standalone HIL matrices, or the full
nRF54L15-only migration. PB-019 acceptance boxes remain unchecked pending
those remaining obligations.

## Primary build and exact source image

Fresh primary build used NCS v3.3.0 and toolchain `911f4c5c26`, target
`xiao_nrf54l15/nrf54l15/cpuapp`, single image. The first build exposed an
additional compiler diagnostic in the previously identified SDK initializer:

```text
uart_nrfx_uarte.c:341:70: warning: integer overflow in expression of type 'int' results in '-294967296' [-Woverflow]
```

The static expression multiplies signed integer literals 4000 * 1000000.
The runtime baud-rate path uses an unsigned value and computes the intended
threshold; the existing `uart_configure()` call before RX remains essential.
The SDK also applies the conversion twice in its static initializer. No SDK
file was modified.

Recorded warning exception: only GCC `-Woverflow`, only the audited SDK
`drivers/serial/uart_nrfx_uarte.c`, only in this HCI application. CMake checks
the complete SDK file SHA-256
`6baa5b12680837b2efa47b8b1047284ea2d2d29eb9384895409efee83bcd8066`
before applying the source-local option. Changed SDK bytes fail configure
and require review. The warning concerns an unused static threshold replaced
before reception, not tolerated runtime overflow. Assertions remain enabled;
no warning flag is changed on application sources or other SDK files.

Fresh rebuild `/tmp/opencode/pb019-resume-build-20260924-r2.log` has no
compiler or assigned-value Kconfig warnings. The existing informational
`__ASSERT()` enabled notice remains classified under repository policy.
Both builds produce exactly the already-tested async-r5 HEX SHA-256:
`3e95fba54276d6651e1f099cc6d83e6e9d4c3e98649ee71222a2852b1ea5fed1`.
The HCI source directory now gets its own CMake-generated compilation-database
link. `clangd --check` reports zero real diagnostics; seven `SwapBinaryOperands`
tweak artifacts are check-mode artifacts, recorded in
`/tmp/opencode/pb019-resume-clangd-20260924.log`.

Post-run source-memory comparison is retained under
`/tmp/opencode/pb019-resume-image-verify-20260924-r3`: fresh DP/AP/FICR
fingerprint, OpenOCD `dump_image` for every HEX segment, and byte comparison
against the fresh build. All 199168 image bytes match. No source flash occurred.

Earlier verification attempts are failures, not clean evidence: `verify_image`
tried a target CRC algorithm while running, then a halt-based attempt timed
out. They remain in the unsuffixed and `-r2` evidence directories. Debug
interference left H4 Reset unanswered. A fresh identity-checked source reset
and image readback restored it, under
`/tmp/opencode/pb019-resume-post-debug-reset-20260924`. Subsequent serial-MCP
traffic verified H4 Reset response `040e0401030c00` and Read BD_ADDR response
`040e0a01091000eeddccbbaac0`, each received in 12 ms without lost buffered
bytes. Serial connection was closed. Prefer readback comparison without
target algorithms for future running-controller provenance checks.

## Supported helpers and current receiver proof, 2026-09-24

The supported `fw-build-dongle` clean compile produced single-image
`build/dongle/zephyr/zephyr.hex` for `xiao_nrf54l15/nrf54l15/cpuapp` SDC,
SHA-256 `3e95fba54276d6651e1f099cc6d83e6e9d4c3e98649ee71222a2852b1ea5fed1`.
No actionable compiler/Kconfig warnings; only previously documented
informational `__ASSERT()` notice. Stock SAMD11 bridge remains unchanged,
carrying UART20 P1.9/P1.8 at 1 Mbaud H4 8N1 without flow control. Lab HCI
address is `C0:AA:BB:CC:DD:EE`.

Supported helper evidence (fresh raw identity and load verification retained):

- `/tmp/opencode/pb019-supported-reset-20260924-r1`
- `/tmp/opencode/pb019-supported-flash-20260924-r1`
- `/tmp/opencode/pb019-supported-attach-20260924-r1`: HCI startup, scoped
  attachment and child cleanup succeeded. Attachment resolves actual tty and
  HCI index from live identity, without fixed mapping or persistent service.
- Supported lifecycle suites at parent review: build 7, flash 8, reset 4,
  attach 9 (PTY encoded HCI traffic and real subprocess cancellation, timeout,
  privilege failure); H4 parser suite 1. Reviewed attach fix avoids treating
  `sudo kill -0` status 1 as unambiguous proof of a dead process.

New receiver flash `/tmp/opencode/pb019-new-receiver-flash-20260924-r1`:
cpuapp SHA-256
`eb3607ef67cbabf7e5fb471eb70500b1fa8cab5032e30bcb4eb4ec5c10923633`,
FLPR SHA-256
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Boot recorded `BLE ready`, `FLPR ready` and advertising without warning/error.

Complete supported-helper run `/tmp/opencode/pb019-supported-six-20260924-r2`:
all six rows completed 12000 CLI frames over 120 seconds, exit 0.

| Case | Valid SDUs | Decoded | PLC |
| --- | --- | --- | --- |
| Mono | 12005 | 12040 | 35 |
| Mono reconnect | 11998 | 12040 | 42 |
| Mode A | 12007 / 12022 | 24080 | 68 |
| Mode A reconnect | 12008 / 12000 | 24080 | 89 |
| Mode B | 11948 | 24062 | 166 |
| Mode B reconnect | 12007 | 24080 | 66 |

Decoder errors, I2S underruns, stream resets, empty SDUs, RX errors and
unknown flags were zero in every row; alerts were empty. Frozen 90% valid
and 5% PLC limits passed with **nonzero** loss and concealment. RTN 5 / 20 ms
latency at 10 ms interval is retained standard QoS; these results are not
loss-free or clean-commit release acceptance. The `r1` supported diagnostic
failed before audio due to Path/string input and remains failure evidence,
not a successful stream. No hardware commands ran during this docs integration.

Integration retires obsolete HCI netcore files and unused config fragments,
preserving `dongle/hci_identity.h`, new single-image HCI source and historical
evidence. Active central setup now requires session-bound roles and
system-manager `systemd-run` containment for detached root descendants. Full
migration, exhaustive active-doc audit and clean-tree release acceptance remain
pending.

## 2026-09-25 production-image repeat: qualification withdrawn

Previous prototype and new-receiver six-case successes above remain historical
observations. They are superseded as qualification evidence by
`/tmp/opencode/pb019-final-six-20260925-r1`: mono 12001 valid / 12031 decoded /
30 PLC and reconnect 12007 / 12040 / 33 passed, then Mode A stopped with
8909/12000 CLI frames, 8900 receiver valid per CIS, 18478 decoded, 679 PLC,
one I2S underrun and one stream reset. Kernel at 01:41:09 reported hardware
error `0x07`, then HCI Reset `0x0c03` and Remove CIG `0x2065` timed out.
Private `/tmp/opencode/pb019-final-hci-fault-core-20260925-r1` records parser
`-71` (`-EPROTO`), H4 type 0, last frame 128 and bridge fault 7. Offline
`/tmp/opencode/pb019-ring-decode-20260925-r2.txt` shows inserted `0xAA`
followed by missing `0x03` in adjacent ISO payloads 112 ring positions apart.
This does not isolate the boundary or prove an SDC/SAMD11 cause.

External RAM-trace diagnostic build SHA-256
`d81038ea94e91d5512d7d7b5b481a1243dfcc628f13aae7aa28c16d6385f852a`
used a copied SDK, with no installed SDK or repo production repair. Six trace
cases passed in `/tmp/opencode/pb019-uart-trace-six-20260925-r1`, but tracing
perturbs timing and is not qualification. Private pass core
`/tmp/opencode/pb019-uart-trace-pass-core-20260925-r1` has raw fault 0/parser
0; `/tmp/opencode/pb019-pass-trace-decode-20260925-r1.txt` shows 512 continuous
records, six defer/six resolve, no premature copy. Driver lines 1042-1320
suggest a possible *separate* bounce-prepare hazard: byte 0 and tail >=112
initialized while offset 110 could keep stale data. This predicts substitution,
not the observed insertion/deletion; do not apply speculative repair.

PB-019 AC2 and AC3 now unchecked; AC1 and AC4 remain checked, AC5 pending
audit. XIAO status: **Prototype / qualification incomplete**. Investigate and
repair UART boundary, repeat production-image six cases. This failure is
engineering work, not a technical hard blocker. Raw btmon/RAM/core evidence
may contain bond keys; do not stage or upload. Current-state and clean-gate
authority boundary: the dated migration-verification report
(`docs/development/nrf54l15-migration-verification-results-20261001.md`;
the 2026-09-25 continuation snapshot this line cited was retired at Git rev
`a94f010`).

## 2026-09-25 audited SDK bounce-prepare compatibility repair (software only)

Subsequent review of the passing physical trace establishes a separate,
reproducible defect rather than attributing the earlier failed HCI session.
Record 644514 in `/tmp/opencode/pb019-pass-trace-decode-20260925-r1.txt`
has old bounce offset 107, `prev_cnt=3`, `bounce_limit=110`, and anomaly
address old offset 110, below `swap_len=112`. The existing preparation leaves
that old slot untouched. A legitimate `0xAA` at new offset 0 with DMA pointer
at least 2 bytes into the new buffer causes `anomaly_byte_handle` to replace
it with stale `0x49` from old offset 110. Initializing all 512 bounce bytes
with `0xAA` before use prevents this false repair, regardless of global CC
phase. It does **not** establish a cause or fix for the observed insertion
`0xAA` / deletion `0x03` in the earlier failing session.

`scripts/patch_ncs_uarte.py` checks the exact installed NCS v3.3.0 source
SHA-256 `6baa5b12680837b2efa47b8b1047284ea2d2d29eb9384895409efee83bcd8066`
and unique preparation anchors, then writes a generated driver only under
`build/dongle/`. Only `prepare_bounce_buf` changes: unused `swap_len`, a
full-buffer `memset`, and one full-range cache flush for cacheable devices.
The build replaces exactly one `drivers__serial` source. No installed SDK
source changes, no trace instrumentation, no changed baud, credits, buffers,
timers, acceptance limits, fault policy, or resynchronization. The existing
static-initializer `-Woverflow` suppression remains limited to the audited
generated driver; `uart_configure()` still sets the runtime threshold before
RX. The generated copy and SDK source differ in exactly this function.

Host replay extracts the real `prepare_bounce_buf`, `anomaly_byte_handle`,
and `fill_usr_buf` from each audited original/generated driver. Mocked 32-bit
DMA pointer and cache-disabled nRF54 path produce actual emitted user-buffer
bytes, not a copied predicate. Replayed prefix `8e 11 21` at offset 107,
stale old[110] `49`, and incoming new[0] `aa`, new[1] `1f`: expected
`8e 11 21 aa 1f`, original emits `8e 11 21 49 1f`, generated emits expected.
Matrix includes all 256 incoming values (including `0xAA`) in both new-buffer
and old-tail destinations, stale `49/03/00/aa` and old offsets
107/109/112/125. An old-tail `0xAA` already matches the new-buffer sentinel,
so the emitted payload remains `0xAA`.
Host GCC replay permits only the audited driver's pointer-to-32-bit cast
warning on 64-bit host; production build adds no such suppression.

Verification (no hardware actions or commit):

- `nix develop -c python3 tests/unit/hci_uarte/test_hci_uarte.py`: PASS,
  original 3/8192 emitted-buffer mismatches, generated 0/8192; invalid hash,
  missing/duplicate anchors, same source/output refusal and deterministic
  generated bytes verified.
- `nix develop -c python3 tests/unit/hci_h4/test_hci_h4.py`: PASS (1 test).
- `nix develop -c fw-build-dongle`: PASS, pristine 300/300 build; log
  `/tmp/opencode/pb019-sentinel-build-20260925-r1.log`. Generated source
  SHA-256 `ff0567405dda2d8eb714aaa6e5345d375f6b2500f3daff01af5f55eb4b337cf1`;
  HEX SHA-256 `49b56e93de5bc581d23ef15b3700d8c3625c21cc711d2fbfba7b9c337617a7d9`.
  Resolved `.config` SHA-256 remained
  `690150dcedec3d30ac1b1da314c9944a472bf15cd5d36d6bff571b7615a60645`
  before/after. SDK source SHA-256 remained unchanged. No actionable
  compiler or Kconfig warning; existing informational `__ASSERT()` notice.

Parent review required before any controlled, uninstrumented physical repeat.
Neither host replay nor build qualifies HCI or closes PB-019 AC2/AC3.

## 2026-10-08 preservation appendix: rejected UART-defect experiments and negative facts

> Historical reconciliation appended during the independent-validation
> documentation pass. PR-era chronology snapshots (`nrf54l15-only-resume-
> 20260924.md`, `nrf54l15-only-continuation-20260925.md`, the observability
> checkpoint of 2026-09-25, and the 2026-10-02 PR wrap-up, all at Git rev
> `a94f010`) recorded rejected-experiment details and boundary authority; the
> retained facts are preserved in this report and in the 2026-10-08 appendix
> of `docs/development/nrf54l15-migration-verification-results-20261001.md`.
> Those snapshots are coordination history, not current permissions; the old
> do-not-use-manual-hardware command chain is obsolete and is not carried
> forward (the standing hardware authority in AGENTS.md and the milestones
> plan governs hardware work). No new experiment was run and no conclusion
> changed.

### UART SDK defect and 502→112 threshold workaround (historical, NCS v3.3.0)

The original interrupt-driven H4 sample used one-byte RX DMA and stalled
during streaming; the replacement uses continuous async RX with TIMER-backed
bounce buffers (1024-byte total buffer, 4000 us switch-latency budget).
Setting that budget alone did not work: physical SWD captured assertion
`uart_nrfx_uarte.c:1270`, `bounce_limit < bounce_buf_len`, with a runtime
threshold of **502** despite the requested 4000 us budget. NCS v3.3.0's
static initializer applied the microseconds-to-bytes conversion twice and
then overwrote the correctly computed runtime value during async
initialization. The application therefore calls `uart_config_get()` and
reapplies the configuration with `uart_configure()` before RX starts,
computing the correct threshold **112** for a 512-byte half-buffer at
1 Mbaud/4000 us; SWD verified 112. No SDK file was modified; assertions and
warnings were not suppressed. Key evidence roots:
`pb019-async-stall-core-03` through `-06` (failed state) and
`pb019-async-corrected-threshold` (fixed threshold). An early debugger probe
used unsupported Tcl `mrw` and stopped before its resume command; the next
probe resumed the board. That diagnostic helper requires hardening before
reuse. The threshold 112 is also why the later bounce-prepare false-repair
window is exactly "old slot below 112": the corrected runtime threshold, not
an arbitrary constant.

### Rejected sensitivity experiments: none established a root cause

1. UART idle timeout 100 us instead of 1000 us did not improve peer
   delivery; 1000 us was restored.
2. In-memory NCP timestamps proved the nRF submitted completion events to
   UART about every 10 ms while Linux received bursts of six roughly
   50–60 ms apart. This locates the observed batching downstream of event
   dequeue, not inside SDC; it does not prove which downstream component
   batches.
3. A real-buffer-backed 20-credit bridge queue/proxy was implemented and
   tested as a sensitivity probe. It did not improve receiver delivery and
   was fully removed: no `iso_credit.c/.h`, no event rewriting, no virtual
   credit accounting; `BT_ISO_TX_BUF_COUNT` restored to 6.
4. Source ISO link quality showed 50 flushed versus 3 unacknowledged packets
   in one midstream snapshot, supporting missed scheduling/deadlines rather
   than attributing all loss to over-air errors. Counters are not a direct
   receiver-SDU count, and `hcitool cmd` may print the first unrelated HCI
   event; use the matching Command Complete in `btmon.log`, not that short
   output.

Relevant runs: `pb019-xiao-h4-quality-02`, `pb019-xiao-h4-ncp-trace-01`,
`pb019-ncp-trace-core-01`, `pb019-xiao-h4-async-audio-09` (rejected credit
proxy experiment). Temporary NCP instrumentation was removed. Together with
the 2026-09-25 records above, these experiments **do not individually or
jointly resolve the insertion/deletion fault**; "trace perturbation" (the
RAM-trace six-case pass) and "unknown cause" remain separate unresolved
historical findings, neither retroactively fixed by the generated
driver-sentinel repair. The sentinel eliminates only the stale old-slot
false-replacement class; no per-historical-fault root cause is claimed.

### Aborted six-case run accounting (2026-09-24 session)

The user-aborted `120`-seconds-per-case run retained exactly five completed
case records and one sixth case that had started (UART activity) but has no
completed CLI record or terminal stream summary. It is an interrupted
partial run, not six-case acceptance. Sampled PLC deltas (78→194 with
rendered frames 1082→21082) are the real-loss evidence for the interrupted
Mode A reconnect; rounded `(0%)` UART percentages are never treated as zero
loss - compare exact counter deltas. Detached `sudo`/`btattach` and
`sudo`/`btmon` descendant groups of the abort were cleaned up after fresh
probe enumeration, verifying the owned adapter's lab address `C0:AA:BB:CC:
DD:EE`; only that adapter was touched.

## 2026-09-25 clean gate and latest candidate: physical qualification still fails

Local `0d22829` fixed the test-only HCI output-root portability; `9b99ce5`
adopted the audited 36-file coverage baseline. Canonical gates on clean
commits `9b99ce5` and `a78f8f47101a9c040d8b5f735f96632e853c37b6`
passed 78/0/78 and **79 PASS / 0 FAIL / 79 TOTAL**, respectively. Latest
41 Twister + five exec-only + 30 Python tests, coverage, matrix and strict
BSim 17 scenarios / 26 runs passed. Clean manifest:
`/tmp/opencode/nrf54-a78f8f4-clean-canonical-r1/coverage/run-manifest.json`
(`dirty: false`, source `a78f8f4`); full-gate clone
`/tmp/opencode/nrf54-validation-a78f8f4-gates/le-audio-receiver` clean
before/after. Coverage: 36 files, 4971/5427 lines, 2203/3008 branches,
377/377 functions, no zero-hit numeric functions. Primary unrelated PB-013
dirty edits remained untouched. Separate clean HCI build at
`/tmp/opencode/nrf54-validation-a78f8f4/le-audio-receiver/build/dongle/zephyr`
started clean; only generated LSP symlink changed afterward (recorded).
Uninstrumented HEX SHA-256
`5377fff7bee0256dd59206f46a187d301450b809e4b1db6a67a749bec434b697`.

Host replay of functions extracted from original SDK driver and generated
driver reports original **3/8192** emitted-buffer mismatches, generated
**0/8192**, including all 256 byte values in both destinations. Generated
driver fills the *entire* bounce buffer with `0xAA` and guards exact SDK
source hash `6baa5b12680837b2efa47b8b1047284ea2d2d29eb9384895409efee83bcd8066`.
This is a real old-slot (<112) false-replacement repair, not a demonstrated
fix for inserted/missing bytes. No installed SDK change, trace, changed baud,
credits, buffers, limits or silent resynchronization.

Physical `/tmp/opencode/pb019-a78f8f4-six-20260925-r1` **FAILED** Mode A
reconnect: 7185 sent, RX 7178/7180, underrun 1, reset 1, kernel hardware
error `0x07`; private `/tmp/opencode/pb019-a78f8f4-fault-core-20260925-r1`
records `-EPROTO`. Passive host-monitor variant
`/tmp/opencode/pb019-a78f8f4-monitor-six-20260925-r1` **FAILED** Mode A:
6970 sent, RX 6955/6962, decoded 14628, PLC 711, underrun 1, reset 1,
hardware error `0x07`. Private monitor core
`/tmp/opencode/pb019-a78f8f4-monitor-fault-core-20260925-r1` records parser
`-22`, type 5, used 128, expected 128: SDC lower path rejects malformed
ISO length. Raw btsnoop (6986699 bytes, SHA-256
`c1eeb1d1d888b148bef66d631a0bef74f410fba2e94666192c60b7b65d63857c`)
was copied to private core. Offline comparison
`/tmp/opencode/pb019-monitor-host-compare-20260925-r1.txt`: clean host
record 76837, flags `0x00020012`, ISO TX adapter 2, 128 bytes, header
`02 20 7c 00 32 1b 78 00` (handle 2, sequence 6962). Matching ring
frame 2526 follows H4 at 2525; inserted `0xAA` at frame offset 6 / ring
2532 before SDU length `0x78`, missing `0x0c` at host offset 117 / ring
2644, 112 positions apart. Previous five ISO records (6960-6962) matched
ring exactly; zero captured drops/truncations over 76858 host records.
Fault lies downstream of Linux monitor, before parser. This does not uniquely
separate SAMD11 bridge, wire, UARTE, DMA or driver copy; no SDC or SAMD11
causation claim. Passing RAM trace perturbed timing and cannot qualify image.
Keep all raw btmon/HCI/RAM/core data private and out of Git.

After failures, source was restored to standalone role via runner
`/tmp/opencode/hil-runs/pb035-restored-source-20260925-r2`; `images.json`
retains standalone CPUAPP `51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59`,
receiver CPUAPP `9427913c9595f976cf1644d1ed857b6dc37ad0197fefa29dd4efa35d9e2427bd`
and FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Flash/boot restoration completed; cleanup failures empty, no active HCI
adapter or nrfdebugservertest owner. **Smoke FAILED** strict log scan:
`bt_conn: conn 0x200051b0 failed to establish. RF noise?` (not RF-cause
proof), despite RX 765 / decoded 776 / PLC 11 / zero underrun, reset and
decode error. Historical 20/20 fixed-image matrix remains historical.

Missing measurement capability: `sigrok-cli` exists, but USB enumeration
found no logic analyzer (only hubs, two ASUS Bluetooth adapters, two XIAO
debug bridges and SEGGER J-Link); two XIAOs seen via `nix-nrf probes` do not
establish static role mapping. Need authorized, connected voltage-compatible
high-impedance UART wire capture on source UART20 RX P1.8 (SAMD11 to nRF),
optionally TX P1.9, with common GND and sufficient sampling for 1 Mbaud 8N1
no flow. Compare wire against host and RAM, then make minimal grounded repair
and rerun full uninstrumented six-case matrix and clean gate. Current evidence
does not support another cause-specific fix; software investigation remains
possible. AC2/AC3 stay unchecked, AC1/AC4 checked, AC5 audit pending. No
qualification, RH4/FR4 or release claim.
