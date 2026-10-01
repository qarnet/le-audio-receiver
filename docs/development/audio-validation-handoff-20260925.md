# Audio-domain testing and timestamp observability: implementation handoff

Date: 2026-09-25. Design/research handoff requested by user; no firmware/test
implementation or hardware acceptance performed here. Active SDK: NCS v3.4.1.
Inspected repository implementation at `90522390ef4d8cbd560f23e7c8b46f08611563de`.

## Start here

1. Read `AGENTS.md` and `nrf54l15-observability-resume-20260925.md`. Preserve
   existing PB-013 changes and immutable evidence. Do not resume unrelated paused
   HCI repairs merely because analyzer software is installed.
2. Use [central analyzer setup](../testing/logic-analyzer-setup.md) for physical
   mapping, ground/power/mute checks, identity workflow and capture templates.
   It is the sole current wiring declaration; do not duplicate its table here.
3. Follow Backlog.md readiness/ownership rules. This handoff does not mark new
   audio-testing work Ready, rewrite existing frozen limits, or grant acceptance
   to older images. Refine the relevant scope before implementation.

Related work, not automatic dependencies or scope transfers: PB-013 (360-frame
FLPR support), PB-014 (emergency fallback), PB-023/PB-025 (analog fixtures),
PB-033 (session identity), PB-035/PB-036 (HIL matrices/artifacts), PB-040 (SDK).
Fixture/channel identification is a separate backlog deliverable, not PCM testing.
Its item is [PB-041](../product/backlog/tasks/pb-041%20-%20Identify-lab-DUT-and-verify-logic-analyzer-wiring-per-session.md).

Background research and independent LC3 procedures live locally at:

- `/home/thomas-workstation/Nextcloud/Development-Resources/le-audio/Bluetooth/AUDIO-TESTING-RESEARCH.md`
- Same directory: `ASSESSMENT.md` and `FLPR-OFFLOAD-RESEARCH.md`.

Earlier reports are inspection snapshots, some using NCS v3.3.0. Reverify APIs,
timing units and build parameters against v3.4.1. This handoff consolidates all
proposed test domains; it is not a replacement for official codec procedures.

## Decision: independent test verdicts, small final integration suite

| Domain | Primary proof boundary | Exclude from first test |
| --- | --- | --- |
| Encoded-data correctness | Exact received SDUs, per-stream event identities | PCM, analog capture |
| Codec correctness | Known LC3 through real decoder to unscaled PCM | RF, volume, ASRC |
| ASRC/DSP correctness | Synthetic PCM through public resampler API | RF, codec, live clocks |
| Rate matching/control | Real controller + real ASRC + independently clocked queue | RF and physical capture |
| Delivery deadlines | Local reference/arrival/ready timing with event identity | Whole-waveform comparison |
| Physical presentation | Captured I2S samples and reference marker | Analog quality initially |
| Final integration/quality | Representative complete source-to-output paths | Exhaustive component matrix over RF |

Jitter, long-term rate mismatch, presentation phase, loss/PLC and CPU deadline
failures are separate disturbances. ASRC cannot restore missing packets. Finite
buffer hides rate mismatch temporarily; uninterrupted LRCK alone does not prove
correct content, rate matching, or negotiated presentation latency.

## 1. Shared event ledger and metric definitions

Implement bounded nonblocking trace records, not per-frame formatted UART logging.
Use fixed memory budget, explicit overflow/drop counts and schema version. Drain
outside measured path where practical. No extra logging on HCI H4 transport.
Keep instrumented/uninstrumented image hashes and compare perturbation.

Record raw values with domains, units and identities:

| Fields | Meaning |
| --- | --- |
| Run/session, device, firmware hash, boot/stream generation | Reject stale or cross-image joins |
| Logical media event, CIS/ASE/channel ID, app counter, ISO sequence | Link source intent, received SDUs and L/R assembly; unwrap per generation |
| Raw ISO reference, timestamp-valid flag, receiver GRTC | Distinguish synchronization reference from callback arrival |
| Source intended event and actual submit entry/return | Detect source preparation/submission lateness, not air success |
| RX entry, decode start/end, sink entry, queue acceptance | Attribute receiver preparation cost and headroom |
| ASRC input/output frame counts, correction ppm, execution mode | Track rate matching; distinguish CPU, FLPR and fallback |
| Queue frames and descriptor ownership, DMA progress when available | Backpressure, starvation and actual consumption |
| PLC/repeat/drop/failure/recovery events | Preserve media timeline and distinguish intentional concealment |
| Mapping version/age/uncertainty, capture marker ID | Correlate domains without replacing raw clocks |

