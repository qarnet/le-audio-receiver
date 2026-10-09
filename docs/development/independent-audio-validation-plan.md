# Independent audio validation plan: open proposal

Status: open research and test-design proposal. This is a proposal document,
not an implementation record and not acceptance evidence. Backlog items
PB-041, PB-042, PB-043, PB-044, PB-047, PB-048, PB-049, and PB-050 remain
the sole status owners; this plan changes no backlog readiness, status, or
dependency. Active SDK: NCS v3.4.1. The configured project references
provide the external Bluetooth reference library; older NCS v3.3.0 research
snapshots are history, and the v3.4.1 pin wins where they disagree.

## Related software checkpoints (links, not restatements)

Two of the plan's originally proposed domains have software-model
checkpoints. Their result documents own the details; do not re-derive scope
from this plan:

- PB-045, independent ASRC arithmetic and full-waveform oracle:
  `pb-045-independent-asrc-results-20261003.md` (rational global-coordinate
  oracle, signed-ppm rounding repair, caller-continuity integration).
- PB-046, closed-loop clock recovery with independent timed output:
  `pb-046-clock-loop-results-20261003.md` and
  `tests/unit/clock_loop/README.md` (real production
  controller/conversion/sink/offload/processor against an independently
  clocked finite FIFO with a live-word oracle).

Both are CPU software-model proofs. Physical FLPR/IPC, RF, analog,
presentation, full codec conformance, and release acceptance remain separate
domains; nothing in those results transfers to them.

## Open domains and their proof boundaries

The proof-domain table below is the core of this plan. Each row is a separate
verdict; a pass in one row proves nothing in another. Jitter, long-term rate
mismatch, presentation phase, loss/PLC, and CPU deadline failures are separate
disturbances. ASRC cannot restore missing packets. A finite buffer hides rate
mismatch temporarily; uninterrupted LRCK alone proves neither correct content,
rate matching, nor negotiated presentation latency. Transport never restores
a missed SDU.

| Domain | Primary proof boundary | Exclude from the domain's verdict |
| --- | --- | --- |
| Encoded-data correctness | Exact received SDUs, per-stream event identities | PCM, analog capture |
| Codec correctness | Known LC3 through the real decoder to unscaled PCM | RF, volume, ASRC |
| ASRC/DSP arithmetic | Synthetic PCM through the public resampler API | RF, codec, live clocks |
| Rate matching/control | Real controller + real ASRC + independently clocked finite queue | RF and physical capture |
| Delivery deadlines | Local reference/arrival/ready timing with event identity | Whole-waveform comparison |
| Physical presentation | Captured I2S samples and reference marker | Analog quality initially |
| Final integration/quality | Representative complete source-to-output paths | Exhaustive component matrix over RF |
| DSP spectral quality (separately open) | Tones, sweeps, transients, noise at production ratio | Arithmetic-oracle claims |

DSP spectral quality remains an OPEN research domain separate from the
PB-045 arithmetic oracle and the PB-046 clock-model proof. Product numeric
envelopes for it (and for any timing budget) require grounded agreement from
product goals and measured characterization; do not invent limits.

## 1. Shared event ledger and metric definitions (open)

Implement bounded nonblocking trace records, not per-frame formatted UART
logging: fixed memory budget, explicit overflow/drop counts, schema version,
drain outside the measured path where practical. No extra logging on the HCI
H4 transport. Keep instrumented/uninstrumented image hashes and compare
perturbation.

Record raw values with domains, units, and identities:

