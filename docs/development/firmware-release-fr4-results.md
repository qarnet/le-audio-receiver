# FR4 results — exact-artifact hardware acceptance

Status: **BLOCKED**. FR4 is not accepted. The exact draft candidate
`v0.1.0` failed mandatory nRF5340 mono hardware acceptance and remains
private, unpublished, and intentionally untagged. The local replacement
preflight passed both receiver targets but is replacement-candidate
preflight only, not FR4 exact-artifact acceptance. FR5 remains blocked.

Date: 2026-08-10. Historical handoff at Git revision `a94f010`:
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

## Historical replacement candidate and later failures

The closeout above is dated 2026-08-10. A separate replacement draft was later
created on 2026-08-11: release `368351363`, tag label `v0.1.0`, target
`5966d68f8155a5a96da96fc7859f1064a3591472`, trusted-main run `31459113243`
attempt 1. Its historical jobs `93678896322`, `93687178841` and `93688175449`
passed tests, firmware and release in that order, with 65/0/65 tests, NCS
v3.3.0 `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956` and toolchain container
`sha256:f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276`.
It is neither original failed draft `367572702` nor PB-006's later nRF54L15-only
candidate `391991202`. Same version label does not mean identical bytes.

| Asset ID | Name | Bytes | SHA-256 |
|---|---|---:|---|
| 509723232 | `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 580613 | `1bb2d2143624d9ab3c64bb1a94d6bb2ce7ea216aa167121f9d12d31629b2e83f` |
| 509723234 | `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 623434 | `cc91f07b26b7ac2a15a996793bcc65497f065b6aef2c7c24269b511d8c182ae3` |
| 509723235 | `release-provenance.json` | 1265 | `c12ccfc7049fc4949c52a1367f66654b2c939564bd64841ccd0f4ae78e200d41` |
| 509723233 | `SHA256SUMS` | 232 | `c0f23bfb6e18f3ee2a5f9258a3ee738ccb772fdf6b98bd555b1a379e2a63d608` |

| Extracted image | SHA-256 |
|---|---|
| nRF5340 CPUAPP | `8239f20629711b12080bbb1333f410e339bf0dd39d925884f194f58a7424e61c` |
| nRF5340 CPUNET | `2ce0ca1aa9fdc27d9fc1a4b25148af82da2fb52f1834f61113685d0851119619` |
| nRF54L15 CPUAPP | `8e4bd57dfb18cff600ca97a25e956d0ea9ca6b808ccf393b56edf973b5bfbdd6` |
| nRF54L15 FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

CPUAPP embedded commit/source-path strings changed layout and relocations from
local preflight `5e7f502`; its bytes were not expected to match. CPUNET and FLPR
happened to match. Acceptance required immutable asset hashes, regenerated
provenance/notes, source lineage and hardware behavior, not local rebuild equality.

Historical execution record `/tmp/opencode/fr4-replacement-v0.1.0-94WQMu`
reported identity/provenance/flash/boot gates and nRF5340 rows 1-4 passed. This
was partial execution, not full FR4 acceptance or nRF54L15 acceptance. Row 5
first failed twice at the host profile-exposure check:
`found_device=True`, `found_sink=True`, `found_profile=False`. Plain
`wpctl status` showed friendly descriptions; `wpctl status -n` showed
address-bearing node names. The repair retained exact address matching and
independent `pw-dump` card/sink checks, with no friendly-name fallback.
Evidence filenames were `logs/7p5-gate-blocker.txt` and
`logs/nrf5340-7p5-gate.log`.

Later `logs/row5-new-blocker.txt`, `logs/nrf5340-7p5-r3-gate.log` and
`logs/nrf5340-7p5-r3-receiver.log` recorded another gate defect. PCM submission
finished about 5.5 s before Disable and the receiver summary. Immediate parsing
reported `Summary log seen: False`. Three runs also reproduced a real I2S
deadline error before Disable. A bounded monotonic 15 s summary wait exposed
late evidence; it did not excuse the I2S error. Timeout/read errors cannot
become success, and the strict parser still determines the verdict.

These facts come from historical documents at Git revision `a94f010`, not a
fresh raw-log rehash. External paths are local historical provenance; availability
and the intermediate draft's later deletion date are unverified. The original
failed draft was deleted 2026-09-19, as recorded in `firmware-release-plan.md`.
PB-007 owns exact acceptance of the later candidate. Nothing here proves it.

## Repair rationale and negative controls

The first mono failure and Mode A diagnostic used matched unframed 2M QoS:
10,000 us interval, 120-byte SDU, RTN 2, 10 ms latency and 40,000 us presentation
delay. A selection/configuration race let BlueZ select both FL and FR before
either configuration; rejecting only the second configuration left a partial
two-CIS group. The fix reserves capacity during selection, consumes the matching
reservation only after successful configuration, and never clears it for an
unrelated transport. Direct configuration and Release cleanup remain supported.

Timestamp concealment uses `MAX(seq_omitted, cadence_omitted)` to avoid counting
the same missing position twice. Delivered no-TS and LOST callbacks consume their
own event positions; valid/LOST/valid at 10,000/20,000/30,000 us requires only the
LOST frame's PLC. Mode A timestamp synthesis stayed outside that fix. RESYNC
adds no cadence omissions; independent sequence-gap PLC can still occur. Gap
logging stays DEBUG to avoid real-time UART load.
Historical source grounding concerned NCS v3.3.0, not a new SDK assertion.

The scaled grid tolerance combines 32 us capture/tick quantization with a
ceiling-rounded 1000 ppm combined endpoint budget over the event span, capped at
one quarter interval for unambiguous grid choice. The original fixed 10 us limit
was superseded. Boundary witnesses include 42/43 us, 122/123 us at 10 ms and
100/101 us at 7.5 ms, plus long-noTS cap and tiny-interval cases. Computable
off-grid diagnostics precede delivered-position rejection, preserving the winning
reason without synthesis. The post-fix 64/1/65 gate failure came from four missing
public cadence outcome entries; later matrix repair restored 65/0/65 without
changing the coverage baseline or relabeling timestamp-only gaps as sequence loss.

The historical nRF5340 sysworkq overflow occurred during valid Config/QoS before
Enable/ISO. Its 1024-byte stack exhausted despite timing sensitivity; doubling
to 2048 cost 1 KiB against measured 145296 B of 448 KiB. It did not justify moving
unidentified work to a new queue. nRF54L15 pairing work included reset/bonding,
feedback and security/advertising. Its `g_pairing_wq` was identified at
`0x2000ebb8`, with 1012/1024 B used. Historical RAM 161084/163840 B left 2756 B;
1536 instead of 2048 preserved more headroom while meeting measured margins.
The local acceptance required at least 256 B unused, at most 80 percent stack
usage, readable symbol/table evidence and at least 2 KiB post-build RAM headroom.
These are dated repair criteria, not fresh build-budget claims.

If RF delivered every event, cadence activation had to be disclosed as unobserved,
not fabricated. One evidenced environmental retry retained both attempts; repeated
deterministic firmware failures were not acceptance. Real pipe-fd tests used owned
`os.pipe()` descriptors, never literal fd 5. Historical host USB isolation had
separate approval, recorded state and scoped restoration; it grants no present
host-adapter mutation authority and is not the current session-bound XIAO workflow.