Separate primary media event from resampled block index: output counts vary and
one concealment action may produce multiple blocks. Define which sample represents
event onset, including codec algorithmic delay, before comparing to wire markers.

Receiver-local metrics, all expressed in one validated controller/GRTC domain:

```text
target(i)       = wrap_safe(SDU_reference(i) + negotiated_PD)
arrival_slack   = target(i) - callback_entry(i)
ready_slack     = target(i) - audio_ready(i)
output_error   = observed_presentation(i) - target(i)
queue_time     = queued_remaining_output_frames / actual_output_rate
```

`audio_ready` must identify a real boundary. Software-ready, accepted by I2S,
DMA-active and first sample on wire are not interchangeable. Retain missing/invalid
timestamps as unavailable evidence, not zero or fabricated deadline success.
Expand 32-bit timestamps to correct nearby epoch even when reference is in past.
Reset mapping on stream/device restart. Test 32-bit microsecond wrap (~71.6 min).

Report sample count, extrema, p50/p95/p99 where useful, histogram resolution,
deadline misses, longest gaps, queue/latency slope, PLC and emergency repeats.
Observed maximum is not proven WCET. Use per-CIS gaps, not global receive gap that
can hide one silent stereo stream. Do not sum nested elapsed-time intervals as CPU
load. Convert clock ticks using actual image timebase, not assumed 128 MHz.

Current seams: `src/audio_perf.c`, `src/audio_shell.c`, `src/bt_bap.c:974-1119`,
`src/audio_stream_session.c`, `src/audio_i2s.c:430-595` and `src/flpr_shell.c`.
Existing `audio perf` default deadline is fixed 10,000 us (`Kconfig:95-102`);
new timing gate must use explicitly justified budget rather than silently applying
that value to 7.5-ms streams. Existing HIL limits remain unchanged.

**Smallest verification:** feed encoded trace records through public export/parser
boundary and prove joins, unit conversion, missing data, overflow, wrap, reconnect
and out-of-order export cannot yield false PASS. Hardware marker/capture later
checks timestamp placement independently.

## 2. BLE timestamp synchronization: useful, but map clocks rather than retime them

### First choice: sink-local ISO references

No new BLE synchronization protocol is needed to calculate sink deadline from
valid RX ISO reference plus PD. Reference already represents ISO synchronization
timing in receiving controller's local clock domain. Record callback/ready times
against same GRTC. Raw numerical timestamps from two devices are not automatically
equal, despite shared Bluetooth event schedule.

Do not subtract source TX timestamp from sink RX timestamp without mapping **and
matching reference semantics**. In Nordic ISO sample, source central's reference
marker adds central transport latency as well as PD; receiver uses RX reference
plus PD. This is reference-definition handling, not an extra arbitrary delay.

Verified v3.4.1 sources (SDK root `/home/thomas-workstation/ncs/v3.4.1`):

- `nrf/samples/bluetooth/iso_time_sync/README.rst:117-120,287-292`.
- `nrf/samples/bluetooth/iso_time_sync/src/iso_rx.c:60-83`.
- `nrf/samples/bluetooth/iso_time_sync/src/iso_tx.c:175-182,306-324`.
- `zephyr/include/zephyr/bluetooth/iso.h:342-358`: timestamp-valid flag.

### Optional cross-device correlation: matched ACL anchor reports

Use Nordic `conn_time_sync` pattern. Both controllers timestamp corresponding
ACL connection event anchor. Carry captured anchor records over GATT/control
channel, or collect both independently for offline fitting. Message arrival time
is **not** the time synchronization observation.

Fit matched same-connection, unwrapped same-event pairs:

```text
sink_us ≈ sink_origin_us + a * (source_us - source_origin_us)
relative_skew_ppm = (a - 1) * 1,000,000
```

