# All-nRF54L15 continuation: current boundary, 2026-09-25

The section below records the earlier `6941c82` dirty-tree diagnostic
checkpoint. Current committed and physical results follow at the end of this
file. Do not resume the old `/tmp`
worktree, reset this tree, change the committed coverage baseline, or treat
fixed-image diagnostics as clean-commit acceptance. Historical pause record:
`nrf54l15-only-resume-20260924.md`; component evidence: `pb-019-hci-resume-results.md`,
`pb-034-primary-repair-results.md`, `pb-035-source-matrix-results.md`,
`pb-036-source-artifact-results.md`, `pb-037-retirement-results.md`.

## Current diagnostic evidence, not final integration acceptance

- Unit phase: **75 PASS / 0 FAIL / 75 TOTAL** (41 Twister, five exec-only,
  29 Python), `/tmp/opencode/nrf54-only-unit-20260925-r1.log`. RH2 fake
  final: 272; last full HIL test run: 336 plus one intentional hardware opt-in
  skip, before three later tests; focused `scripts/test_hil_runner.py`: 106
  passed. Do not infer a 339-test full rerun. Build-contract unit 52 and
  resolved actual 69/0; matrix unit 45 and actual checker zero errors.
- Final diagnostic build logs:
  `/tmp/opencode/nrf54-only-{receiver,source,hci}-build-20260925-r1.log`.
  Receiver CPUAPP SHA-256
  `9427913c9595f976cf1644d1ed857b6dc37ad0197fefa29dd4efa35d9e2427bd`,
  FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`;
  standalone source CPUAPP
  `51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59`;
  HCI CPUAPP
  `3e95fba54276d6651e1f099cc6d83e6e9d4c3e98649ee71222a2852b1ea5fed1`.
  Source/receiver hashes match the completed fixed-image physical matrix.
- `/tmp/opencode/hil-runs/pb035-xiao-matrix-20260924-r2`: full RH3
  **20/20** children passed, none failed/cancelled/cleanup-failed, including
  7.5 ms, reconnect, hang and stall with unchanged limits. Earlier r1 failed
  preserved Mode B with source BT TX processor stack exhaustion (900 bytes,
  PSP = PSPLIM); board-local 2048-byte repair passed that unchanged row.
  Matrix used earlier imported runner code while later default/schema/docs
  changes landed. It is fixed-image physical diagnostic proof, not final
  clean-commit integration. Latest runner smoke
  `/tmp/opencode/hil-runs/pb035-final-runner-smoke-20260925-r1` passed under
  six guarded identity checks, restored standalone source image and receiver
  images above; system service exited 0 with empty cgroup. Source board no
  longer runs diagnostic HCI firmware. Resolve identities afresh for each
  future target-changing action; never infer static probe or tty roles.
- `/tmp/opencode/nrf54-only-bsim-20260925-r1.log`: strict 17 scenarios / 26
  runs PASS, retained TX hashes, PCM maximum/RMS/minimum correlation
  257/182/32767 under unchanged 2048/512/32750 limits. Strict LC3 generator
  hashes unchanged; 38 calibration tests passed.
- Report-only coverage `/tmp/opencode/nrf54-only-coverage-20260925-r1`:
  population 36, lines 4971/5427, branches 2203/3008, functions 377/377;
  no zero-hit numeric functions. Mode A/session ratios improved, not lowered.
  Historical eight APLL plus one no-HFCLK tests remain, while production APLL
  retirement changes population 37 to 36. Baseline untouched. **This is not
  baseline enforcement.**

## HCI qualification correction

Earlier prototype and new-receiver six-case passes are genuine historical
observations, not final production-image qualification. Repeat
`/tmp/opencode/pb019-final-six-20260925-r1` stopped on Mode A: mono
`rx_valid=12001 decoded=12031 PLC=30`, reconnect `12007/12040/33` passed;
Mode A CLI sent 8909 of 12000 frames, receiver valid 8900 on each CIS,
decoded 18478, PLC 679, I2S underrun 1, stream reset 1. Kernel reported
hardware error `0x07` at 01:41:09, then HCI Reset `0x0c03` and Remove CIG
`0x2065` timed out. Private core
`/tmp/opencode/pb019-final-hci-fault-core-20260925-r1` records parser
`-71` (`-EPROTO`), H4 type 0, last frame 128, bridge fault 7. Offline ring
comparison `/tmp/opencode/pb019-ring-decode-20260925-r2.txt` finds inserted
`0xAA` then missing `0x03` in adjacent ISO payloads 112 ring positions
apart. No unique boundary cause is identified. Do not attribute this to SDC
or SAMD11 without proof, and do not label it fixed or a technical hard blocker.

External diagnostic RAM-trace build SHA-256
`d81038ea94e91d5512d7d7b5b481a1243dfcc628f13aae7aa28c16d6385f852a`
used a copied SDK and changed no installed SDK file or repo production repair.
`/tmp/opencode/pb019-uart-trace-six-20260925-r1` passed six cases, but trace
perturbs timing and does **not** qualify the production image. Private trace
core `/tmp/opencode/pb019-uart-trace-pass-core-20260925-r1` has raw state
fault 0 / parser 0; offline decode
`/tmp/opencode/pb019-pass-trace-decode-20260925-r1.txt` records 512 continuous
records, six defer/six resolve, no premature copy found. SDK driver source
analysis at lines 1042-1320 suggests a separate bounce-prepare invariant
hazard: byte 0 and tail offsets >=112 initialized, while observed anomaly at
offset 110 could retain stale old data. This predicts substitution, **not**
proved insertion/deletion. No speculative SDK repair applied. Keep btmon and
RAM/core captures private; they may contain bond keys. Never stage/upload them.

PB-019 AC2 and AC3 are unchecked following this contrary production-image
evidence; AC1 build and AC4 helper checks remain checked, AC5 awaits audit.
XIAO is a **Prototype / qualification incomplete**, not a lab-qualified or
public supported adapter. AX210 validation is unaffected. Continue controlled
UART boundary investigation and grounded repair with full production-image
six-case repeat; ordinary engineering failure is not a hard blocker.

## Clean gate authority boundary and next steps

Attempted command:

```text
nix develop -c bash scripts/test-coverage.sh --output /tmp/opencode/nrf54-only-clean-coverage-20260925-r1
exit 1 before builds:
FATAL: worktree is dirty — --write-baseline and baseline enforcement require a clean exact commit
```

This is a real **authority blocker for clean-commit acceptance**, not a failed
coverage ratio or license to rewrite the baseline. No explicit local commit,
push, or PR authorization exists. Obtain explicit **local commit** authorization
before clean exact-commit coverage enforcement and exact source archive
provenance. Do not infer permission to push, open a PR, create/rewrite release
assets, or publish. Pending migration changes and all immutable evidence stay
preserved. PB-037 reference hygiene and classified residuals are recorded in
`nrf54l15-active-reference-audit-20260925.md`; the final clean integration
audit and gate remain pending. PB-038 stays Backlog, not silently Ready.

No RH4 physical exact-source-archive acceptance, analog qualification, FR4,
public release, clean canonical software gate, or final migration acceptance
is claimed. Keep HCI engineering work separate from this clean-commit authority
boundary. Review status and docs against latest physical failure before any
future qualification claim.

## Local checkpoint authorization, 2026-09-25

The user subsequently authorized local commits for this migration checkpoint;
the earlier authority-blocker account above remains the historical state when
the clean coverage attempt ran. No push, PR, release action, or acceptance is
authorized by this checkpoint. Preserve the concurrent, unrelated PB-013
product-owner refinement unmodified and unstaged. With that file still dirty,
run future clean exact-commit verification in a fresh throwaway validation clone
of the committed HEAD, never in the old `/tmp` worktree. The clone is for checks
and provenance only; implementation continues in this primary repository.
The uninstrumented production HCI image remains known failing in the final
six-case repeat. This checkpoint does not claim completed migration, HCI
qualification, clean canonical gate, or hardware acceptance.

## Latest committed software and physical boundary, 2026-09-25

Primary branch now contains local commits `21ff2f0`, `0d22829`, `9b99ce5`
and `a78f8f4` (`a78f8f47101a9c040d8b5f735f96632e853c37b6`). User
local-commit authority resolved the earlier clean-tree precondition. Full
canonical gates passed at `9b99ce5` (78/0/78) and latest code `a78f8f4`
(**79 PASS / 0 FAIL / 79 TOTAL**: 41 Twister, five exec-only, 30 Python,
coverage, matrix and strict 17-scenario/26-run BSim). Exact clean manifest:
`/tmp/opencode/nrf54-a78f8f4-clean-canonical-r1/coverage/run-manifest.json`.
Adopted baseline: 36 files, 4971/5427 lines, 2203/3008 branches,
377/377 functions, no zero-hit numeric functions. Fresh validation clone
`/tmp/opencode/nrf54-validation-a78f8f4-gates/le-audio-receiver` was clean
before and after full gate; unrelated PB-013 changes remain dirty and unstaged
in primary. Clean HCI build started clean at
`/tmp/opencode/nrf54-validation-a78f8f4/le-audio-receiver/build/dongle/zephyr`;
after build only generated LSP symlink changed (recorded). Candidate HEX
SHA-256 `5377fff7bee0256dd59206f46a187d301450b809e4b1db6a67a749bec434b697`.
The full-gate clone was separate from that build clone.

Audited generated SDK driver initializes every bounce-buffer byte to sentinel
`0xAA`, with original SDK source hash guard; host replay of actual extracted
driver functions found original 3/8192 mismatches and generated 0/8192,
covering all 256 input byte values in both destinations. No installed SDK
edit, RAM trace, changed baud, credits or limits. It prevents stale old-slot
false replacement of legitimate `0xAA` at offset below 112; it does **not**
close the insertion/deletion mechanism. Uninstrumented candidate
`/tmp/opencode/pb019-a78f8f4-six-20260925-r1` failed Mode A reconnect:
7185 sent, receiver valid 7178/7180, one I2S underrun and reset, kernel
hardware error `0x07`, private core
`/tmp/opencode/pb019-a78f8f4-fault-core-20260925-r1` parser `-EPROTO`.
Controlled passive Linux monitor variant
`/tmp/opencode/pb019-a78f8f4-monitor-six-20260925-r1` also failed Mode A:
6970 sent, RX 6955/6962, decoded 14628, PLC 711, one underrun and reset,
hardware error `0x07`. Private monitor core records parser `-22`, H4 type 5,
used/expected 128/128: SDC lower path rejects malformed ISO length. Offline
host-vs-ring comparison in `/tmp/opencode/pb019-monitor-host-compare-20260925-r1.txt`
locates clean host record 76837 (handle 2, sequence 6962) against ring frame
2526: inserted `0xAA` at frame offset 6 and missing `0x0c` at offset 117,
112 ring positions apart; prior five ISO records matched exactly. Host
btsnoop had zero recorded drops/truncations across 76858 records. Fault is
downstream of Linux monitor and before parser; evidence does not isolate
SAMD11, wire, UARTE, DMA or driver copy, nor justify SDC causation. Earlier
RAM-trace pass perturbs timing and cannot qualify this production image.
Sensitive HCI/RAM captures and cores stay private outside Git.

After failed HCI runs, identity-checked restoration used runner
`/tmp/opencode/hil-runs/pb035-restored-source-20260925-r2` and retained
`images.json`: source standalone CPUAPP
`51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59`,
receiver CPUAPP `9427913c9595f976cf1644d1ed857b6dc37ad0197fefa29dd4efa35d9e2427bd`,
FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Flash/boot restoration completed, cleanup failures empty, no active HCI
adapter or nrfdebugservertest owners. Strict smoke **FAILED** log scan on
receiver warning `bt_conn: conn 0x200051b0 failed to establish. RF noise?`;
that text does not prove RF causation. RX 765, decoded 776, PLC 11, zero
underruns/resets/decode errors do not turn failed smoke into a pass. Earlier
20/20 fixed-tuple matrix remains historical diagnostic, not fresh acceptance.
Revalidate live identity before future hardware work.

**Measurement blocker:** parent checked `sigrok-cli` installed at
`/run/current-system/sw/bin/sigrok-cli`, but USB enumeration showed hubs,
two ASUS Bluetooth adapters, two XIAO debug bridges and SEGGER J-Link, no
USB logic analyzer; `nix-nrf probes` saw two XIAOs. Need user-provided,
connected authorized voltage-compatible high-impedance logic analyzer on
source UART20 RX P1.8 (SAMD11 to nRF), optional TX P1.9 and common GND,
sampled adequately for 1 Mbaud 8N1 no flow control. Wire bytes matched
against host and RAM can isolate boundary; current evidence does not support
a further grounded cause-specific fix, not a claim that software debugging is
impossible. Then make minimal grounded repair and rerun all six physical
cases plus clean full gate without silent resync, audio removal, or relaxed
limits. PB-019 AC2/AC3 remain unchecked; AC5 pending. PB-038 stays Backlog.
Exact-source archive RH4, analog qualification, FR4 and final migration
acceptance remain pending. No push, PR or release action.