| Fields | Meaning |
| --- | --- |
| Run/session, device, firmware hash, boot/stream generation | Reject stale or cross-image joins |
| Logical media event, CIS/ASE/channel ID, app counter, ISO sequence | Link source intent, received SDUs, and L/R assembly; unwrap per generation |
| Raw ISO reference, timestamp-valid flag, receiver GRTC | Distinguish synchronization reference from callback arrival |
| Source intended event and actual submit entry/return | Detect source preparation/submission lateness, not air success |
| RX entry, decode start/end, sink entry, queue acceptance | Attribute receiver preparation cost and headroom |
| ASRC input/output frame counts, correction ppm, execution mode | Track rate matching; distinguish CPU, FLPR, and fallback |
| Queue frames and descriptor ownership, DMA progress when available | Backpressure, starvation, and actual consumption |
| PLC/repeat/drop/failure/recovery events | Preserve the media timeline; distinguish intentional concealment |
| Mapping version/age/uncertainty, capture marker ID | Correlate domains without replacing raw clocks |

Fixed-budget ledger states: budget, overflow counter, raw clock domains and
units, boot/stream generation IDs, CIS/ASE IDs, raw timestamp flags, source
intended-send, RX decode-ready, queue actual remaining frames, ppm, mode, and
PLC state; the frame-count versus mode mapping is explicit, and any
uncertainty is recorded, never averaged away. Separate the primary media
event from the resampled block index: output counts vary, and one concealment
action may produce multiple blocks. Define which sample represents event
onset, including codec algorithmic delay, before comparing to wire markers.
Software-ready, DMA-active, and wire-onset states are not interchangeable
evidence; label each boundary explicitly.

Receiver-local metrics, all in one validated controller/GRTC domain:

```text
target(i)       = wrap_safe(SDU_reference(i) + negotiated_PD)
arrival_slack   = target(i) - callback_entry(i)
ready_slack     = target(i) - audio_ready(i)
output_error    = observed_presentation(i) - target(i)
queue_time      = queued_remaining_output_frames / actual_output_rate
```

`audio_ready` must identify a real boundary; software-ready, accepted by I2S,
DMA-active, and first sample on wire are not interchangeable. Retain
missing/invalid timestamps as unavailable evidence, never zero or fabricated
deadline success. Expand 32-bit timestamps to the correct nearby epoch even
when a reference lies in the past; the 32-bit microsecond wrap is about 71.6 min,
with a half-range of about 2147.5 seconds for nearby-epoch selection. It needs a wrap-test
row. Reset the mapping on stream/device restart.

Report sample count, extrema, p50/p95/p99 where useful, histogram resolution,
deadline misses, longest gaps, queue/latency slope, PLC, and emergency
repeats. The observed maximum is not a proven WCET. Use per-CIS gaps, not a
global receive gap that hides one silent stereo stream. Do not sum nested
elapsed-time intervals as CPU load, and do not sum per-CIS gaps into a
global load. Convert clock ticks using the actual image timebase, never an
assumed 128 MHz.

Current seams: `src/audio_perf.c`, `src/audio_shell.c`, `src/bt_bap.c`,
`src/audio_stream_session.c`, `src/audio_i2s.c`, and `src/flpr_shell.c`.
The existing `audio perf` default deadline is the fixed 10,000 us Kconfig
value; any new timing gate must justify its own budget rather than silently
applying that value to 7.5 ms streams. Existing HIL limits are unchanged.

**Smallest verification:** feed encoded trace records through the public
export/parser boundary and prove joins, unit conversion, missing data,
overflow, wrap, reconnect, and out-of-order export cannot yield a false PASS.
Hardware marker/capture later checks timestamp placement independently.

## 2. BLE timestamp mapping: map clocks, never retime them (open)

### First choice: sink-local ISO references

No new BLE synchronization protocol is needed to compute the sink deadline
from a valid RX ISO reference plus negotiated PD. The reference already
represents ISO synchronization timing in the receiving controller's local
clock domain. Record callback/ready times against the same GRTC. Raw
numerical timestamps from two devices are never automatically comparable,
despite the shared Bluetooth event schedule.

Do not subtract a source TX timestamp from a sink RX timestamp without a
validated map **and matching reference semantics**. In the Nordic ISO sample
central CIS TX adds the actual reported `bt_iso_info.unicast.central.latency`
as well as PD; RX uses the valid receive reference plus PD. This is not the
configured maximum QoS latency. The sample uses configured LED presentation
delay rather than BAP-negotiated PD. This is a
reference-definition difference, not an arbitrary extra delay. Never compare
raw device clocks side by side.