Keep origins centered to avoid numerical precision loss. Store raw pairs,
coefficients, fitting interval, residuals, age and uncertainty. Mark stale estimates
unusable after missing anchors, reset/reconnect, ambiguous wrap or unsupported
connection-parameter change. No arbitrary extrapolation across those boundaries.

Verified integration points:

| API/config | Contract |
| --- | --- |
| `CONFIG_BT_CTLR_SDC_CONN_ANCHOR_POINT_REPORT` | Enables SDC ACL anchor-report capability; not enabled in inspected receiver/source/HCI build artifacts |
| `CONFIG_BT_HCI_VS_EVT_USER` | Zephyr-host user vendor-event callback support |
| `hci_vs_sdc_conn_anchor_point_update_event_report_enable()` | Enable reporting through host wrapper |
| `bt_hci_register_vnd_evt_cb()` | Receive and dispatch vendor reports without stealing other handlers |
| `sdc_hci_subevent_vs_conn_anchor_point_update_report_t` | Connection handle, 16-bit event counter, 64-bit local controller anchor timestamp |

References: `nrf/subsys/bluetooth/controller/Kconfig:369-378`;
`nrf/include/bluetooth/hci_vs_sdc.h:395-405`;
`zephyr/include/zephyr/bluetooth/hci.h:181-201`;
`nrfxlib/softdevice_controller/include/sdc_hci_vs.h:309-334,1729-1744`.

Central reports occur each connection interval; peripheral reports require a
received packet. Reports can be overwritten if not consumed promptly, and HCI
Reset disables reporting. Check actual command status and image capabilities.
Bound diagnostic traffic so instrumentation does not starve ISO work.

Do not copy demo arithmetic unchanged. `conn_time_sync/src/peripheral.c:112-125`
uses event-count extrapolation, not a full fitted skew estimator; inspect counter
wrap, integer-width and interval-change handling. Sample's illustrative 4-us
accuracy budget is **not** a guarantee for this fixture. Include age × residual
skew, timestamp/marker quantization and measurement uncertainty.

**Avoid naive GATT ping/round-trip divided by two.** Directional scheduling and
retransmission asymmetry make offset uncertain. GATT is suitable for transporting
historical hardware-anchor timestamps, not defining those timestamps from arrival.

### Do not hide drift under test

Clock map is analysis metadata only. Do not reset/discipline GRTC, force source and
sink oscillators equal, or replace raw PCLK/queue evidence with corrected time.
Controller-to-controller mapping does not directly measure source ADC/USB sample
clock or sink I2S/PCLK. Rate-matching test still needs independent clocks.

Standalone nRF54 source is first target: it already reads GRTC in
`hil/source/app/src/hil_source_controller_time_nrf54.c:15-33`.
Linux HCI path has **three domains**: Linux monotonic, source controller GRTC,
sink controller GRTC. Controller mapping does not calibrate Linux host time.
Vendor event access and command ownership through BlueZ/HCI require separate
verification; Zephyr wrappers are not Linux APIs. Do not use HCI UART arrival time
as microsecond-accurate controller time.

### Independent validation of mapping

Capture source/sink hardware-timed GPIO markers on one analyzer and compare
observed offsets with map predictions and declared uncertainty. Reserve verified
spare pins/channels; current CH3 remains power observation unless setup declaration
is deliberately changed. Name markers as submission, reception or presentation;
none automatically means actual RF transmission.

Receiver owns TIMER20, CAPTURE[0] and allocated GRTC/GPPI links in
`src/audio_timing_nrf54.c:428-477`. Do not reuse/reset them. Offline anchor mapping
itself needs no new timer. Additional hardware markers require separate allocated
resources. Never access SDC/MPSL-owned RADIO. Sample nRF54 GRTC absolute-set API
has deprecation/current locking differences; verify chosen API at implementation.

**Smallest verification:** synthetic encoded anchor streams with known offset/skew,
wrap, missing reports and resets produce bounded mapping errors or explicit
unavailable status; then real common-analyzer markers validate mapping within
declared bounds. Do not claim cross-device accuracy from self-consistent logs alone.

## 3. Content-only codec tests

Use known reference-encoded LC3 stream through real `audio_decode_sdu()` to unscaled
PCM. Preserve continuous decoder history. Compare against independently decoded
reference using official alignment, RMS/MAD procedure; do not compare lossy output
byte-for-byte with original PCM or transfer current portability thresholds into
formal conformance.

