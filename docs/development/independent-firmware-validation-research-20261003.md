# Independent firmware validation: research record, 2026-10-03

## Scope and owner direction

The owner requested a documented investigation followed by backlog items for
the proposed validation steps. This record concerns independently calculated
or otherwise validated reference data, not another replay of previously
observed good DUT output. It does not implement firmware, run vendor tools,
approve licenses, change frozen acceptance limits or claim qualification.

Repository inspection checkpoint: `8778266b4efcd38170ba2bcb2cbd6bbd1cb2bdc3`.
PR #15 was human-merged at `0c9d2391f8532684e5014225b82749f0d930ce6e`.
Active SDK remains NCS v3.4.1. Earlier research under the external library is
dated technical context, not present acceptance evidence.

**LC3-only boundary:** the owner's licensing/compatibility caution is binding.
LC3plus is a separate codec/specification family. Its code, vectors, licenses,
modes and conformance results must not be presumed interchangeable with
Bluetooth LC3. The earlier exploratory suggestion of ETSI LC3plus as a native
secondary oracle is **not a recommended implementation path for this track**.
No LC3plus adoption, source reuse or derived-vector generation is authorized
by these backlog items. Any future consideration requires a separate explicit
owner decision plus demonstrated compatibility and rights review.

The owner has ideas for handling Win32 reference files. Windows, Wine or other
execution arrangements remain open for refinement; this document does not
choose or install one.

## Configuration and external library inspected

`opencode.json` already advertises described references:

| Alias | Declared path |
| --- | --- |
| `le-audio-resources` | `~/Nextcloud/Development-Resources/le-audio/Bluetooth` |
| `ncs` | `~/ncs/v3.4.1` |
| `nix-nrf-dev` | `../nix-nrf-dev` |

No config edit or restart was necessary. The Bluetooth library's `README.md`
indexes specification/characterization extracts, conformance tooling, TCRL
worksheets, `ASSESSMENT.md`, `AUDIO-TESTING-RESEARCH.md` and offload research.
The supplied official software package is v1.0.8, dated 2024-07-01. Its script
README identifies conformance driver v0.6.3; its reference-binary README
identifies fixed-point encoder/decoder v1.6.1B. These identities are distinct.

The authored read-only `verify_references.py` passed: four source hashes,
347 output hashes, 262 PDF pages, 329 worksheet cell counts and byte-identical
extracted vendor text. This proves extraction/provenance integrity only.
Equations, diagrams, workbook applicability and legal notices still require
original-source review.

Vendor tools/materials remain external and read-only. No codec executable,
conformance driver, auto-download, installer or hardware action ran during
this investigation. No third-party material was copied into the repository.

## Current repository evidence and independence limits

| Boundary | Current evidence | Limit |
| --- | --- | --- |
| Fixture origin | `tests/fixtures/lc3/gen_fixtures.c` creates deterministic PCM, encodes it with liblc3 and decodes with liblc3 to create PCM anchors. Continuous corpus holds codec state over 128 frames. | Generated data, not RF captures, but encoder and expected decoder share one implementation lineage. |
| Codec provenance | `tests/fixtures/lc3/portable-oracle-manifest.json` preserves historical NCS v3.3.0 generation and liblc3 revision `48bbd3eacd36e99a57317a0a4867002e0b09e183`. Active v3.4.1 still uses that codec revision. | Hashes establish identity/reproducibility, not independent semantic correctness. Host Linux encoder resolution is less tightly pinned. |
| Decoder/session regression | `tests/unit/decode`, `tests/unit/audio_stream_session` and BSim exercise real production decode/routing/state with known frames. | Expected PCM is largely same-library-derived. These are valuable regressions, not independent LC3 conformance. |
| Stateful loss | `stateful-reference-manifest.json` binds exact histories; BSim now uses target-native startup recipes. | PLC advances history but its waveform is not generally compared numerically. Old measured startup recipes are not current universal truth. |
| ASRC | `tests/unit/asrc/src/test_asrc.c:240-358` compares production ASRC instances for a purported independent reference; chunking case checks counts. | Determinism/count checks do not independently establish sample arithmetic or full concatenated waveform correctness. |
| Offload/sink | Offload comparison uses the same ASRC; I2S ownership tests use mocked processing and limited write snapshots. | Does not independently prove complete post-ASRC output or closed-loop rate stability. |
| Physical HIL | Frozen transport/runtime matrix checks aggregate delivery, PLC, errors and fault recovery. | Not per-SDU content identity, deadline headroom or independent presentation-phase proof. |
| Passive I2S captures | Prior analyzer work verifies wiring/clock/frame geometry with negative controls. | Geometry is not exact PCM content, freshness, analog function or calibrated fidelity. |

The portability limits 2048 maximum sample error, RMS 512 and Q15 correlation
at least 32750 belong to the existing project regression contract. They are
not official LC3 conformance thresholds and must not be silently relabeled or
loosened when adding a different reference implementation.

**Fixed vectors are not inherently bad.** Independently validated, reproducible,
versioned vectors are legitimate benchmarks. Observed DUT output is not enough
to define correctness. Keep useful same-library regressions and historical
captures; add independent oracles and require fresh evidence for new physical
acceptance verdicts.