Verified NCS v3.4.1 sources:

- `nrf/samples/bluetooth/iso_time_sync/README.rst` (reference semantics and
  latency accounting).
- `nrf/samples/bluetooth/iso_time_sync/src/iso_rx.c` and `iso_tx.c`
  (reference and timestamp handling).
- `zephyr/include/zephyr/bluetooth/iso.h`: `BT_ISO_FLAGS_TS` timestamp-valid
  flag; `struct bt_iso_recv_info.ts`.

### Optional cross-device correlation: matched ACL anchor reports

The Nordic `conn_time_sync` pattern timestamps the corresponding ACL
connection-event anchor on both controllers. Carry anchor records over
GATT/control, or collect them independently for offline fitting. Message
arrival time is **not** the time-synchronization observation: RTT-derived and
arrival-based estimates are not anchor pairs.

Fit matched same-connection, unwrapped, same-event pairs:

```text
sink_us ≈ sink_origin_us + a * (source_us - source_origin_us)
relative_skew_ppm = (a - 1) * 1,000,000
```

Center origins to avoid precision loss. Store raw pairs, coefficients,
fitting interval, residuals, age, and uncertainty. Proposed validation policy
rejects stale estimates beyond a defined age bound, reset/reconnect, ambiguous
counter/timestamp unwrap and parameter boundaries without a validated model.
Missing reports and resolved wraps remain explicit evidence conditions; their
effect on estimate validity needs a tested policy, not an invented SDC guarantee.
A map without enough matched anchors to compute its uncertainty is unavailable.

Verified NCS v3.4.1 integration points:

| API/config | Contract |
| --- | --- |
| `CONFIG_BT_CTLR_SDC_CONN_ANCHOR_POINT_REPORT` | SDC controller capability under BT_CONN, not runtime enablement; not enabled in retained receiver/source/HCI configs |
| `CONFIG_BT_HCI_VS_EVT_USER` | Zephyr-host user vendor-event callback support |
| `hci_vs_sdc_conn_anchor_point_update_event_report_enable()` | Host wrapper takes `sdc_hci_cmd_vs_conn_anchor_point_update_event_report_enable_t`; enable 1 or disable 0 for all ACL connections, other values RFU; returns 0 or negative error |
| `bt_hci_register_vnd_evt_cb()` | Registers one user vendor callback, replacing any previous user callback; true means handled, false defers to stack; multiple user consumers need shared dispatch |
| `sdc_hci_subevent_vs_conn_anchor_point_update_report_t` | Connection handle, 16-bit event counter, 64-bit local controller anchor timestamp |

References: `nrf/subsys/bluetooth/controller/Kconfig`;
`nrf/include/bluetooth/hci_vs_sdc.h`;
`zephyr/include/zephyr/bluetooth/hci.h`;
`nrfxlib/softdevice_controller/include/sdc_hci_vs.h`.

The host wrapper also requires `CONFIG_BT_LL_SOFTDEVICE_HEADERS_INCLUDE` for
compilation. The sample registers after `bt_enable()` and then sends enable 1.

Central reports occur each connection interval; peripheral reports require a
received packet. Reports can be overwritten if not consumed promptly; HCI
Reset disables reporting. Check the actual command status and image
capabilities. Bound diagnostic traffic so instrumentation never starves ISO
work.

Do not copy the demo arithmetic unchanged: it uses event-count extrapolation,
not a full fitted skew estimator; inspect counter wrap, integer width, and
interval-change handling. The sample's illustrative 4 us accuracy budget is
**not** a guarantee for this fixture. Include age-by-residual skew,
timestamp/marker quantization, and measurement uncertainty. Keep citations
to the SDK's relative paths pinned to the v3.4.1 tree, and reverify `age`,
`residual`, `uncertainty`, wrap, reset, and parameter-change handling at
implementation time: an anchor map missing any of those provisions is
invalid, not partially valid.

