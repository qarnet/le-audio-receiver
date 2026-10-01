# All-nRF54L15 observability pause and resume, 2026-09-25

Latest entry point: [2026-09-30 analyzer continuation](logic-analyzer-continuation-results-20260930.md).
It reconciles later clean software/HCI records and failed matrix, then records
one unchanged Mode A row plus passive I2S windows and offline negative controls.
Older pause, board-state and unrun-gate statements below remain dated history,
not current status. Source timing failure and full migration remain open.

## Authority and stop point

**User paused work pending either RTT or logic-analyzer tools.** This is a
handoff, not migration completion or hardware acceptance. Work in primary
repository on `feature/nrf54l15-only-continuation`; never resume former `/tmp`
worktree. New validation clones are verification-only. Implementation checkpoint
`a1a62df42488847bbdaf61f71840d36e14c64520`; clean local code verification
`daf7cd9404e32bacbff4b6431dafccbd28e4a8eb`. This docs-only pause change
may advance HEAD without changing validated code provenance.

Preserve user-owned pending PB-013 feature-refinement file byte-for-byte and
unstaged; `.codebase-memory/` is also untracked. Do not rewind user edits,
apply saved PB-013 patch on top, or implement 360-frame FLPR offload. Local
commits authorized; no push, PR, publication or merge authorized. No backlog
status changes made for this pause; no human-acceptance claims.

Read `AGENTS.md`, this note, `ncs-3.4.1-upgrade-results.md`, and
`pb-019-hci-resume-results.md` first. Older
`nrf54l15-only-resume-20260924.md` and
`nrf54l15-only-continuation-20260925.md` preserve historical chronology, not
current SDK/board assumptions. Component evidence:
`pb-034-primary-repair-results.md`, `pb-035-source-matrix-results.md`,
`pb-036-source-artifact-results.md`, `pb-037-retirement-results.md`.
Preserve immutable run evidence.

## Installed SDK and verified software

- Active SDK `~/ncs/v3.4.1`: nrf
  `b20f8619ba9a5530f8c34b0a130d829947cfe55d`, Zephyr
  `33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`, toolchain pin
  `8285d8ad56` (Zephyr SDK 1.0.1, GNU 14.3). Enter fresh shell with
  `env -u ZEPHYR_BASE nix develop` when old environment active. Do not
  choose SDK by directory order; v3.3.0 remains for historical evidence.
  User's `nix-nrf-dev` working tree untouched. Inspect new RTT tool README/API
  when supplied; do not guess its syntax or supported actions.
- PB-040 **Review**, not Done. Clean clone at `daf7cd9` passed canonical
  **80 PASS / 0 FAIL / 80 TOTAL**: 41 Twister, five exec-only, 31 Python,
  coverage, matrix and strict BSim Stage 1 (17 scenarios / 26 runs). HIL
  Python: 340 passed, one intentional hardware-opt-in skip. Three pristine
  physical-target builds **3/3** and resolved build contract **69/69**.
  Coverage baseline unchanged: 36 files, 4971/5427 lines, 2203/3008
  branches, 377/377 functions. Clean gate preceded builds; generated
  compile-command symlink changes afterward are not post-build clean-tree
  evidence.
- liblc3 revision `48bbd3eacd36e99a57317a0a4867002e0b09e183`, LC3
  fixture bytes, hashes, recipes and limits unchanged. Fixture manifests
  still say v3.3.0; active v3.4.1 host calibration reports original
  `fixture_ncs_version` separately. ARM calibration **completed 296 build
  steps; no ARM test executed**. CI configuration/contract tests changed and
  passed locally; no hosted CI run or push.
- Physical builds: zero compiler/Kconfig warnings. Exact CMake diagnostic
  `__ASSERT() statements are globally ENABLED` is source-checked for enabled
  development assertions. Host-only `native_sim` has NCS unsupported-SoC
  notice. Five exact upstream BSim file/hash-scoped warning exceptions are
  listed in SDK results; no blanket waiver. Retain raw logs; inspect new
  warnings individually.

Clean v3.4.1 outputs **not flashed**. Build artifacts from separate clean
validation clone under
`/tmp/opencode/pb040-validation-daf7cd9/le-audio-receiver/build/`
(or rebuild current primary with provenance):

| `zephyr.hex` image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `716d43fe57b5af2ed1bc8fec9197c9e07bd81f5cab88673cdc8d4aa5fac700bd` |
| Receiver FLPR | `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a` |
| Standalone source CPUAPP | `805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c` |
| HCI CPUAPP | `c2108956760a770e45d8bf52f86736c0bc7da3fa881410c6248414446d88a1d9` |

