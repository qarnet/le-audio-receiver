# R9 results — host central orchestration split

Accepted: 2026-08-06.  Start commit `fe5213b` (R8 docs acceptance;
worktree clean); handoff commit `0c09100`; tests commit `41b2e14`;
implementation + tests + inventory truth commit `21d57a5`; docs
acceptance commit (this document's commit).  No production BAP
behavior, firmware source, BSim hash/count, CLI flag, D-Bus object
path, LC3 payload byte, 10 ms pacing, sudo boundary, or exit code
changed.

## Goal met

`scripts/bap_central.py` (1770 lines) is now a thin argparse +
dependency-wiring + main-coordinator (~330 lines).  Discovery,
security/connect strategies, the BAP endpoint, and the LC3
source/writer lifecycle each have exactly one owning module:

- **`bap_central_device.py`** — adapter power, `--peer-addr` exact-peer
  path, existing Device1 enumeration, and bounded `InterfacesAdded`
  discovery.  `DiscoverySession` owns its signal match for its lifetime
  and `StopDiscovery` exactly once (idempotent `close()`, safe from
  `finally`).
- **`bap_central_security.py`** — JustWorks agent class factory +
  registration, `wait_for_helper_ready` (moved verbatim; `READY_PREFIX`
  now imported from `hci_raw_connect.py` — single source), raw-HCI
  fresh-connect strategy (`RawHciConnect` with the exact pre-split argv
  and dual gates), BlueZ preserve-bond Connect strategy, RemoveDevice
  fresh-only boundary, proxy recreation after removal, Pairable/Trusted/
  async Pair/state reads, services-resolved warning (not tightened), and
  cleanup Disconnect.
- **`bap_central_endpoint.py`** — endpoint constants/byte blobs,
  `MediaEndpoint1` class factory (SelectProperties/SetConfiguration/
  Release/ClearConfiguration verbatim), registration, deferred async
  Acquire with pending/acquired fd ownership, second-ASE grace,
  all-or-nothing, and mode inference.
- **`bap_central_session.py`** — lazy liblc3 loader + `LC3Encoder`,
  sine generator, per-mode payload logic, `StreamSession`
  (encoders/writer lifecycle, exact teardown tail), importable without
  liblc3 (stdlib-only tests).
- **`bap_central.py`** — `CentralCleanup`, the single idempotent
  resource owner (fixed teardown order, safe from `finally`), the six
  verbatim CLI flags, `_import_dbus()`, and the flow coordinator.  No
  function owns discovery + security + endpoint + stream + teardown.

`bap_central_policy.py` and `bap_central_writer.py` are unchanged
boundaries; `hci_raw_connect.py` is unchanged (one comment updated).
No production C/matrix/coverage-baseline population change (33).

## Error / resource ownership (as designed)

- Every fatal path inside a module prints its exact pre-split message
  then raises that module's `CentralError`; the CLI catches (exit 1)
  after the `finally`-registered `CentralCleanup` runs.  Deep
  `sys.exit` inside modules was eliminated.
- `CentralCleanup` runs the fixed teardown order — discovery close →
  transports release (fds closed) → writer bounded join/force →
  endpoint unregister → agent unregister → Device1 Disconnect →
  raw-helper terminate — executing only the stages whose resource was
  actually acquired.  Double-run is a no-op.
- The raw helper is terminated on EVERY post-spawn failure (silently on
  error paths, `[cleanup] Raw-HCI helper terminated` on the success
  tail).  Device Disconnect runs before helper terminate; the disconnect
  stage is registered only once a link can exist (raw gate 2 / BlueZ
  Connect / discovery Pair).
- FD ownership is single-owner: `acquire_transports` closes every
  taken fd itself on timeout/partial failure (all-or-nothing); fully
  acquired fds are closed exactly once by the transports stage; the
  writer reads only its snapshot taken at `StreamSession.start()`.
- The error-path cleanup was exercised on real hardware failures (see
  Notes): on two Acquire failures during the E83 bonded rows, the owner
  correctly unregistered endpoint+agent and disconnected the ACL — no
  leak, primary `[error]` lines and exit 1 preserved.

## Tests (tests-first; four new stdlib Python children)

Python children 12 → 16; gate 51 → 55.  New suites (127 tests):

| Suite | Tests | Proves |
|-------|-------|--------|
| `tests/unit/bap_central_device` | 15 | peer bypass path, enum filtering/connected flag, discovery found/timeout/KeyboardInterrupt, StopDiscovery once + signal match removed, close idempotence, power-on, resolve_device composition |
| `tests/unit/bap_central_security` | 50 | agent method surface + registration, wait_for_helper_ready (moved), exact raw helper argv + hold, RemoveDevice fresh boundary + reproxy, preserve-bond connect success/fail/error/timeout/disconnect-first, pair skip/fail/success, helper terminated on every post-spawn failure, services warning, cleanup ordering (disconnect before helper terminate) via the owner |
| `tests/unit/bap_central_endpoint` | 31 | SelectProperties exact byte configs/QoS (mono FL/FR + stereo), SetConfiguration LTV parse + pending state, deferred async Acquire (all success / partial / timeout close all taken fds + Release), all-or-nothing, mode inference, Release/ClearConfiguration idempotent no fd double close, Release never unregisters |
| `tests/unit/bap_central_session` | 31 | lazy liblc3 import, encoder/writer injection, per-mode payload sizes incl. REAL liblc3 golden tests (120 mono / 240 stereo_b concat / 120×2 stereo_a), writer duration/interrupt/error, force-stop, exact teardown order/prints, tolerated errors, idempotent cleanup, helper terminated on pre-stream failure, CLI golden (six flags/defaults/help, path constants, `flpr_hang_gate.launch_bap_central` argv compatibility, `--help` without dbus/liblc3 in a subprocess) |

Shared stdlib fakes live in `tests/unit/bap_central_fakes.py` (not a
gate child).  Updated: `hci_raw_connect` imports `wait_for_helper_ready`
from `bap_central_security` (its 34 HCI/state-machine tests unchanged).
Unchanged and passing: `bap_central_policy` (20), `bap_central_writer`
(6), `flpr_hang_gate` (21), `flpr_stall_gate` (30), both BlueZ suites
(53 + 89), `test_matrix`, `test_coverage_runner`, `bsim_runner`,
`build_contract`, `fw_flash_dongle`.

## G1 (canonical)

`./scripts/test-all.sh` on clean `21d57a5` → **55 PASS / 0 FAIL /
55 TOTAL**, exit 0 (31 twister + 5 exec-only + 16 Python + coverage +
matrix + BSim Stage 1; log `/tmp/r9-hw/gate1.log`).  Coverage child:
population 33 baseline enforcement 0 errors.  Matrix child: 0 errors.
BSim Stage 1: all 17 scenarios strict-checked; existing pins
byte-identical (mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms `0xBAE24F7E`,
reconnect fresh mono oracle, `duplicate_release_10ms`).  Builds 3/3:
`fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` all exit 0 with
zero new/actionable compiler warnings (only the documented NCS v3.3.0
diagnostics: PARTITION_MANAGER deprecation, SW Split experimental
symbols, FLPR-image `UART_CONSOLE` assigned-but-got).  Build contract
**79/79** (`check-build-contract.py` → 79 assertions, 0 failed).
`git diff --check` clean.

## Live hardware (both targets; planned non-destructive resets; no
recovery, no reflash — firmware unchanged)

