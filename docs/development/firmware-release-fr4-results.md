# FR4 results — exact-artifact hardware acceptance

Status: **BLOCKED**. FR4 is not accepted. The exact draft candidate
`v0.1.0` failed mandatory nRF5340 mono hardware acceptance and remains
private, unpublished, and intentionally untagged. The local replacement
preflight passed both receiver targets but is replacement-candidate
preflight only, not FR4 exact-artifact acceptance. FR5 remains blocked.

Date: 2026-08-10.  Handoff
`docs/development/firmware-release-fr4-results-handoff.md`; procedure
`docs/development/firmware-release-fr4-procedure.md` (historical, executed
2026-08-10); plan `docs/development/firmware-release-plan.md`.

## Evidence split

Two distinct evidence classes must not be conflated:

- **Immutable draft evidence**: the exact draft release `367572702`
  (`v0.1.0`, target `3d9a9186ec288484a637dac1dc7460319daf5e84`) and its
  four assets, as downloaded and flashed from the draft. This candidate
  **failed** FR4.
- **Local build evidence**: pristine builds from committed local HEAD
  `5e7f502` (`fix: increase nRF54L15 pairing workqueue stack`). These
  images **passed** a six-row local hardware matrix on both targets. They
  are not the draft's bytes and carry no FR4 acceptance.

No raw log or binary is copied into this repository. Retained run
directories under `/tmp/opencode/` hold all raw evidence (see Retained
evidence).

## Exact `v0.1.0` candidate failure

Canonical retained evidence: `/tmp/opencode/fr4-v0.1.0-OEp9Kh/MANIFEST.md`.

The exact draft candidate passed every identity, checksum, ZIP, internal
checksum, provenance, notes-body, and untagged-ref validation, and its
nRF5340 Mode A diagnostic passed on exact draft bytes. After the central
mono selection fix `9f456b1`, strict mono established exactly one CIS and
sent 12000 frames over 120 seconds, but the receiver reported:

```text
SDUs=8876 decoded=8876 plc=0 decode_err=0
i2s_underrun=0 stream_reset=225 empty_sdu=0
```

The receiver emitted 225 each of:

- `i2s_nrfx: Next buffers not supplied on time`;
- `i2s_nrfx: Cannot write in state: 4`;
- `audio_i2s: I2S underrun, restarting DMA`.

This deterministic mandatory failure stopped the matrix before nRF54L15.
Exact `v0.1.0` therefore failed FR4. Nothing may describe it as awaiting a
first run or as accepted. It remains private, unpublished, and intentionally
untagged (authenticated git-ref lookup returns HTTP 404 as expected).

## Defect and fix progression

Retained manifests under `/tmp/opencode/`:

1. `fr4-cadence-local-b6jNTm/MANIFEST.md`: cadence concealment activated
   and DMA resets removed, but four cadence RESYNC warnings exposed an
   overly tight fixed timestamp tolerance.
2. `fr4-cadence-local-bcrVW1/MANIFEST.md`: scaled tolerance was present,
   but nRF5340 faulted with `ZEPHYR FATAL ERROR 2: Stack overflow on CPU
   0`, current thread `sysworkq`.
3. `fr4-cadence-local-stSZAJ/MANIFEST.md`: six stream rows passed after
   increasing nRF5340 sysworkq to 2048, but review measured nRF54L15
   `g_pairing_wq` at 1012/1024 (98 percent, 12 bytes unused). Review
   correctly blocked final acceptance of the local preflight.
4. `fr4-cadence-local-yc4N3U/MANIFEST.md`: final rerun at `5e7f502` after
   increasing `g_pairing_wq` to 1536. All six rows passed on first attempt.

Implementation sequence:

```text
9f456b1 fix: reserve mono ASE during BlueZ selection
60e2ac1 fix: conceal timestamp-detected ISO omissions
606fbed fix: complete ISO cadence verification
0f3ad2c fix: scale ISO cadence timestamp tolerance
7d4fc71 test: complete ISO cadence diagnostic coverage
ec8c846 fix: increase nRF5340 system workqueue stack
5e7f502 fix: increase nRF54L15 pairing workqueue stack
```

## Final local hardware matrix

Canonical retained evidence: `/tmp/opencode/fr4-cadence-local-yc4N3U/MANIFEST.md`.
Both pristine builds passed with only the documented NCS v3.3.0 diagnostic
set. Both targets flashed and verified normally. No recovery, mass erase,
probe-rs, settings erase, release write, tag, push, or CI action occurred.

Final image identities from committed local HEAD `5e7f502`:

| Image | Bytes | SHA-256 |
|---|---:|---|
| nRF5340 `merged.hex` | 1037672 | `ab8abda54987d2cd0cb664ca58ee95a42907d0713e562f95f1e4fb7463a990fd` |
| nRF5340 `merged_CPUNET.hex` | 403684 | `2ce0ca1aa9fdc27d9fc1a4b25148af82da2fb52f1834f61113685d0851119619` |
| nRF54L15 cpuapp | 1500940 | `85253a4b69a89c6bc8d7073a0d0c8ccbc50ba559ea6c6eefe5cbf5f3839cbbc0` |
| nRF54L15 FLPR | 91857 | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Final row results:

| Target | Mode | Central | Receiver summary | Result |
|---|---|---|---|---|
| nRF5340 | mono | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=8862 decoded=12007 plc=3145` | PASS |
| nRF5340 | Mode B | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=9162 decoded=24034 plc=5710` | PASS |
| nRF5340 | Mode A | 3000 frames / 30 s / 100 fps, two CISes | stream 0 `SDUs=2789 decoded=5578 plc=1`; stream 1 `SDUs=2805` | PASS |
| nRF54L15 | mono | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=10153 decoded=12040 plc=1887` | PASS |
| nRF54L15 | Mode B | 12000 frames / 120 s / 100 fps, one CIS | `SDUs=10319 decoded=24080 plc=3442` | PASS |
| nRF54L15 | Mode A | 3000 frames / 30 s / 100 fps, two CISes | stream 0 `SDUs=2195 decoded=6088 plc=1698`; stream 1 `SDUs=2208` | PASS |

Every final row had `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`,
zero cadence RESYNC warnings, and zero unexplained warnings. nRF54L15 FLPR
stayed ACTIVE/Ready/ACKed/Healthy, submit equaled success, fallback was
zero, and all fault/recovery counters were zero. Cadence activation was
observed on both targets. Physical audibility was not observed and was not
required for this local diagnostic preflight.

Stack gates:

| Target | Workqueue | Usage | Criterion |
|---|---|---|---|
| nRF5340 | sysworkq | 844/2048 used, 41 percent, 1204 bytes unused | PASS |
| nRF54L15 | `g_pairing_wq` | 1012/1536 used, 65 percent, 524 bytes unused, identified by runtime thread-object address matching ELF symbol `g_pairing_wq` | PASS |

## Software gates at this code state

All recorded at exact starting HEAD `5e7f502` before this documentation
commit:

- Canonical gate: **65 PASS / 0 FAIL / 65 TOTAL** (35 twister + 5
  exec-only + 22 Python + coverage + matrix + BSim Stage 1).
- Focused suites: `audio.iso_seq` 39/39, `audio_stream_session` 47/47,
  build-contract checker tests 52/52.
- Build contract: **96 assertions, 0 failed** (`BUILD CONTRACT PASSED`).
- BSim Stage 1 pins remain byte-identical.
- Fresh report-only coverage run (`./scripts/test-coverage.sh --report-only
  --output /tmp/opencode/fr4-final-coverage --clean-output`): numeric lines
  4777/5234 (91.3 percent), branches 2091/2896 (72.2 percent), functions
  363/363 (100.0 percent), population files 36, exit 0. The committed
  coverage baseline remains unchanged.

## Retained evidence

Raw logs, manifests, checksums, downloaded draft assets, and extracted
images live outside the repository under `/tmp/opencode/`, retained with
per-directory `MANIFEST.md` and `SHA256SUMS`:

- `/tmp/opencode/fr4-v0.1.0-OEp9Kh/` — exact draft candidate execution
  (failed at nRF5340 mono).
- `/tmp/opencode/fr4-cadence-local-b6jNTm/`,
  `/tmp/opencode/fr4-cadence-local-bcrVW1/`,
  `/tmp/opencode/fr4-cadence-local-stSZAJ/`,
  `/tmp/opencode/fr4-cadence-local-yc4N3U/` — local fix progression and
  final local matrix.
- `/tmp/opencode/fr4-final-coverage/` — fresh report-only coverage run at
  `5e7f502`.

No raw log or binary is copied into this repository.

## Safety boundary

The failed exact draft `v0.1.0` is untouched: not deleted, not replaced,
not published, not tagged. No tag, push, PR, merge, hosted CI, flashing,
reset, recovery, or serial work was performed by this documentation commit.
No release or version state changed. Nothing was published.

## Next step

A replacement candidate must be created through the accepted trusted-main
lifecycle: write the new version into the root `VERSION` file, and let the
accepted CI path build, package, and create a new immutable draft from
trusted `main`. Then that new draft's exact assets must rerun the full
FR4 procedure on both targets before FR5 can publish anything.
No replacement version has been selected. FR5 remains blocked.