**Avoid naive GATT ping/round-trip divided by two.** Directional scheduling
and retransmission asymmetry make the offset uncertain. GATT transports
historical hardware-anchor timestamps; arrival never defines them.

### Do not hide drift under test

The clock map is analysis metadata only. Do not reset or discipline GRTC, do
not force source and sink oscillators equal, and do not replace raw
PCLK/queue evidence with corrected time. Controller-to-controller mapping
does not measure the source ADC/USB sample clock or the sink I2S/PCLK domain.
Rate-matching proofs still need independent clocks. ADC, USB, and Linux
monotonic clocks remain additional separate domains; do not claim
microsecond UART-arrival accuracy.

### Independent validation of the mapping

Capture source/sink hardware-timed GPIO markers on one analyzer and compare
observed offsets against map predictions and their declared uncertainty.
Reserve verified spare pins/channels; CH3 remains power observation unless
the wiring declaration is deliberately changed. Name markers as submission,
reception, or presentation; none automatically means actual RF transmission.

The receiver owns TIMER20, CAPTURE[0], and the allocated GRTC/GPPI links in
`src/audio_timing_nrf54.c`. Do not reset or reuse them. Offline anchor
mapping itself needs no new timer. Additional hardware markers need separately
allocated resources. Never access SDC/MPSL-owned RADIO. The sample's raw
GRTC absolute-set API has deprecation and current-locking differences;
verify the chosen API at implementation time. Where the original handoff
proposed API names, treat them as supported only after reverification against
the installed v3.4.1 headers; do not carry them forward as verified by this
plan.

**Smallest verification:** synthetic encoded anchor streams with known
offset/skew, wrap, missing reports, and resets produce bounded mapping errors
or an explicit unavailable status; then real common-analyzer markers validate
the mapping within declared bounds. Cross-device accuracy is never claimed
from self-consistent logs alone.

## 3. Content-only codec tests (open; tooling/rights prerequisite first)

Use a known reference-encoded LC3 stream through the real
`audio_decode_sdu()` to unscaled PCM. Preserve continuous decoder history.
Compare against an independently decoded reference using the official
alignment and RMS/MAD procedure; the project's portability thresholds are
not formal conformance, and lossy output is never compared byte-for-byte
with the original PCM. PLC output need not match another valid PLC algorithm
sample-for-sample.

Start with supported 48 kHz 10/7.5 ms configurations, mono then distinct L/R
Mode A/B. Cover supported byte budgets, malformed/empty input, loss/recovery,
channel routing, reconnect, and configuration rejection without poisoning the
next valid stream. Check frame/sample counts separately from numerical
signal differences.

Container discipline: reference container file headers and frame-length
fields are not ISO SDU bytes; parse officially and feed exact SDUs. The
local conformance package is not turnkey native encoder source or a ready
corpus. Tool execution/install/download and redistribution rights require a
separate, explicit approval; keep tooling provisioning outside the normal
test run (PB-042 owns the decision; LC3plus stays excluded).

Existing fixtures remain useful: `tests/fixtures/lc3/`,
`tests/unit/decode/`, `tests/unit/audio_stream_session/`, and BSim cover
regression behavior. Add independence rather than replacing them. For the
HIL source's encoder, judge by reference decoder/encoder-quality procedure,
not encoded-byte equality. Pin host encoder identity and validate PCM input
size before native calls.

**Smallest verification:** an independent reference LC3 decode produces
correctly routed, counted PCM within applicable decoder tolerance; altered
payload, channel, or history causes failure. Volume/mute tests stay separate
at their public sample-processing boundary.

## 4. ASRC: arithmetic (covered), DSP quality (open)