Evidence: `/tmp/r9-hw/` (`MANIFEST.md` + `SHA256SUMS`; central stdout
for every run, full gate log, build logs, build contract).  Central
dongle C0:AA:BB:CC:DD:EE, settings `powered le secure-conn
cis-central` (verified before and after).  Probes identical to
baseline: XIAO 8EE9B3FF → nRF54L15 (DPIDR 0x6ba02477 PART 0x00054b15
VARIANT AAC0); Pico E6635C08CB1F502B → nRF5340 (PART 0x00005340
VARIANT QKAA); J-Link SEGGER 1366:1061 used for the dongle reset.
Receiver identities from boot logs captured before reset: Xiao
`DB:A6:0C:05:A2:AA` (random), E83 `E8:54:F0:E0:D9:42` (random).  No
audibility claim.

| Row | Central result | Receiver evidence |
|-----|----------------|-------------------|
| Xiao fresh Mode A 30 (`--peer-addr`) | RC=0, 3000 frames @100.0 fps, stereo_a 2×120 B | Stream[0]: SDUs=2094 decoded=6090 plc=1902 **decode_err=0 i2s_underrun=0 stream_reset=0**; **FLPR offload submit=3045 success=3045 fallback=0** busy=0, faults 0, ACTIVE gen=2 |
| Xiao bonded reconnect Mode A 30 (`--peer-addr --preserve-bond`) | RC=0, 3000 frames; BlueZ Connect + disconnect-first + pair-skip | Stream[0]: SDUs=2158 decoded=6072 plc=1756 zeros; **FLPR submit=3036 success=3036 fallback=0** |
| Xiao fresh Mode B 30 (`--stereo --peer-addr`) | RC=0, 3000 frames; **stereo_b single ASE 240-byte SDU** | ASE[0] chan_count=2, QoS sdu 240; SDUs=2329 decoded=6090 plc=1432 zeros; **FLPR submit=3045 success=3045 fallback=0** |
| E83 fresh Mode A 30 (**discovery**, no `--peer-addr`) | RC=0, 3000 frames; discovery scan found E83 RSSI=-36 | SDUs=2580 decoded=5160 plc=1 zeros |
| E83 bonded reconnect Mode A 30 (`--peer-addr --preserve-bond`) | RC=0, 3000 frames (after dongle reset; see Notes) | SDUs=2836 decoded=5674 plc=2 zeros |
| E83 bonded Mode B 30 (`--stereo --preserve-bond`) | RC=0, 3000 frames; stereo_b 240-byte SDU | SDUs=2916 decoded=5834 plc=2 zeros |
| E83 APLL evidence 15 (`--preserve-bond`) | RC=0, 1500 frames | mid-stream `audio status`: **Drift state ACTIVE, Drift ppm -500** (matches R7 baseline), decode errors 0, underruns 0 |