See `ncs-3.4.1-upgrade-results.md` for exact logs and warning classification.
Build hashes are not board-image evidence or v3.4.1 physical qualification.

## Last known board state, not fresh identity

Last owned hardware action: `/tmp/opencode/hil-runs/pb035-restored-source-20260925-r2`.
Standalone source v3.3.0 SHA-256
`51477c5a23ab81cc3dac3ea93969165d897f6bef6b8aab92a6cc455b05ea5f59`;
receiver CPUAPP `9427913c9595f976cf1644d1ed857b6dc37ad0197fefa29dd4efa35d9e2427bd`;
FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
Restoration flash completed; strict smoke **FAILED** with
`bt_conn: conn ... failed to establish. RF noise?`. No RF-cause proof.
RX 765, decoded 776, PLC 11, zero errors/underrun/reset do not make smoke
pass. Historical physical 20/20 matrix was fixed v3.3.0-image proof only.

No hardware probed, read, reset or flashed for this pause; parent host check
found no running `pb019*`/`pb035*` units then. User may change boards later.
Do not assume current board images. Second XIAO last known **standalone**,
not HCI or trace; it alternates standalone source and Linux HCI roles, never
concurrently. Other XIAO remains receiver with CPUAPP + FLPR; standalone
source carries one CPUAPP.
Resolve fresh raw DP/AP/FICR and USB identity, bound session roles and boot
provenance before target action. No static probe/tty/HCI role table.

## PB-019 HCI fault: proven boundary, unknown root cause

Clean v3.3.0 HCI candidate `a78f8f4`, HEX SHA-256
`5377fff7bee0256dd59206f46a187d301450b809e4b1db6a67a749bec434b697`;
six-case attempt stopped at Mode A reconnect after 7185 frames. Controlled Linux
`btmon` Mode A repeat reached 6970 frames: receiver RX 6955/6962,
underrun 1, reset 1, HCI hardware error 7. Immutable root
`/tmp/opencode/pb019-a78f8f4-monitor-fault-core-20260925-r1` contains
copied exact ELF/HEX, generated SDK driver, RAM core and btsnoop. Compare:
`/tmp/opencode/pb019-monitor-host-compare-20260925-r1.{py,json,txt}`.
Host record 76837, handle 2, sequence 6962: clean 128-byte frame prefix
`02207c00321b7800`. Target parser: frame 128, type 5, error `-22`,
inserted `0xAA` at frame offset 6 (ring 2532, before SDU length `0x78`),
missing `0x0C` at offset 117 (ring 2644, 112 positions later). Prior five
packets matched exactly. Host btsnoop: 76858 records, zero captured drops
or truncation. Corruption lies downstream of Linux monitor capture into nRF
ring; this does **not** uniquely identify SAMD11, wire, UARTE, DMA, driver
or SDC. Earlier `-EPROTO` evidence: `pb-019-hci-resume-results.md`.
Addresses/offsets tied to archived ELF must not be reused for v3.4.1.

Two separate defects/workarounds; do not conflate:

1. SDK UART static initializer double-converts/signed-overflows threshold.
   Pre-RX `uart_configure()` reapplication corrects to 112, not 502;
   generated-source overflow exception narrowly hash-guarded.
2. Bounce prepare initializes byte 0 and tail from 112, not old anomalous
   offset 110. Original driver replay: 3/8192 mismatches; generated full-
   buffer initialization: 0/8192. Fixes false `0xAA` replacement only,
   **not** remaining physical insertion/deletion (later runs still failed).
   v3.4.1 original driver SHA-256
   `d68f45fbef9da8077efe6c9f94c609393fc3485bd1d486e4f710288f6d808bd3`
   re-audited, core logic unchanged; only generated copy patched, never SDK
   source on disk. Do not remove/reintroduce workaround blindly or claim
   SDK upgrade resolved physical fault.

## Observability next step, either tool acceptable

This **supersedes older logic-analyzer-only blocker**: RTT is worthwhile,
not equivalent to independent wire capture. RTT transport is not Zephyr
deferred logging; producer argument/timestamp/buffer-copy cost remains.
Earlier binary RAM trace deferred decoding already; instrumented six-case
pass did not reproduce fault. Instrumentation may alter or mask timing, so
pass is **not** repair proof. SWD/RTT and CDC HCI share SAMD11/USB; polling
can change timing. Record trace/no-trace image hashes.