Start supported 48-kHz 10/7.5-ms configurations, mono then distinct L/R Mode A/B.
Cover supported byte budgets, malformed/empty input, loss/recovery, channel routing,
reconnect and configuration rejection without poisoning next valid stream.
Check frame/sample counts separately from numerical signal differences.

Local conformance package is not turnkey native encoder source or ready corpus.
Reference container needs parsing; file headers/frame-length fields are not ISO
SDU bytes. Tool execution/install/download and redistribution rights need separate
approval. Keep tooling provisioning outside normal test run. Reference PLC need
not match another valid PLC algorithm sample-for-sample.

Existing fixtures remain useful. `tests/fixtures/lc3/`, `tests/unit/decode/`,
`tests/unit/audio_stream_session/`, and BSim already cover much regression behavior.
Add independence rather than replacing them. For the HIL source's encoder, use
reference decoder/encoder-quality procedure separately, not encoded-byte equality.
Pin host encoder identity and validate PCM input size before native calls.

**Smallest verification:** reference LC3 produces correctly routed, counted PCM
within applicable decoder tolerance; altered payload/channel/history causes failure.
Volume/mute tests stay separate at their public sample-processing boundary.

## 4. ASRC arithmetic and quality, no clocks or radio

Run production `audio_asrc_process()` against independent global-coordinate,
high-precision interpolation oracle. Specify rounding and rational/Q32 step-error
bounds. Never call DUT twice and name that independent reference.

Tests: DC/silence; bounded ramps; signed fractional/tie cases; impulses crossing
blocks; distinct channels; irregular partitions; ratio/ppm changes; invalid input,
capacity failure and subsequent valid retry; reset; CPU/FLPR handover continuity.
Compare complete output and produced/consumed counts. Dynamic ratio changes must
occur at same global media positions when comparing chunk partitions.

Current `test_global_continuous_ref` compares same algorithm twice;
`test_chunking_invariance_480_vs_irregular` compares counts only:
`tests/unit/asrc/src/test_asrc.c:240-358`. Keep determinism coverage, add genuine
oracle and waveform partition checks. Existing FLPR equivalence is implementation
parity, not independent correctness; verify every emitted block across handover,
not only final internal state.

Separate DSP quality characterization: low/mid/high-frequency tones, sweeps,
transients, band-limited noise and stereo independence at real production ratio.
Measure gain/bandwidth, distortion, aliasing/images and ratio-modulation artifacts.
Current linear interpolator is not band-limited SRC; use analytic/high-quality
reference as quality baseline, not strict byte oracle. Do not invent universal
quality threshold; agree limits from product goal and measured characterization.

**Smallest verification:** independent oracle catches wrong fractional position,
rounding and lost cross-block history; quality metrics independently expose poor
high-frequency behavior even when arithmetic is correct.

## 5. Closed-loop rate matching with virtual clocks

Highest-value first new suite. Link real `audio_drift.c` and `audio_asrc.c` with
event-driven independent source and output clocks. No radio or LC3. Model finite
queue with actual block lengths; feedback uses actual slab-free semantics while
acceptance also measures queued frames/time.

Keep nominal 48,000 -> 47,619 conversion separate from drift correction. Use
production 2000-ppm output / 150-ppm integral tuning as one tested configuration,
not only existing unit suite's 500/500. Model PCLK-derived TIMER and I2S together;
source/controller skew and arrival jitter are independent knobs.

Required matrix:

- Both signs of fixed skew; zero skew; measured PCLK baseline plus residual skew.
- Steps, ramps/wander, jitter/bursts at unchanged average rate.
- Delayed/missing/noisy feedforward, saturation and return to supported envelope.
- Both 480- and 360-input blocks; per-block controller cadence matters.
- Loss/replacement duration and complete callback silence; reconnect/reset/wrap.
- Capacity pressure and processing/offload-delay models.

±50/100/200 ppm residual cases and clamp-adjacent cases are candidate research
stimuli, not promised supported range. Feedforward consumes output headroom and
phase integral is limited; establish support envelope explicitly.

