# R6 results — BAP receive-pipeline decomposition

Accepted: 2026-08-05.  Start commit `860ea02` (R5 docs acceptance;
worktree clean); handoff commit `d2b9a91`; tests+implementation commits
`c3b4e7b` + `3c7396a`; coverage migration commit `67d2a18`; docs
acceptance commit (this document's commit).  No production BAP behavior,
BSim hash/count, callback order, malformed/gap mutation order, stream
behavior, counter, log/observer event, timing update, or ASCS stack
ownership changed; no BSim pins changed.

## Goal met

`src/bt_bap.c` (1673 → ~1300 lines) is reduced to Bluetooth
service/lifecycle orchestration; the new `src/audio_stream_session.{c,h}`
is the exclusive owner of app audio receive/session state and the
decode/conceal/volume/push mechanics:

- validated codec shape per slot (set at Config, revalidated at Enable);
- LC3 decoder contexts with static memories;
- per-slot `audio_iso_seq` trackers;
- shared Mode A `modea_state`/`modea_event` + `l_buf`/`r_buf`/`stereo_out`;
- configured occupancy, `pd_us`, `recv_cnt`, mode inference
  (mono / Mode B / Mode A);
- malformed-SDU rejection (exact order: seq update, then length check),
  omitted-callback synthesis (PLC push vs synthetic Mode A LOST sentinel),
  and one common decode→volume→observer→sink-push tail for the normal and
  concealed mono/Mode B paths plus the Mode A event decode/interleave/push;
- the corresponding `audio_stats_*`, `audio_perf_*` (decode wrap +
  `push_failure`), and `bsim_observer_*` calls, moved without reordering.

`bt_bap.c` retains the `bt_bap_stream` pool, ASCS callbacks and config
responses, `qos_pref`, pairing/PACS/advertising, `lifecycle_lock` +
`audio_path_generation`, the gate orchestration (`sink_close_audio_path`,
`stream_started` transactional open), the recv adapter (decomposes ISO
info into scalars; owns the ISO_RECV perf wrap, the `idx==0 && valid &&
has_ts && gate_open` timing-reference update using the session's stored
pd, the gate snapshot, and the gate-blocked throttle +
`bsim_observer_recv_gate_blocked`), and the shell `bt_bap_audio_path_stop`.

## Admission/lease design (implemented as decided)

One session mutex + condvar guard admission/generation/in-flight only;
never decode/sink/BT calls.  `recv()` acquires a lease under the lock
(rejecting `-EINVAL` when admission closed / slot invalid / not
configured), runs the whole decode path outside the lock, then releases
and broadcasts the drain condvar.  `rx_close()` (idempotent) closes
admission, bumps the generation, and waits for admitted leases to drain.
Every close path (`sink_close_audio_path` forced and normal, release,
disconnect) calls `rx_close()` before any decoder/assembler/sequence
reset; the reset calls themselves (`release`, `reset_all`, `start_clear`)
never reopen admission.  Only `rx_open()` at the successful
`stream_started` gate-open edge enables admission; a failed/stale open
leaves/returns admission closed.  The session mutex is provably never
held across decode/sink (dedicated concurrent tests with a parked fake
sink push).  No session API acquires `lifecycle_lock`; `rx_close()` runs
outside the lifecycle-lock hold.

## Ownership map (what moved / what stayed)

| Concern | Owner after R6 |
|---------|----------------|
| `bt_bap_stream` pool, ASCS callbacks, config responses, `qos_pref` | `bt_bap.c` |
| Pairing/PACS/advertising, `default_conn`, `sem_disconnected` | `bt_bap.c` |
| `lifecycle_lock`, `audio_path_generation`, gate orchestration | `bt_bap.c` |
| Recv adapter: perf wrap, timing update, gate snapshot, gate-blocked throttle/observer, scalar decomposition | `bt_bap.c` |
| Validated codec shape, decoder ctx, per-slot seq tracker, Mode A assembler, recv counters, pd, mode inference | `audio_stream_session.c` |
| Malformed rejection, omitted-callback synthesis, decode/conceal/volume/push + stats/perf/observer | `audio_stream_session.c` |
| Receive admission/lease (rx_open/rx_close, generation, in-flight) | `audio_stream_session.c` |
| Configured/started/audio-path gate, occupancy | `stream_lifecycle.c` (unchanged role; `sink_configured` narrowed) |

`struct bt_bap_stream` / conn / ep / codec_cfg / qos / iso are never
touched by the session; the recv adapter copies only scalars and `buf->data`.

## stream_lifecycle narrowing

`stream_lifecycle_sink_configured(size_t idx, int chan_count)` →
`stream_lifecycle_sink_configured(size_t idx)`; `sink_chan_count[]`
became `bool sink_occupied[]`.  The gate decision logic is unchanged (it
already counted configured slots, never the chan_count value); every
`chan_count > 0` test became `occupied`.  Lifecycle suite: 31 → 28 tests
(three obsolete chan_count-specific tests deleted:
`test_zero_chan_count_sink_absent`, `test_negative_chan_count_inert`,
and `test_idle_force_close_clears_stale_latch`, which depended on
`configured(idx, 0)` that no longer exists; released-slot inert-start
remains covered by `test_release_then_slot_reuse`).  All other tests pass
unchanged with the new signature.

## Direct test suite — `tests/unit/audio_stream_session` (29 tests)

Compiles production `audio_stream_session.c` + `audio_decode.c` +
`audio_modea.c` + `audio_iso_seq.c` + `audio_stats.c` + `audio_perf.c`
against faithful fake sink/volume/observer seams, the checked-in 48 kHz
LC3 fixtures, and a linker-wrapped `lc3_decode` (fail-all mode for hard
decode failures).  `CONFIG_AUDIO_PERF_MEASUREMENT=y` (real push-failure
counters), suite-local Kconfig for `BSIM_OBSERVER` / `BSIM_SINK_POOL_LIMIT`
and minimal BT buffer configs so the observer header compiles.  Coverage
of the decided matrix: config/accessors/invalid slots (NULL shape,
out-of-range idx, unconfigured recv → `-EINVAL`); mono/Mode B/Mode A
classification and golden decode+push (raw fixture hashes — the fake
volume is identity, the .pcm fixtures ARE the interleaved decode output);
Mode A equal-TS pair, one-sided loss (PLC half source-validity via the
observer), missing-TS valid rejection with no assembler mutation;
malformed length rejection (one decode error, no decode/push/Mode A
mutation, valid input resumes — mono and Mode A); LOST-SDU PLC; decoder-
not-ready skip; hard decode failure skip (mono/Mode B) and Mode A event
consumption; sequence-gap PLC cadence (mono: 2 PLC + 1 valid; Mode B: 4
PLC frames) and Mode A synthetic-LOST ordering; out-of-window resync with
no synthesis; admission closed → `-EINVAL` no decode/push → rx_open
restores; rx_close blocks until an admitted lease parked inside the fake
sink push drains (real threads) and rejects late RX; the session mutex is
acquirable by a second thread while a lease is inside the push (no lock
across decode/sink); release slot reuse; reset_all clears
shape/recv/seq/assembler; reconnect starts fresh; sink-failure perf
push-failure accounting; disable keeps shape but nulls decoders;
gate-independent valid-recv counting; start_clear re-bases seq and clears
pending Mode A halves.

Suite result: **29 PASS / 0 FAIL** (`west build -t run`,
native_sim/native/64).  The pure decode (43), modea (15), iso_seq (18),
stats (10), perf (19), volume (12), and lifecycle (28) suites all still
pass untouched.

## BSim

`tests/bsim/CMakeLists.txt` compiles `src/audio_stream_session.c`.
Observer calls moved with their code without reordering.  Stage 1 (all 16
scenarios, 25 runs) passed strict checking with **byte-identical pins**:
mono 10 ms full `0x22AB5C0D`; modea/modeb 10 ms `0xBAE24F7E`;
7.5 ms `0x01A3EB05` / `0x2D95D15C` / `0xFF82CADB`; invalid-SDU resume
`0x0C61918D`; one-CIS-loss `0x30D6BAF0`; reconnect = fresh mono oracle
`0x22AB5C0D`; all per-scenario L/R hashes and push counts unchanged.

## Coverage migration (population 29 → 30)

Deliberate baseline migration on clean `3c7396a` via
`scripts/test-coverage.sh --write-baseline /tmp/r6-baseline-candidate.json`
(gcovr 8.4 / gcov (GCC) 14.3.0, recorded in the baseline), inspected, and
committed as `tests/coverage-baseline.json` in `67d2a18`.  Provenance:
receive lines moved from excluded integration `bt_bap.c` into included
direct `audio_stream_session.c`, so no old aggregate comparison against
the excluded file is possible (documented in `docs/testing/coverage-matrix.md`
"R6 baseline migration").  New file: **274/292 lines, 131/192 branches,
25/25 functions** (the 4 CONFIG_ZTEST lease accessors are GCOVR-excluded).
Every other file remains at or above its committed record — verified
programmatically across lines/branches/functions with **zero ratio
decreases**; `stream_lifecycle.c` stays 62/62 L, 34/36 B, 7/7 F (the
removed chan_count storage was unmeasured dead scalar space).  Aggregate:
lines 3777/4164 (90.7%), branches 1611/2237 (72.0%), functions 246/246
(zero-hit enforcement clean).  No exclusions added, no thresholds
weakened.  The session suite originally compiled production
`audio_volume.c` without VCP, creating a second zero-hit
`audio_volume_init` gcovr record; the fake-volume seam (sanctioned by the
handoff) removed that compilation so `audio_volume.c`'s record is
untouched (`3c7396a`).

## G1 results (clean `67d2a18`)

- `./scripts/test-all.sh` — **49 PASS / 0 FAIL / 49 TOTAL** (29 twister +
  5 exec-only + 12 Python + coverage + matrix + BSim), exit 0, elapsed
  17 m 01 s, log `/tmp/r6-gate1.log`.  Coverage child: numeric population
  **30**, baseline enforcement **0 error(s), PASS**; matrix child:
  **0 error(s), 0 note(s)**; BSim Stage 1 **PASS — all scenarios
  strict-checked** with the unchanged pins above.
- `fw-build-5340`, `fw-build-54l15`, `fw-build-dongle` — all exit 0, zero
  new/actionable warnings (the only `src/` warning found during the
  focused pass — an unused `cnt` when `CONFIG_INFO_REPORTING_INTERVAL=0` —
  was fixed before the gate).  CMake diagnostics remain the documented
  pre-existing NCS set.
- `python3 scripts/check-build-contract.py --nrf5340 build/nrf5340
  --nrf54l15 build/nrf54l15` — **76 assertions, 0 failed, BUILD CONTRACT
  PASSED**.
- `git diff --check` clean; worktree clean at gate time.

## G3 hardware (both targets, 2026-08-05)

Planned non-destructive flash only; no mass erase / recovery.  Evidence:
`/tmp/r6-hw/` (`MANIFEST.md` + `SHA256SUMS`; raw logs for boots, all six
rows, offload counters, APLL status, and the pre-R6 A/B comparison).
Central per the AGENTS central-only rule: hci0 over `/dev/ttyACM2` @
1 000 000 baud H4, `btmgmt info` verified **addr C0:AA:BB:CC:DD:EE**,
**current settings: powered le secure-conn cis-central**.  Probes
(nrf-probes): **XIAO CMSIS-DAP serial 8EE9B3FF, DPIDR 0x6ba02477, PART
0x00054b15, VARIANT AAC0**; **Pico CMSIS-DAP serial E6635C08CB1F502B,
DPIDR 0x6ba02477, PART 0x00005340, VARIANT QKAA** — identical to the
pre-refactor baseline identities.  Receiver identities from boot logs:
nRF54L15 `DB:A6:0C:05:A2:AA` (random), nRF5340/E83 `E8:54:F0:E0:D9:42`
(random).  Fresh rows used `bt unpair` (receiver) + `bluetoothctl
remove` / central bond-dir removal; bonded rows preserved the bond
(`--preserve-bond`).  No audibility claim.

| Target | Row | Central | Receiver result | Offload / warnings |
|--------|-----|---------|-----------------|--------------------|
| nRF54L15 | Mode A fresh 120 s | 12000 frames @ 100.0 fps | Stream[0] SDUs=9760 decoded=24060 plc=4540; Stream[1] SDUs=9774; **decode_err=0 i2s_underrun=0 stream_reset=0** | submit=12030 success=12030 **fallback=0** busy=0; faults timeout/full/stale/seq/frame/crc/payload=0; ASRC state=0 verify=0 |
| nRF54L15 | Mode B fresh 120 s | 12000 frames @ 100.0 fps | Stream[0] SDUs=10569 decoded=24062 plc=2924; **decode_err=0 i2s_underrun=0 stream_reset=0** | submit=12031 success=12031 **fallback=0**; all faults 0 |
| nRF54L15 | Mode A bonded reconnect 120 s | 12000 frames @ 100.0 fps (`--preserve-bond`, Pair() skipped) | Stream[0] SDUs=9956 decoded=24044 plc=4133; Stream[1] SDUs=9971; **decode_err=0 i2s_underrun=0 stream_reset=0** | submit=12022 success=12022 **fallback=0**; all faults 0 |
| nRF5340/E83 | Mode A fresh 120 s | 12000 frames @ 100.0 fps | Stream[0] SDUs=11322 decoded=22644 plc=0; Stream[1] SDUs=11338; **decode_err=0 i2s_underrun=0 stream_reset=0** | zero `ISO seq gap` / `seq discontinuity` / `i2s_nrfx: Next buffers` / `Cannot write` lines |
| nRF5340/E83 | Mode B fresh 120 s | 12000 frames @ 100.0 fps | Stream[0] SDUs=11660 decoded=23320 plc=0; **all zeros** | zero warning lines |
| nRF5340/E83 | Mode B bonded reconnect 120 s | 12000 frames @ 100.0 fps (`--preserve-bond`) | Stream[0] SDUs=11656 decoded=23312 plc=0; **all zeros** | zero warning lines |
| nRF5340/E83 | APLL evidence (60 s bonded Mode B, mid-stream `audio status`) | 6000 frames @ 100.0 fps | Frames decoded 4142, I2S underruns 0, Stream resets 0, **Drift state ACTIVE, Drift ppm −500**, Resampler identity | matches T8 baseline APLL evidence |

nRF54L15 plc (2924–4540) reflects the session RF environment, inside the
T8 accepted range 0–7110; decode_err/i2s_underrun/stream_reset zero on
every row.  E83 counts match the T8 baseline almost exactly (Mode A
11322/11338 vs 11320/11336; Mode B 11660/11656 vs 11659/11658).

### Diagnostics note (environment, not R6)

An early E83 Mode A attempt showed CIS 0 with zero SDUs plus continuous
"Mode A: half dropped (overflow=N)" (a session-visible symptom of one
channel never delivering), and an early E83 Mode B attempt showed
stream_reset=93–100 with ~24% SDU loss.  The identical symptoms
reproduced on the **pre-R6 (R5) firmware** in the same session
(stream_reset=93, SDUs=9132), and the pre-R6 Mode A connect itself
failed at the ISO Acquire stage — proving the cause was transient
dongle/controller state (a stale receiver bond left over from the
pre-R6 experiment, BlueZ reconnect/pairing churn after the dongle's
connection context was exhausted by an undropped Xiao ACL, plus degraded
RF at that moment), not an R6 regression.  After clearing the stale
bonds on both sides and re-running, every E83 row is clean and matches
the baseline.  Raw A/B logs preserved in `/tmp/r6-hw/prefw-*.log`.

## Deviations / blockers

None blocking.  Process notes:

- The canonical gate's `cnt` unused-variable warning (INFO_REPORTING
  INTERVAL 0 in production) was fixed with `(void)cnt` in the adapter
  before the gate run.
- read_acm.py requires pyserial, which is not on the default python3
  PATH; the hardware session injected the nix pyserial site-packages via
  PYTHONPATH (and the console command helper used the same path).
- The dongle keeps an ACL for a previously connected peer (SW Split has
  one connection context): connecting to a second receiver requires
  disconnecting/removing the first (0x0d Limited-Resources symptom).
  Documented in the evidence manifest, not a firmware defect.
- The fresh rows each required the sanctioned stale-bond reset
  (`bt unpair` + central `bluetoothctl remove`/bond-dir removal) — the
  documented R4/R5 workflow, not a firmware defect.

## Non-scope items untouched

R7–R10; mixed-duration support; stack-ownership changes; codec/protocol
changes; 360-frame FLPR offload; BSim repin; destructive hardware
recovery.