The independent arithmetic oracle and caller-continuity domain are covered
by PB-045 (see the checkpoint links above). What remains open is the
separate DSP quality
characterization: low/mid/high-frequency tones, sweeps, transients,
band-limited noise, and stereo independence at the real production ratio.
Measure gain/bandwidth, distortion, aliasing/images, and ratio-modulation
artifacts. The linear interpolator is not band-limited SRC; use an
analytic/high-quality reference as the quality baseline, not a strict byte
oracle, and never call the DUT twice and name that independent. Do not
invent a universal quality threshold: agree limits from the product goal and
measured characterization first.

**Smallest verification (open item):** independently measured quality
metrics expose poor high-frequency behavior even when the PB-045 arithmetic
oracle proves positional correctness; altered ratio behavior is bounded and
reported.

## 5. Closed-loop rate matching: clock model covered, deadline domain open

The closed-loop controller/conversion/sink/offload clock-model domain is
covered by PB-046 (see its result links). Still open, per the original
matrix:

- Deadline and headroom metrics (arrival/ready slack, output error,
  queue-time slope) against the section 1 ledger, on real hardware timing
  domains.
- The modeled PCLK/I2S pair on hardware (nRF54 GRTC/PCLK) beyond the
  software-model FIFO, with the production 2000 ppm output / 150 ppm
  integral tuning preserved; ±50/100/200 ppm residuals and clamp-adjacent
  cases are candidate research stimuli, not a promised supported range.
- The nominal 48,000 -> 47,619 conversion stays separate from drift
  correction in any new deadline evidence.