Acceptance: bounded queue and latency, correct frame accounting, settling behavior,
no healthy-case underrun/push failure or emergency repeated blocks. Fault cases
need explicit bounded recovery/reporting rather than impossible endless concealment.
PLC behavior itself requires real session layer; do not mock PLC and claim it proven.

**Negative control:** keep nominal resampling, disable correction. Injected skew
must show predicted queue slope and fail same healthy-case bound. Choose virtual
duration exceeding headroom / (sample_rate × abs(residual_ppm) × 1e-6), not arbitrary
short smoke. 100 ppm at 48 kHz means 4.8 frames/second. Repeatable seeds/event logs
support replay. Model time advances independently of successful sends or DUT output.

Then add sibling suite through real `audio_sink_push()` with real drift/ASRC and
clock-driven fake I2S. Track complete FIFO descriptors, active/next buffer ownership,
timed releases, starvation/error state and complete output. Existing fake's manually
released buffers and sixteen-byte snapshots are insufficient. Retain focused
ownership/lifecycle tests; do not replace them with huge integration test.
Connect existing timing HAL shadows to real timing backend after queue plant works.

**Smallest verification:** same skew scenario passes correction enabled and fails
disabled; then same public sink calls expose any modeled DMA starvation or repeated
content. Native/BSim execution is not embedded WCET proof; inject processing delays
and validate assumptions on hardware.

## 6. Transport and deadlines independently of decoded waveform

Incremental first step: observer before decode in `stream_recv()` records exact
SDU identity/bytes, per-CIS sequence, flags and times for prerecorded valid LC3.
BSim already uses prerecorded frames (`tests/bsim/client/src/bsim_tx.c:296-338`).
Extend physical source with fixture replay where useful; preserve continuity and
sequence identity at corpus loop boundaries. No realtime encode needed to prove
transport, but retain loaded encode/decode integration rows.

Optional stronger isolation: dedicated raw ISO/diagnostic receiver with opaque
SDUs containing session/stream ID, logical sequence and deterministic payload.
Never pass opaque data to normal LC3 decoder. Match production CIS count, size,
interval, PHY, encryption, framing and negotiated QoS. Raw ISO does not prove ASCS.

Validate exact expected bytes and count all intended events, including missing first
or last events; separate source preparation misses from submitted-but-undelivered
events. Track duplicates, reorder, stale epochs, valid-empty, flags, one-CIS silence
and burst-loss lengths. HCI sequence and application event counter are different.

Source evidence: intended event, ready/submit times, lead margin, send errors.
Sink evidence: reference, callback/ready headroom, per-CIS gap/loss and output link.
Source `sent` callback and vendor TX timestamp are completion/scheduling metadata,
not peer-delivery or air-time counters (`zephyr/include/zephyr/bluetooth/iso.h:755-765`;
`nrfxlib/softdevice_controller/include/sdc_hci_vs.h:1482-1500`).

Optional tools, not new mandatory runtime dependencies:

- `zephyr/samples/bluetooth/iso_connected_benchmark`: synthetic traffic baseline;
  review before reuse, stock callback counts are not full sequence/deadline oracle.
- `zephyr/tests/bsim/bluetooth/ll/cis`: synthetic sequence/fault patterns.
- HCI LE Read ISO Link Quality: PDU retransmission/CRC/flush counters aid diagnosis,
  not one-to-one SDU delivery proof. Retain raw command status and support evidence.
- HCI LE ISO Transmit/Receive Test: controller-generated RF isolation only; separate
  test-mode image/data-path restrictions and `CONFIG_BT_CTLR_SDC_ISO_TEST` support.

**Smallest verification:** distinct peers exchange exact identified SDUs; deliberate
gap, corruption, stale replay and lateness yield separate correct verdicts, including
missing callback detection. Counter/parser synthetic tests complement, not replace,
physical peer traffic. Keep frozen HIL 90% valid/5% PLC gate unchanged; new timing
budgets require their own documented basis and verdict.

## 7. Physical digital output and end-to-end checks

After fixture identity is verified, capture I2S at DAC connector. First direct
synthetic PCM through production sink, then prerecorded LC3 through RF and real
decode/volume/ASRC/FLPR path. Use distinct channels and unique band-limited markers,
not identical periodic tones with ambiguous alignment.

