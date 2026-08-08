# R10 results — final integration and documentation closeout

Accepted: 2026-08-06.  Start commit `4da2df1` (R9 docs acceptance;
worktree clean); handoff commit `987e066`; doc/archive cleanup commit
`6934d9e`; final acceptance commit (this document's commit).  **R10 is
docs/evidence only**: no production or test behavior change, no coverage
baseline regeneration (population stays 33; committed baseline
`54a6b8e` byte-identical), no BSim re-pin (17 scenarios / 26 runs, all
pins byte-identical).  Firmware identity: the production/test tree is
unchanged between the accepted R9 code state (`4da2df1`) and R10 because
R10 is docs/evidence only; T8 commit `971e6a4` is the behavioral
baseline, not an identical source tree — R0–R9 made structural
production-source changes while preserving behavior.  Builds/flashes
used the G1 build trees (build/nrf5340, build/nrf54l15, build/dongle)
produced on `6934d9e`, and the final docs commit changes no firmware.

## Track outcome

**R0–R10 refactor track COMPLETE/ACCEPTED (2026-08-06).**  Every accepted
behavior of the T0–T8 baseline is preserved; the resulting architecture,
inventory, and evidence are the new authoritative baseline.

## Commits

| Commit | Contents |
|--------|----------|
| `987e066` | `docs: record R10 handoff — final integration and documentation closeout` |
| `6934d9e` | `docs: R10 architecture truth cleanup and zero-link archive` |
| `0cb215f` | `docs: accept R10 — final integration closeout, track COMPLETE` |
| (final) | `docs: correct R10 production-tree identity wording` (this document's correction commit; phrase fix — no evidence altered) |

## Doc/architecture truth (all updated in `6934d9e`)

- **AGENTS.md** — status through R10; canonical gate **55 children** =
  31 twister + 5 exec-only + 16 Python + coverage + matrix + BSim;
  coverage population **33** (4024/4402 L, 1695/2356 B, 289/289 F);
  build contract **79/79**; BSim **17 scenarios / 26 runs** (1–9 twice,
  10–17 once); key files include the R4 shell split, R6
  `audio_stream_session.c`, R7 teardown owner, R8 FLPR acceptance
  modules, R9 central modules, and `audio_sink` stream_open/close.
- **README.md** — same inventory/BSim (17 scenarios/26 runs,
  31/5/16/55 children), current `bt_bap`/session/teardown/central blurbs.
- **STATUS.md** — R10 IN PROGRESS section replaced by ACCEPTED/COMPLETE
  (this commit); stale Phase 5/6 "Next actions" table labeled historical.
- **docs/design.md** — old T8 claims labeled historical; new current
  R0–R10 architecture summary/diagram/key-ownership section added; the
  historical nRF5340 ISO-ts frequency claim corrected with an annotation
  (current production: `audio_timing_none.c` no-op, no frequency update;
  phase-only PI from I2S buffer fill — `audio_sink_sdu_ref_update()`
  removed).
- **docs/testing/behavior-contract.md** — baseline provenance updated to
  the current committed baseline `54a6b8e` / population 33 /
  4024/4402 L, 1695/2356 B, 289/289 F / gcovr 8.4 + gcov (GCC) 14.3.0,
  migrated at R4/R6/R8, no R10 migration; ownership clarifications only,
  no outcome weakened.
- **docs/testing/coverage-matrix.md** — stale "current 47/26" language
  replaced; current **55 children / 33 files**; T8 historical
  attribution retained.
- **scripts/bsim-stage1-run.sh** — header corrected: 17 scenarios,
  1–9 twice, 10–17 once.
- **docs/testing/pre-refactor-hardware-baseline.md** — one header
  annotation added (historical T8 evidence; never rewritten).
- **docs/development/refactor-plan.md** — R10 marked ACCEPTED and the
  whole track **COMPLETE** (this commit).

## Archive (25 zero-link files, `6934d9e`, `git mv` preserves history)

Reference search re-run immediately before the move (repo-wide grep for
each filename; only self-references and the handoff's own archive list
matched).  Moved to `docs/development/archive/` (index: `ARCHIVE.md`):

- Pre-refactor T-track handoffs: **8 files** — T1 review-fix; T2, T3, T4
  (each with a review-fix variant); T8.
- BZ desktop-track handoffs: **9 files** — Phase 2 execution handoffs
  (plain, diagnostic, final-review, frame-duration-fix, systemwide),
  Phase 3 execution handoffs (plain, SPA-proof-fix, strict-host-fix),
  and the pairing-mode-filter implementation handoff.
- Refactor handoffs R0–R2: **3 files**.
- Phase 6 intermediate stage results (kept archived):
  `phase6-stage2-results.md`, `phase6-stage3a-results.md`,
  `phase6-stage3b-results.md`,
  `phase6-stage3-hardware-acceptance-results.md`,
  `phase6-stage4a-reset-order-results.md`.

The 20 archived handoff documents above (and the 37 handoffs that
remained in `docs/development/`) were **deleted** at the 2026-08-08 repo
wrap-up; superseding authority is the accepted plan of record
(`docs/development/refactor-plan.md`), `STATUS.md`, and the per-phase
results docs.  Exact per-file names and the move/deletion record remain
recoverable in git history (archive move at `6934d9e`; deletions in the
wrap-up commit).  `docs/development/archive/ARCHIVE.md` indexes the
surviving archived files.

Not moved at R10 (active links then): all refactor results; the
remaining refactor handoffs R3–R9; `pre-refactor-testing-plan.md` and
the pre-refactor T0/T1/T5/T6/T7 handoffs linked from STATUS/
`workstation-transfer-status`/`refactor-r2-results`/`check-test-matrix.py`;
`phase2-stock-desktop-gate-results.md`; `phase3-results.md`;
`bluez-wireplumber-interoperability-plan.md`; the Phase 1 execution
handoff and the linked BZ2/BZ3 handoffs; the dongle probe-selection fix
handoff; the Xiao RF-switch fix handoff; `bsim-stage0/1-results.md`;
`workstation-transfer-status.md`; all phase 4/5 results; phase 6 stage
0/1/4a-runtime/4b/5 results.

## G1 — canonical, on the clean `6934d9e`

Commands (exact):

```bash
time ./scripts/test-all.sh                 # gate1.log
./scripts/test-coverage.sh --output /tmp/r10-coverage --clean-output
fw-build-5340 && fw-build-54l15 && fw-build-dongle
python3 scripts/check-build-contract.py --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
git diff --check
```

Results:

- **`Gate complete: 55 PASS / 0 FAIL / 55 TOTAL`**, exit 0, elapsed
  **17m49.6s** (bash `time`).  Children: 31 twister + 5 exec-only + 16
  Python + coverage + matrix + BSim Stage 1.
- Coverage child: numeric population **33**, baseline enforcement
  **0 error(s)** (committed `tests/coverage-baseline.json`).  Matrix
  child: `check-test-matrix: 0 error(s), 0 note(s)` (zero-hit
  289/289 functions).
- BSim Stage 1: all **17 scenarios strict-checked** (scenarios 1–9 run
  twice, 10–17 once = 26 runs), every pin byte-identical:
  mono_10ms `0x22AB5C0D` (L==R `0x32777D65`), mono_7p5ms `0x01A3EB05`,
  modea_10ms `0xBAE24F7E`, modea_7p5ms `0x2D95D15C`,
  modea_reverse_start_10ms `0xBAE24F7E`, modeb_10ms `0xBAE24F7E`,
  modeb_7p5ms `0xFF82CADB`, invalid_sdu_resume_10ms `0x0C61918D`,
  modea_one_cis_loss_10ms `0x30D6BAF0`, modea_first_stop_10ms
  `0x5A025240`, release_without_disable_10ms `0xAEBD23A1`,
  disconnect_streaming_10ms `0x8500C966`,
  reconnect_second_stream_10ms `0x8500C966` (seg2 = fresh mono
  `0x22AB5C0D`), unsupported_source_direction / no_free_sink_slot /
  invalid_codec_fields `0x00000000`, duplicate_release_10ms
  `0xAEBD23A1`.
- Explicit coverage enforcement (`--output /tmp/r10-coverage
  --clean-output`): **0 error(s)**, baseline PASS; numeric
  lines **4024/4402 (91.4%)**, branches **1695/2356 (71.9%)**,
  functions **289/289 (100.0%)**; gcovr 8.4 / gcov (GCC) 14.3.0; HEAD
  `6934d9e`, worktree clean.
- Builds **3/3** exit 0 (nRF5340, nRF54L15, dongle).  Warning scan:
  zero new/actionable compiler/Kconfig warnings — only the documented
  NCS v3.3.0 diagnostics (PARTITION_MANAGER deprecation, `__ASSERT()`
  informational, SW Split experimental symbols, the
  `BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY` choice gap, FLPR
  `UART_CONSOLE` assigned-but-got, watchdog No-SOURCES), classified per
  STATUS.md.
- Build contract: **79 assertions, 0 failed, BUILD CONTRACT PASSED**,
  exit 0.
- `git diff --check` clean; worktree clean.

## Final-commit G1

The final acceptance commit — this document's correction commit — is
docs-only (results doc + acceptance marks in
plan/STATUS/AGENTS/README/`workstation-transfer-status`).  Per the R10
handoff, the canonical `./scripts/test-all.sh` was re-run on the exact
corrected final commit (this document's correction commit) — **55 PASS /
0 FAIL / 55 TOTAL**, exit 0, gate log recorded against this exact HEAD.
Builds were not re-run for the docs-only commit (recorded code/tree
identity: G1 builds on `6934d9e`; the final commit changes no
source/test/build input — verified `git diff` is docs/markdown only).

## Hardware — nRF54L15 (all PASS)

Fresh R10 runs (evidence: `/tmp/r10-hw/`, `MANIFEST.md` + `SHA256SUMS`).
Central = nRF5340DK hci_uart dongle `C0:AA:BB:CC:DD:EE` (settings
`powered le secure-conn cis-central`, verified before and after);
receiver = Xiao random `DB:A6:0C:05:A2:AA` (probe `8EE9B3FF`, DPIDR
`0x6ba02477`, PART `0x00054b15`, VARIANT `AAC0`).  All ~12000 central
frames @100.0 fps for 120 s rows.

| Row | Command | Evidence |
|-----|---------|----------|
| L1 fresh Mode A 120 s | `bap_central.py --peer-addr DB:A6:0C:05:A2:AA --duration 120` (after `bt unpair`) | 12000 frames; Stream[0] SDUs=11320 decoded=24080 plc=1440 **decode_err=0 i2s_underrun=0 stream_reset=0**; **FLPR submit=12040 success=12040 fallback=0**, faults 0 |
| L2 fresh Mode B 120 s | `--stereo --peer-addr ... --duration 120` (after `bt unpair`) | stereo_b 240-byte SDU; 12000 frames; Stream[0] SDUs=10332 decoded=24070 plc=3406 zeros; **FLPR submit=12035 success=12035 fallback=0** |
| L3 bonded reconnect Mode A 120 s | `--peer-addr --preserve-bond --duration 120` | 12000 frames; Pair() skipped; Stream[0] SDUs=10079 decoded=24078 plc=3920 zeros; **FLPR submit=12039 success=12039 fallback=0** |
| L4 FLPR hang Mode A 180 s | `flpr_hang_gate.py --port /dev/ttyACM0 --duration 180 --peer-addr ...` | **16/16 checks PASS**; 1 recovery, 1 restart, epoch changed, probation cleared, all faults 0 |
| L5 FLPR hang Mode B 180 s | same + `--stereo` | **16/16 checks PASS** |
| L6 BZ3 full | `bluez-wireplumber-phase3-gate.py --receiver 'LE Audio Receiver' --serial /dev/ttyACM0 --duration 30 --log-dir /tmp/r10-hw/bz3 --stage full` | **exit 0, 3/3 playbacks** (fresh pair, persisted-bond reconnect, reset+reconnect); receiver SDUs=4589/4585/4578, decoded=9458/9458/9434, all zero decode_err/i2s_underrun/stream_reset (7.5 ms stock PipeWire) |
| L7 pairing reset + BONDED_ONLY | `bt unpair` (console); fresh OPEN pair 60 s; preserve-bond reconnect 60 s; unbonded-identity connect attempt | `bt unpair` → "Pairing mode reset: bonds cleared; open pairing enabled."; fresh pair 6000 frames; bonded reconnect 6000 frames (Pair skipped); **unbonded own-address (`11:22:33:44:55:66`) connect REJECTED at the LL (HCI 0x0d, 22871 attempts) while the bonded address connects** — best-available public evidence: no second physical central exists on the bench, so the external unbonded attempt used a distinct own-address identity on the same dongle; the receiver-side FAL rejection is proven by the 0x0d response vs the bonded-address success, plus the BONDED_ONLY rebuild/`BT_LE_ADV_OPT_FILTER_CONN` logic is direct-unit-tested |
| L8 stall gate + diagnostics | `flpr_stall_gate.py --port /dev/ttyACM0 --log ...` during a live stream; then `flpr status`, `flpr ring status`, `flpr stress 5`, `flpr ring test 50` | **stall gate GATE PASSED** (timed 60 ms, ack, recovery, probation); `flpr status` healthy (RX lost/dup/ooo/missed 0); `flpr ring status` rings healthy + stall mask 0x01 duration 60 ms; `flpr stress 5` Sent=5 Recv=5 Timeout=0 Stale=0 Mismatch=0 ErrSend=0; `flpr ring test 50` **PASS: all 50 blocks transferred, zero errors** |
| L9 clean boot | reset (OpenOCD `reset run`) + capture | full boot: `BLE ready`, `settings_load() OK`, FLPR handshake READY/ACK, rings/offload/runtime init OK, `Advertising as "LE Audio Receiver"`; **zero warnings/errors** |

## Hardware — E83 / nRF5340 (all PASS)

Receiver = E83 random `E8:54:F0:E0:D9:42` (probe `E6635C08CB1F502B`,
DPIDR `0x6ba02477`, PART `0x00005340`, VARIANT `QKAA`).

| Row | Command | Evidence |
|-----|---------|----------|
| E1 fresh discovery Mode A 120 s | `bap_central.py --duration 120` (discovery, no `--peer-addr`; after `bt unpair` + record removal) | discovery found E83; 12000 frames; Stream[0] SDUs=11317 decoded=22636 plc=2 **decode_err=0 i2s_underrun=0 stream_reset=0** |
| E2 fresh Mode B 120 s | `--stereo --duration 120` | stereo_b 240-byte SDU; 12000 frames; Stream[0] SDUs=11659 decoded=23318 plc=0 zeros |
| E3 bonded Mode B 120 s | `--stereo --peer-addr --preserve-bond --duration 120` | 12000 frames; Pair skipped; Stream[0] SDUs=11663 decoded=23326 plc=0 zeros |
| E4 bonded Mode B 60 s APLL | `--stereo --peer-addr --preserve-bond --duration 60` + midstream `audio status` ×2 | 6000 frames; **Drift state ACTIVE, Drift ppm -500, Resampler identity**; decode errors 0, underruns 0, resets 0; summary SDUs=5833 decoded=11666 plc=0 zeros |
| E5 all-log zero scan | full session logs | zero `ISO seq gap` / discontinuity / `i2s_nrfx: Next buffers not supplied` / `Cannot write in state` / decode / underrun / reset lines across E1–E4 |

## Deviations / environment remediation (non-destructive, recorded)

1. **Dongle 0x0d zombie-slot episode** — first L1 attempts failed with
   HCI 0x0d; root cause: bluetoothd held a live ACL to the E83 (kernel
   auto-reconnect of the R9-era bond) occupying the SDC connection
   context.  Fixed with the documented safe ritual: `bluetoothctl
   remove` the stale device, dongle `reset run` (J-Link), clean
   btattach, btmgmt settings.  No mass erase.
2. **`fw-reset-dongle` probe-selection defect** — it feeds
   `nrf-probes --find nrf53` (Pico serial) into `interface/jlink.cfg`
   → "No J-Link device found" (same defect class as the fixed
   `fw-flash-dongle`; `fw-flash-dongle` itself works).  Worked around
   at runtime with a direct J-Link openocd `reset run`.  Recorded as an
   open follow-up (source fix out of R10 scope).
3. **Two same-name receivers on the bench** — BZ3's name filter cannot
   disambiguate; for L6 the E83 was held off-air (both cores halted by a
   live OpenOCD session with `gdb_port 0` so the gate's Xiao-reset
   openocd could bind); for E1–E5 the Xiao was held off-air the same
   way.  All holds released; both receivers left running.
4. **E83 first E1 attempt underrun storm** — after multi-hold churn the
   E83 logged an I2S underrun storm (stream_reset=343).  Clean reflash
   + full dongle ritual cleared it; the accepted E1 row and all later
   rows are zero-error.
5. **BZ3 stale-bond checks** — the gate rejects leftover bonded
   "LE Audio Receiver" records; records were removed per the gate's own
   procedure (never mass-erased).

## No destructive actions / worktree status

No mass erase, no APPROTECT recovery, no security-policy change, no
audibility claim.  Worktree clean at every commit; `git diff --check`
clean.  Repo left clean on `handoff/workstation-transfer`.