Both connect strategies exercised: raw-HCI fresh (`--peer-addr` without
`--preserve-bond`) and BlueZ preserve-bond Connect, plus the no-peer
discovery scan.  Endpoint mode split proven on both targets (stereo_b
single-ASE 240-byte SDU vs stereo_a two 120-byte SDUs).  Successful
teardown tail preserved byte-for-byte on every row: transports released
→ `Teardown tail: N frames written after the duration (release window)`
(35–37 tail frames — the R7 writer-feeding fix preserved) → endpoint
unregister → agent unregister → ACL disconnect → helper terminate
(fresh paths) → `[main] Exiting`.

## Notes / deviations

- **Stale receiver-side bonds**: the first Xiao fresh pairing failed
  with `AuthenticationFailed` (receiver held a prior bond with the
  dongle; the central-side RemoveDevice alone does not clear it).
  `bt unpair` on the receiver console ("Pairing mode reset: bonds
  cleared; open pairing enabled") fixed it; same for the E83 before its
  fresh discovery row.  Documented AGENTS gotcha, not a regression; the
  fresh rows prove pairing against a cleared receiver.
- **Dongle zombie-slot degradation**: the first three E83 bonded
  attempts failed to establish the second CIS (receiver logged
  `conn ... failed to establish. RF noise?`; central logged Acquire
  timeout / `Input/output error`).  The documented recovery —
  `fw-reset-dongle` (J-Link) + btattach re-attach + btmgmt settings —
  restored the dongle and every subsequent row passed clean.  During
  those failures the new R9 error-path cleanup was exercised for real:
  the finally owner unregistered endpoint+agent and disconnected the
  ACL with no leak and exit 1 preserved.
- Two deliberate, documented behavior deltas from the handoff: (1)
  liblc3 loads lazily at first `LC3Encoder` construction (the
  `[main] liblc3 loaded via ...` line now appears at stream start, not
  import time — required so stdlib-only tests and `--help` never need
  liblc3); (2) early fatal paths now clean up acquired resources via
  the finally owner (pre-split `sys.exit` leaked them) — primary error
  lines and exit codes unchanged, `[cleanup]` diagnostics may follow.
- `wait_for_helper_ready` now imports `READY_PREFIX` from
  `hci_raw_connect.py` (single source; previously defined in both
  files).
- Adapter power-on failure converts an uncaught dbus traceback into a
  clean `[error] Adapter power-on failed: ...` + exit 1 (no pre-split
  message existed to preserve).
- The flpr hang gate hardware row was NOT needed: launcher argv
  compatibility is proven by the CLI golden test (parses
  `launch_bap_central` argv on the new parser) and every stream ran
  through the new CLI end-to-end.

## Non-scope items untouched

R10; security acceptance policy; stricter Pair/ServicesResolved gates;
Pairable/Trusted persistence; 2 s grace; `hci_raw_connect.py` refactor;
desktop BlueZ gate architecture; firmware C changes; BSim pins;
destructive hardware actions; 360-frame FLPR offload.