Prove captured words equal intended **post-ASRC** output stream, including packing
and channel order. Correlate reference marker with appropriate media sample for
presentation error. Check startup, bounded latency drift, repeated/missing blocks,
CPU/FLPR fallback and recovery. Clock continuity alone cannot detect repeated PCM.

Current `i2s_write_gap` is software write timing, not DMA completion or presentation.
Potential lower seam: `zephyr/drivers/i2s/i2s_nrfx.c:157-247`, but released-buffer
events still need sample-time interpretation and independent capture. I2S FRAMESTART
is buffer-boundary event here, not every LRCK edge. Do not use it as sample counter.

At 12 MS/s, typical eight-channel raw capture is about 12 MB/s before compression;
use short targeted high-rate windows plus long-run firmware summaries/markers.
For longer timing-only captures, lower sampling rate only after defining minimum
pulse widths/resolution; do not undersample BCLK and claim decoded PCM validity.
Analyzer timebase accuracy must be characterized for absolute ppm claims. Check
capture loss and compare instrumented/uninstrumented runs.

Analog lane remains separate: existing `scripts/hil/capture_analyzer.py` and
PB-023/PB-025 cover analog fixture qualification. Sound-card capture adds another
sample clock; characterize it. Measure offset/rate error before aligning/resampling
for signal comparison, or analysis can erase timing defect. Compare against
correct reference at output sample times, not raw original-PCM equality.

Keep small full-stack matrix: mono/Mode A/Mode B, supported durations/byte budgets,
volume/mute, startup, loss/PLC, one-CIS failure, reconnect, offload/fallback and
long-run stability. Audio subjective quality is not established solely by numerical
PLC scores. 7.5-ms CPU behavior does not imply 360-frame FLPR acceptance (PB-013).

**Smallest verification:** known signal survives actual I2S path with correct
channel/count/content and bounded measured timing; altered/duplicated block or
forced starvation is detected. Repeat under production load. No analog/RF or
release acceptance claims from host simulator alone.

## 8. Implementation order and completion evidence

Recommended sequence, not replacement backlog statuses:

1. Define trace schema and independent ASRC oracle; add closed-loop virtual clocks.
2. Integrate timed fake I2S and real sink; connect timing backend and lifecycle faults.
3. Add per-CIS transport observer and sink-local deadline metrics.
4. Qualify analyzer harness under separate identification item; capture physical I2S.
5. Add ACL anchor mapping only if cross-device diagnosis needs it; validate with
   common-analyzer markers. This is not prerequisite for sink-local deadline tests.
6. Add independent LC3 vectors when tooling/rights resolved; run representative
   loaded integration and qualified analog cases.

Before each implementation phase, state observable behavior and smallest failing
regression at public boundary. Retain negative controls, invalid/cancel/restart
cases and real boundary traffic. Do not accept tests that only assert helper calls,
private fields or copied constants.

Final evidence bundle: exact source/build/config/image/tool identities; run/session
and harness binding; stimulus seed/fixture hashes; raw traces/captures; clock domains
and mapping uncertainty; all intended event counts; independent verdict per domain;
test commands/results and known exclusions. Do not relax frozen limits, reclassify
unsupported measurements as zero, or overwrite prior evidence to get PASS.

## Official references

- [Nordic ISO time-sync source docs, pinned NCS revision](https://github.com/nrfconnect/sdk-nrf/blob/b20f8619ba9a5530f8c34b0a130d829947cfe55d/samples/bluetooth/iso_time_sync/README.rst)
- [Nordic ACL connection-time-sync source docs, pinned revision](https://github.com/nrfconnect/sdk-nrf/blob/b20f8619ba9a5530f8c34b0a130d829947cfe55d/samples/bluetooth/conn_time_sync/README.rst)
- [SDC anchor-report contracts, pinned nrfxlib revision](https://github.com/nrfconnect/sdk-nrfxlib/blob/bb715183a57ac501907b63df2079c3fbcd067641/softdevice_controller/include/sdc_hci_vs.h#L309-L334)
- [libsamplerate quality measurement](https://libsndfile.github.io/libsamplerate/quality.html)
- [PulseView capture/decoder manual](https://sigrok.org/doc/pulseview/unstable/manual.html)

These sources support methods and API semantics, not invented product timing or
quality thresholds. Local v3.4.1 source remains authority for installed behavior.