Prefer fixed binary events with sequence and target timestamps; freeze first
fault and drain afterward. Bound nonblocking writes, count drops explicitly.
No UART logs on H4, formatting or allocation in critical DMA pointer window.
Audit panic path: standard Zephyr RTT backend may block despite DROP mode.
Required tool capabilities: explicit probe; no implicit reset/halt; ELF
control-block address or bounded search; raw binary channel capture;
adjustable polling; bounded cancel/cleanup and surfaced errors. Inspect
provided tool README/API before assuming commands or supported behavior.

Logic-analyzer alternative: passive voltage-compatible RX P1.8, optional
TX P1.9 and common ground, 1 Mbaud 8N1 without flow control. Use only
provided/authorized equipment. No need ask user to choose before tools arrive.
When practical validate suspected behavior-changing repair on physical board;
simulator failure alone does not establish production firmware fault.

## Private checkpoint, backlog and resume order

Ignored private checkpoint `.session-checkpoints/2026-09-25-ncs341-observability/`:
`README.txt`, manifest, **5095 evidence files (31,206,580 bytes) and six Git
snapshots**; all hashes checked, no missing/unreadable files, directories
0700, files 0600. Holds raw fault/trace cores, code/analysis scripts, last
restoration/session binding, clean SDK logs/coverage manifests, PB-013
snapshot. May contain bond keys and raw RAM: **never stage, upload or include in
public PR**. Prefer original immutable `/tmp` runs. If missing, consult
private README/manifest; restore only into fresh external root. Archived
session/probe IDs are not fresh authorization. Checkpoint 0600 file mode
differs from the session `devices.json` immutable binding contract's required
0400: reconstruct that file only under validated contract or create fresh
session after role checks. Scripts embed
historical absolute SDK paths; review/rebind before execution. Older
`.session-checkpoints/2026-09-24-nrf54l15/` retained.

At pause: PB-019/034/035/036/037 **In Progress**, PB-038 **Backlog**,
PB-039 **Ready** (approval record, not parallel implementation), PB-040
**Review**. No invented Done or human merge acceptance. v3.4.1 software
green locally, hardware pending. Exact source artifact RH4/FR4, analog
qualification and public release **not claimed**.

1. Read note and deep records; check branch/status, preserve user edits.
   Verify installed SDK and newly supplied tool documentation.
2. Resolve fresh bound identity and actual boot/image provenance. Explicitly
   select controlled v3.3.0 baseline versus v3.4.1 candidate in variation
   table; never mix image hashes or logs.
3. Reproduce HCI boundary fault with chosen observability, account for
   timing perturbation, retain exact first-fault evidence. Ground minimal
   repair, rerun unchanged six cases with 90% valid / 5% PLC limits and
   zero-error/recovery criteria; no skipped cases or substituted fault.
4. Rerun current canonical software/build gates; complete PB-035/036 exact
   artifact and PB-038 tasks without weakening acceptance. Ask only for
   genuine missing capability or consequential product decision, not
   routine investigation or failing diagnostics.

## 2026-09-27 addendum: passive I2S bring-up, not HCI observability

The 2026-09-25 pause, board-state and tool-availability statements above are
dated historical facts, not a description of current hardware. New passive
analyzer and three normal 48_4_1 standalone-source HIL rows are documented in
[I2S bring-up results](logic-analyzer-i2s-bringup-results-20260927.md). Mono,
Mode A and Mode B 120 s HIL rows passed; six unmodified `.sr` files contain
two 100 ms 12 MS/s I2S windows per row. Complete halves have 16 BCLK rises,
aligned decoder zero warnings. Original Mode A diagnostic supervisor FAILED
on a mid-word decoder start, not a failed HIL row; offline v2 preserves that
verdict/raw log and validates entire raw geometry before analysis-copy crop.
No HCI adapter started. These taps are at receiver I2S, **not** source UART
P1.8; PB-019 fault remains paused and undiagnosed by this work. PB-041 nonce
fixture identification remains unimplemented. Digital rail-high does not prove
DAC presence, voltage or analog output.

Last known post-Mode-B images: PRIMARY receiver CPUAPP
`e02ab5f15213c05038d6e85cbcef585cf1a8db7f41b3653571dfcdb7f3a43aa6`,
FLPR `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`,
standalone source
`805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c`.
These are not the clean-clone receiver/HCI tuple listed above. Source idle,
receiver role unchanged at last check; fresh identity required next session.
Private ignored evidence mirror:
`.session-checkpoints/2026-09-27-i2s-bringup/` (944 verified files; never stage
or use archived identity as fresh authority). Clean 80/0/80 software gate was
not rerun; no firmware change or new hardware action in this docs-only step.
Not full matrix, RH4/FR4, analog or public acceptance. Keep older pause facts
and immutable failed runs intact.