- The companion suite through real `audio_sink_push()` with a clock-driven
  fake I2S and complete FIFO descriptor/ownership/timed-release tracking
  (existing fake snapshot limitations were documented in the original
  proposal; PB-046's live-word oracle is the current baseline).
- Fault cases need explicit bounded recovery/reporting; an endless
  concealment requirement is invalid. PLC behavior itself needs the real
  session layer; never mock PLC and claim it proven.
- Virtual duration must exceed headroom / (sample_rate x abs(residual_ppm)
  x 1e-6), not an arbitrary short smoke; at 100 ppm and 48 kHz that is
  4.8 frames/second. Repeatable seeds/event logs support replay, and model
  time advances independently of successful sends or DUT output.

**Negative control (retained requirement):** keep nominal resampling,
disable correction; injected skew must show the predicted queue slope and
fail the same healthy-case bound.

**Smallest verification:** the same skew scenario passes with correction
enabled and fails with it disabled; the sink-side suite exposes any modeled
DMA starvation or repeated content; native/BSim execution is never embedded
WCET proof, so inject processing delays and validate assumptions on
hardware.

## 6. Transport and deadlines independently of decoded waveform (open)

Incremental first step: an observer before decode in the stream receive path
records exact SDU identity/bytes, per-CIS sequence, flags, and times for
prerecorded valid LC3. BSim already uses prerecorded frames
(`tests/bsim/client/src/bsim_tx.c`). Extend the physical source with fixture
replay where useful; preserve continuity and sequence identity at corpus loop
boundaries. Realtime encode is not needed to prove transport, but loaded
encode/decode integration rows stay.

Optional stronger isolation: a dedicated raw ISO/diagnostic receiver with
opaque SDUs carrying session/stream ID, logical sequence, and a deterministic
payload. Never pass opaque data to the normal LC3 decoder. Match production
CIS count, size, interval, PHY, encryption, framing, and negotiated QoS. Raw
ISO does not prove ASCS.

Validate exact expected bytes and count all intended events, including
missing first or last events; separate source-preparation misses from
submitted-but-undelivered events. Track duplicates, reorder, stale epochs,
valid-empty, flags, one-CIS silence, and burst-loss lengths. The HCI sequence
and the application event counter are different things.

Source evidence: intended event, ready/submit times, lead margin, send
errors. Sink evidence: reference, callback/ready headroom, per-CIS gap/loss,
and the output link. The source `sent` callback and vendor TX timestamp are
completion/scheduling metadata, not peer-delivery or air-time counters
(`zephyr/include/zephyr/bluetooth/iso.h` `sent` callback doc;
`nrfxlib/softdevice_controller/include/sdc_hci_vs.h` ISO TX Sync semantics).

Optional tools, not new mandatory runtime dependencies:

- `zephyr/samples/bluetooth/iso_connected_benchmark`: synthetic traffic
  baseline; review before reuse; stock callback counts are not a full
  sequence/deadline oracle.
- `zephyr/tests/bsim/bluetooth/ll/cis`: synthetic sequence/fault patterns.
- HCI LE Read ISO Link Quality: PDU retransmission/CRC/flush counters aid
  diagnosis, not one-to-one SDU delivery proof. Retain raw command status
  and support evidence.
- HCI LE ISO Transmit/Receive Test: controller-generated RF isolation only;
  separate test-mode image/data-path restrictions and
  `CONFIG_BT_CTLR_SDC_ISO_TEST` support.

**Smallest verification:** distinct peers exchange exact identified SDUs;
deliberate gap, corruption, stale replay, and lateness yield separate
correct verdicts, including missing-callback detection. Counter/parser
synthetic tests complement, never replace, physical peer traffic. The frozen
HIL 90% valid / 5% PLC gate is unchanged; new timing budgets need their own
documented basis and verdict.

## 7. Physical digital output and end-to-end checks (open; PB-041 prerequisite)

After fixture identity is verified, capture I2S at the DAC connector. First
direct synthetic PCM through the production sink, then prerecorded LC3
through RF and the real decode/volume/ASRC/FLPR path. Use distinct channels
and unique band-limited markers, never identical periodic tones with
ambiguous alignment.

Prove captured words equal the intended **post-ASRC** output stream,
including packing and channel order; channel mapping follows the decoder's
configured channel allocation after ASRC. Correlate a reference marker with
the appropriate media sample for presentation error. Check startup, bounded
latency drift, repeated/missing blocks, CPU/FLPR fallback and recovery.
Clock continuity alone cannot detect repeated PCM. Presentation-phase
measurement stays honest about clock uncertainty: the sound card / analyzer
clock domains must be characterized, and alignment cannot erase a real phase
error.

Current seam knowledge (retained): `i2s_write_gap` is software write timing,
not DMA completion or presentation. The nrfx I2S driver layer
(`zephyr/drivers/i2s/i2s_nrfx.c`) is a potential lower seam, but
released-buffer events still need sample-time interpretation and independent
capture. I2S FRAMESTART fires at buffer-boundary events (~100 Hz at 10 ms DMA
buffers), not at every LRCK edge; never use it as a sample counter. Analyzer
capture at 12 MS/s runs roughly 12 MB/s raw before compression for a typical
eight-channel setup: use short targeted high-rate windows plus long-run
firmware summaries/markers. Short 12 MS/s captures prove windows, not full
runs. Lower the sampling rate for longer timing-only captures only after
defining minimum pulse widths/resolution; never undersample BCLK and claim
decoded PCM validity. Characterize the analyzer timebase before absolute ppm
claims (uncalibrated analyzer ppm is not a product clock number); check
capture loss and compare instrumented/uninstrumented runs.

Analog lane stays separate (PB-023/PB-025 and the capture infrastructure own
it). A sound-card capture adds another sample clock: characterize it, measure
offset/rate error before any alignment/resampling, or the analysis can erase
a timing defect. Compare against the correct reference at output sample
times, not raw original-PCM equality. Rail activity alone does not ACK DAC
presence; PB-041's ADC/input-limit review governs presence evidence.

Keep the small full-stack matrix: mono/Mode A/Mode B, supported
durations/byte budgets, volume/mute, startup, loss/PLC, one-CIS failure,
reconnect, offload/fallback, and long-run stability. Audio subjective quality
is not established by numerical PLC scores. 7.5 ms CPU behavior never implies
360-frame FLPR acceptance (PB-013 owns that decision).

**Smallest verification:** a known signal survives the actual I2S path with
correct channel, count, content, and bounded measured timing; an
altered/duplicated block or forced starvation is detected. Repeat under
production load. No analog/RF or release acceptance claim from a host
simulator.

## 8. Proposed implementation order (recommendation, not backlog status)

1. Define the trace schema and per-domain ledger; extend the PB-045 oracle
   coverage to any new trace boundary.
2. Integrate timed fake I2S deadline evidence with the real sink; connect
   the timing backend and lifecycle faults.
3. Add the per-CIS transport observer and sink-local deadline metrics.
4. Qualify the analyzer harness under PB-041; capture physical I2S.
5. Add ACL anchor mapping only if cross-device diagnosis needs it; validate
   with common-analyzer markers. This is not a prerequisite for sink-local
   deadline tests.
6. Add independent LC3 vectors when PB-042/PB-043 tooling and rights
   resolve; run representative loaded integration and qualified analog
   cases.

Before each implementation phase, state the observable behavior and the
smallest failing regression at a public boundary. Retain negative controls,
invalid/cancel/restart cases, and real boundary traffic. Do not accept tests
that assert only helper calls, private fields, or copied constants.

Proposed architecture decision records for parts of this plan are indexed in
`docs/adr/README.md` (ADR 0001 private BlueZ guest isolation, ADR 0002
per-entry audio LTV interposition, ADR 0003 additive frozen coverage
contract); consulting them is recommended where relevant, and they are not
Approved.

Final evidence bundle for any executed phase: exact source/build/config/
image/tool identities; run/session and harness binding; stimulus
seed/fixture hashes; raw traces/captures; clock domains and mapping
uncertainty; all intended event counts; an independent verdict per domain;
test commands/results and known exclusions. Never relax frozen limits,
reclassify unsupported measurements as zero, or overwrite prior evidence to
obtain a PASS.

## Open dependencies

- PB-042 tooling/rights decision gates PB-043, PB-044, PB-047, PB-048
  (software parts) and PB-049, PB-050.
- PB-041 fixture identity and DAC-presence evidence gates physical rows
  (PB-048 physical, PB-049, analog lanes).
- DSP spectral quality characterization needs product-grounded numeric
  envelopes before any limit exists.
- Anchor maps, trace schema, and deadline metrics need no owner decision to
  start design, but any executed row needs the PB-041 identity lane for
  physical capture.

## Official method references

- [Nordic ISO time-sync source docs, pinned NCS revision](https://github.com/nrfconnect/sdk-nrf/blob/b20f8619ba9a5530f8c34b0a130d829947cfe55d/samples/bluetooth/iso_time_sync/README.rst)
- [Nordic ACL connection-time-sync source docs, pinned revision](https://github.com/nrfconnect/sdk-nrf/blob/b20f8619ba9a5530f8c34b0a130d829947cfe55d/samples/bluetooth/conn_time_sync/README.rst)
- [SDC anchor-report contracts, pinned nrfxlib revision](https://github.com/nrfconnect/sdk-nrfxlib/blob/bb715183a57ac501907b63df2079c3fbcd067641/softdevice_controller/include/sdc_hci_vs.h#L309-L334)
- [SDC report enable, overwrite and reset contract](https://github.com/nrfconnect/sdk-nrfxlib/blob/bb715183a57ac501907b63df2079c3fbcd067641/softdevice_controller/include/sdc_hci_vs.h#L1729-L1756)
- [libsamplerate quality measurement](https://libsndfile.github.io/libsamplerate/quality.html)
- [PulseView capture/decoder manual](https://sigrok.org/doc/pulseview/unstable/manual.html)

These sources support methods and API semantics, not invented product timing
or quality thresholds. The local v3.4.1 source tree remains the authority for
installed behavior. This plan does not imply EULA acceptance, download
rights, or redistribution approval for any vendor tool, corpus, or archive.
SDK citations use repository-relative paths against the active SDK tree;
do not substitute local home-directory or external-library absolute paths.