## Fresh web findings and source quality

1. [Bluetooth SIG LC3 1.0.1 page](https://www.bluetooth.com/specifications/specs/low-complexity-communication-codec-1-0-1/)
   advertises Test Software v1.0.10 as well as older v1.0.9/v1.0.8 releases.
   Local v1.0.8 and base v1.0 PDF are not a verified current tool/specification
   set. Package contents, exact binary identity and platform support of v1.0.10
   were not inspected or executed. Align actual TS/ICS/TCRL and errata with
   selected software rather than assuming an adoption-page link is the latest
   applicable procedure.
2. [SIG LC3 1.0 page](https://www.bluetooth.com/specifications/specs/low-complexity-communication-codec-1-0/)
   identifies mandatory errata for compliance to base 1.0. A base PDF alone
   cannot establish current normative behavior.
3. [Google liblc3 README](https://github.com/google/liblc3/blob/main/README.md)
   describes its Python implementation and Appendix C intermediate-value tests.
   These are useful algorithm regression inputs but remain within the same
   project lineage. [Published conformance reports](https://github.com/google/liblc3/blob/main/conformance/README.md)
   reference SIG software v1.0.7 and ETSI material; neither the reports nor
   upstream qualification qualify this firmware wrapper/build/product.
4. [ETSI TS 103 634 V1.7.1 directory](https://www.etsi.org/deliver/etsi_ts/103600_103699/103634/01.07.01_60/)
   provides specification and source archive for **LC3plus**. Availability of
   reference source is not permission to use it, nor evidence that its modes,
   license or outputs are appropriate Bluetooth LC3 oracles. Excluded as above.
5. [EBU SQAM page](https://tech.ebu.ch/publications/sqamcd) and
   [EBU QC material record](https://qc.ebu.io/testmaterials/523/) identify usage
   terms including R&D restrictions. Direct retrieval of the SQAM landing page
   returned 403; search metadata is not a complete legal review. Do not vendor
   copyrighted audio or derived reference artifacts on that evidence alone.

The local v1.0.8 package READMEs expressly refer to the
[Bluetooth SIG LC3 EULA](https://btprodspecificationrefs.blob.core.windows.net/eula-lc3/Bluetooth-SIG-LC3-EULA.pdf).
Apache-2.0 licensing of production liblc3 does not grant rights to SIG tools,
SQAM audio or reference-derived output. No legal approval is asserted here.

## Recommended LC3-only reference architecture

```text
Authored deterministic PCM
          |
          v
Independent Bluetooth LC3 reference encoder
          |
          +-- framed reference file --> strict adapter --> raw LC3 frames
          |                                                   |
          v                                                   v
Independent reference decoder                         production decoder
          |                                                   |
          +----------- aligned unscaled PCM comparison -------+
```

Use an independent reference-decoded signal, not original PCM equality: LC3 is
lossy. Preserve codec configuration, continuous state, decoder history, frame
ordering, sample counts, delay and terminal padding. Reference containers carry
headers and length fields; they are not raw BAP SDUs. Mono duplication and
distinct stereo routing need separate checks from numerical codec comparison.

First operating points: current 48 kHz, 10 ms/120 bytes and 7.5 ms/90 bytes per
channel. Other claimed operating points require applicability review. This is
not permission to add 155-byte frames, change ISO MTU or expand sample rates.
Do not inject arbitrary nonce/header bytes into a valid LC3 payload. Bind fresh
session/stimulus identity through a refined valid-data recipe and evidence
protocol, not an assumed incompatible frame format.

A licensed reference-data generator may produce sealed reproducible vectors
for fast CI, with a separately provisioned regeneration/conformance lane.
The DUT must never generate its own expected PCM or silently rebaseline it.
Authored signals reduce audio-source licensing risk but do not automatically
settle reference-tool/output redistribution rights. New per-run challenges
and stable independently validated vectors serve different purposes.

PLC comparisons require care: concealment algorithms need not share one exact
waveform. Check public output geometry, channel isolation, bounded behavior,
stateful loss/recovery and applicable normative criteria without assuming
bit-identical reference PLC or objective proof of listening quality.

## Independent boundaries and proposed work packages

| Step | Product outcome | Smallest meaningful verification |
| --- | --- | --- |
| S1 | Approved LC3-only reference tooling, version set and rights/execution contract | Reproducible reference-tool execution evidence in the selected isolated environment, with immutable identities; no assumed Wine/Windows choice. |
| S2 | Independent corpus generation and strict container-to-SDU adapter | Rebuild authored-seed streams identically; reject invalid geometry/headers/lengths/provenance and preserve continuous frame history. |
| S3 | Real production decoder compared with independent PCM | Known reference frames through `audio_decode_sdu()` yield correct counts/routing and applicable aligned metrics; deliberate wrong history/channel/content fails. |
| S4 | Independent ASRC arithmetic and waveform oracle | Real public processing output compared sample-by-sample with a separately expressed high-precision/rational global-coordinate model, including capacity/retry/reset boundaries. |
| S5 | Closed-loop rate/queue validation | Real drift/ASRC and clock-driven output queue remain within refined bounds; the same nonzero-skew case fails with correction disabled. |
| S6 | Fresh exact encoded transport proof | Intended valid SDU bytes match per-stream receive evidence; omission, duplication, reorder, corruption and stale-session evidence fail. |
| S7 | Fresh independent post-ASRC digital I2S content proof | Newly identified capture matches expected word sequence/count/channel mapping; geometry-only, swapped, flat, repeated and stale traces cannot pass. |
| S8 | Delivery-headroom and presentation-phase measurements | Timestamp ledger and physical word positions prove chosen deadlines/phase bounds with explicit clock-mapping uncertainty and deliberate late/offset controls. |
| S9 | Full applicable independent LC3 conformance execution | Approved official procedures cover declared decoder points and produce retained complete results; partial/skipped/wrong-version runs cannot masquerade as conformance. |

For ASRC, silence/DC, bounded ramps, fractional signed cases, boundary impulses,
distinct L/R, irregular chunking and ppm changes permit independently calculated
truth. Compare every concatenated sample and count, not two DUT instances or
final occupancy alone. Linear interpolation arithmetic and spectral quality
are separate: another SRC filter is not a bit-exact linear-ASRC oracle.

Rate mismatch, arrival jitter, missing events and presentation phase must not
be conflated. At 48 kHz, 100 ppm means 4.8 frames/second of imbalance; a startup
reservoir can hide a bad loop in a short run. Virtual clocks and finite queues
need production tuning and explicit supported envelopes. ISO timestamps alone
do not expose the original source ADC/USB clock. No direct SDC-owned RADIO
access is proposed.

## Existing backlog owners and non-duplication

- PB-041 already owns fresh DUT/pin/analyzer challenge and immutable harness
  identity. S7/S8 must reuse that boundary, not create another identity item.
- PB-023/PB-025 own mono/stereo analog fixture qualification; PB-024/PB-026
  own associated output matrices. Reuse them for the analog lane. Digital I2S
  has no DAC ACK and cannot prove DAC presence/function or analog quality.
- PB-013 owns 360-frame FLPR offload. S4/S5 may test current CPUAPP 360-frame
  behavior and existing 480-frame offload, not implement the separate feature.
- PB-007/PB-009 retain exact-candidate release/publication ownership. New codec
  or DSP checks do not complete FR4 or public release.

All new work remains Backlog for refinement. Win32 handling, selected legal
tool version, corpus redistribution/storage, numerical/timing envelopes and
formal applicability are genuine open choices, not invented acceptance values.
The research is technical context; backlog alone owns status/dependencies.

## Backlog mapping

Created after this research record, through Backlog.md. Each item is a separate
requested product outcome, not an implementation subtask. All are P2 Backlog
items pending deeper refinement; the files, not this table, own current status.
No existing product item was rewritten or duplicated.

| Step | Item | True prerequisites |
| --- | --- | --- |
| S1 | [PB-042: LC3-only reference tooling and rights](../product/backlog/tasks/pb-042%20-%20Establish-LC3-only-independent-reference-tooling-and-rights.md) | None |
| S2 | [PB-043: Independent vectors and frame adapter](../product/backlog/tasks/pb-043%20-%20Generate-independent-LC3-reference-vectors-and-strict-frame-adapter.md) | PB-042 |
| S3 | [PB-044: Production decoder independent comparison](../product/backlog/tasks/pb-044%20-%20Validate-production-LC3-decode-against-independent-reference-PCM.md) | PB-043 |
| S4 | [PB-045: Independent ASRC oracle](../product/backlog/completed/pb-045%20-%20Add-independent-ASRC-arithmetic-and-full-waveform-oracle.md) | None |
| S5 | [PB-046: Closed-loop clock recovery](../product/backlog/completed/pb-046%20-%20Validate-closed-loop-clock-recovery-with-independent-timed-output.md) | PB-045 |
| S6 | [PB-047: Fresh exact SDU content/delivery](../product/backlog/tasks/pb-047%20-%20Prove-fresh-exact-LC3-SDU-content-and-per-stream-delivery.md) | PB-043 |
| S7 | [PB-048: Independent fresh I2S content](../product/backlog/tasks/pb-048%20-%20Compare-fresh-I2S-content-against-independent-post-ASRC-expectations.md) | PB-041, PB-044, PB-045, PB-047 |
| S8 | [PB-049: Headroom and presentation phase](../product/backlog/tasks/pb-049%20-%20Measure-audio-delivery-headroom-and-I2S-presentation-phase.md) | PB-048, PB-046 |
| S9 | [PB-050: Full applicable LC3 decoder conformance](../product/backlog/tasks/pb-050%20-%20Run-full-applicable-independent-Bluetooth-LC3-decoder-conformance.md) | PB-044 |

PB-042 is the first codec-track refinement, including the owner's Win32 ideas.
PB-045 can be refined independently while tooling/rights choices are resolved.
Analog work continues under PB-023/PB-025 and PB-024/PB-026; it is not a tenth
new item or implicitly completed by digital validation.
