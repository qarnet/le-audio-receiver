# System HIL milestones

Status: **plan of record for the System HIL track, revised 2026-09-03**. RH0,
RH1, and RH2 are accepted with retained evidence; RH3 is in progress with
frozen receiver transport limits; RH4 and the analog extensions stay gated on
RH3 acceptance. Scope decisions recorded 2026-09-03: nRF54L15 is the only
production receiver target and the nRF5340 release track is eliminated from
this plan (see final section); 7.5 ms (`48_3_1`) is not a supported release
shape until RH3-7p5 closes it (see that phase). First milestone uses hardware
already present and proves receiver transport/runtime behavior without stereo
analog feedback. Mono aggregate feedback may be added with existing USB
adapter after safe electrical qualification. Stereo analog acceptance remains a
later extension.

## Standing decisions (2026-09-03)

1. nRF54L15 is the only production receiver target for this plan. All nRF5340
   receiver testing and its factory release ZIP are eliminated here; the
   nRF5340DK keeps exactly one role, the dedicated HIL source fixture.
2. 7.5 ms (`48_3_1`) must work or it must not be supported. It is removed from
   the mandatory matrix and from release claims until RH3-7p5 either fixes and
   reinstates it or removes it from the product advertisement.
3. Receiver transport limits are frozen (see "Receiver transport limits") and
   enforced by the runner. A row that delivers a small fraction of submitted
   audio is a failed row, not a pass.
4. Reruns are the fix-validation mechanism. Classify a failure (product,
   fixture, or environment defect), fix it, rerun the same row against the
   failing baseline, then rerun the full matrix. Only blind unclassified
   retries are prohibited.

## Goal

Create repeatable hardware-in-the-loop (HIL) evidence now across boundaries
available with current fixture:

```text
dedicated LE Audio source board
  -> Bluetooth LE Audio air interface
  -> nRF54L15 receiver cpuapp + FLPR
  -> receiver serial diagnostics and lifecycle counters
```

This first gate proves pairing, BAP/ASCS/ISO transport, LC3 decode, Mode A/Mode
B routing decisions, ASRC/offload behavior, I2S submission health, teardown,
and reconnect. It does not prove physical DAC output, left/right wiring, analog
continuity, or audibility.

Preserve extension seam for later topology:

```text
nRF54L15 receiver -> I2S DAC -> qualified stereo capture -> analog oracle
```

Only after stereo extension passes may receiver fixture become known-good
analog endpoint for transmitter interoperability claims.

## Scope

### Milestone 1: nRF54L15 receiver transport/runtime acceptance

- One nRF54L15 receiver DUT at a time.
- One dedicated hardware LE Audio source fixture.
- Autonomous pairing, connection, streaming, teardown, and reconnect.
- Mono, Mode A, and Mode B at 48 kHz with 10 ms frames. 7.5 ms is
  diagnostic-only until RH3-7p5 closes it (see standing decisions).
- Receiver and source serial diagnostics as mandatory evidence.
- Exact device, firmware, command, log, and evidence provenance.
- Fail-closed warning and error handling.

### Milestone 1 extension A: existing mono adapter

- Optional after base transport/runtime gate works.
- Reuse USB `0d8c:0014` mono microphone adapter.
- Add only through reviewed passive summing, attenuation, and DC-blocking
  fixture; never connect DAC line outputs directly to microphone input.
- Prove aggregate signal presence, both distinct carrier components, gross
  clipping, and gross dropout.
- Label result mono output smoke, not stereo or full system acceptance.

### Milestone 1 extension B: future stereo feedback

- Add qualified simultaneous two-channel line capture later.
- Prove physical channel mapping, separation, per-channel level/continuity, and
  analog output behavior.
- Reuse source waveform, runner capture interface, evidence schema, and matrix;
  do not redesign base orchestration.

### Milestone 2: transmitter interoperability evaluation

- Future transmitter work only, after Milestone 1 stereo extension is accepted.
- Reuse stereo-accepted receiver and analog oracle as a known-good sink.
- Add independent peer implementations only when a concrete transmitter
  product contract exists.
- Record standards interoperability separately from same-repository
  end-to-end compatibility.

## Non-scope

- Simultaneous testing of multiple receiver targets; nRF54L15 is the only
  production receiver target in this plan.
- Replacing current unit, coverage, build-contract, or BabbleSim gates.
- Treating cpuapp plus FLPR, or cpuapp plus cpunet, as multiple DUTs. Those are
  companion images inside one physical DUT.
- NCS migration as part of receiver HIL work.
- Targeting unknown or non-Nordic hardware.
- nRF54L15 recovery-tool development.
- Calibrated THD, SINAD, SNR, frequency-response, or certification claims.
- Claiming physical stereo mapping, analog continuity, or audibility from base
  transport/runtime milestone or mono adapter extension.
- Phone interoperability, CAP/CAS, TMAS, CSIS, broadcast audio, or extra
  codecs unless later requirements add them.
- nRF5340 receiver testing or release publishing; that target and its release
  track are eliminated from this plan (see final section).
- 7.5 ms (`48_3_1`) as a supported release shape until RH3-7p5 closes it.

## Grounding

### Existing receiver evidence

- `scripts/bap_central.py` already drives strict mono, default Mode A, and Mode
  B at 48 kHz and 10 ms.
- `scripts/bluez-wireplumber-gate.py` already proves stock BlueZ,
  WirePlumber, and PipeWire playback at 7.5 ms.
- `docs/development/firmware-release-fr4-procedure.md` already defines
  fail-closed boot, flash, pairing, reconnect, warning, FLPR, and evidence
  rules.
- `docs/testing/behavior-contract.md` defines stable receiver outcomes and
  parseable status fields.
- `docs/development/firmware-release-fr4-results.md` shows why hardware
  acceptance matters: a 120-second mono run exposed deterministic callback
  omissions and 225 DMA restarts despite earlier software evidence.
- Existing hardware evidence does not include an automated analog capture
  oracle. Audibility has remained optional human observation.

### Dedicated source starting point

Installed NCS v3.3.0 contains
`zephyr/samples/bluetooth/bap_unicast_client/`. It scans for an ASCS server,
configures unicast streams, and transmits LC3. Its board configuration supports
`nrf5340dk/nrf5340/cpuapp` with sysbuild. Its `stream_lc3.c` generates a sine
wave and encodes real LC3.

The sample is reference code, not ready acceptance firmware:

- `src/main.c` selects `BT_BAP_LC3_UNICAST_PRESET_16_2_1`, while this receiver
  accepts 48 kHz only.
- `src/stream_tx.c` sends opportunistically when ISO buffers become available;
  acceptance needs explicit bounded run control and final counters.
- Current generated audio does not provide distinct left/right signatures
  needed to detect channel swaps or one silent channel.
- Acceptance needs deterministic peer selection, fresh/bonded state control,
  machine-readable results, and clean restart behavior.

Build a repository-owned source fixture from this verified API example rather
than treating the sample binary as a test oracle.

The sample's exact nRF5340 sysbuild path is sufficient for frozen profiles:

- `sample.yaml` allows `nrf5340dk/nrf5340/cpuapp` with sysbuild.
- `sysbuild.cmake` adds `hci_ipc`, selects
  `nrf5340_cpunet_iso-bt_ll_sw_split.conf`, and applies the
  `bt-ll-sw-split` snippet.
- That CPUNET configuration enables ISO central, two connected ISO streams,
  310-byte host ISO TX MTU, and 247-byte controller ISO SDU maximum. Largest
  baseline SDU is 240-byte Mode B `48_4_1`, so it fits verified limits.

### NCS v3.4.0 multi-DUT finding

Do not migrate to NCS v3.4.0 for Twister multi-DUT support. Exact Nordic tag
`ncs-v3.4.0` resolves to sdk-zephyr commit
`bf801e4e3d19e1ffa76164346480cb7734dd2800`, and it does not contain that
feature:

- `scripts/pylib/pytest-twister-harness/src/twister_harness/fixtures.py`
  creates `device_object` from `twister_harness_config.devices[0]`.
- Tagged pytest documentation exposes singular `dut`, `unlaunched_dut`, and
  `shell` fixtures.
- Tagged testsuite schema and source contain no `required_devices` support.
- Tagged tree contains no `tests/subsys/testsuite/multidut` test.

Upstream Zephyr main added multi-DUT reservation in commit
`30479513933afbd8e7b97e31e23cff0f3debf8a6` and later added demonstration tests
in `a31ddde203ad060ed0c976bb8ad1ea0f6a8b1c31`. Current online documentation
therefore describes behavior newer than exact Nordic v3.4.0. Version migration
would add receiver and toolchain risk without providing required orchestration.

### Current host-tool finding

Both bare host PATH and current `nix develop` shell lack `pytest`, labgrid,
`arecord`, `aplay`, and pyserial. `flake.nix` currently adds only gcovr,
dbus-python, and PyGObject beyond the NCS shell.

Base milestone needs only `python3Packages.pytest` and
`python3Packages.pyserial`. Do not block base work on audio packages. Mono or
stereo capture extension later adds `alsa-utils` and
`python3Packages.numpy`; `flake.lock`, not workstation-global install, pins
their exact versions. NumPy is enough for proposed FFT, correlation, and
windowed-energy oracle; SciPy stays unnecessary unless adapter qualification
proves otherwise.

## Runner decision

Use standalone pytest with repository-owned fixtures. Do not run this milestone
as a Twister test.

### Why pytest

- Direct ownership of two physical devices with different firmware and roles.
- Fixture-scoped acquisition and guaranteed cleanup for probes, serial ports,
  Bluetooth state, optional future capture process, and retained evidence.
- Natural matrix parametrization across mode, frame duration, fresh/bonded
  state, and fault cases.
- JUnit output without forcing production firmware into a synthetic Zephyr
  test application.
- Public-boundary tests can validate runner behavior with fake processes and
  recorded logs before hardware execution.

Pin pytest and pyserial during first implementation phase. Add ALSA utilities
only with mono/stereo extension. Keep HIL tests outside canonical software gate
until lab resource contract and runtime are accepted.

### Why not Twister

NCS v3.3.0 and exact v3.4.0 expose one pytest DUT. Twister could own the
receiver while custom code secretly owns a source fixture, but then device
reservation, source flashing, source serial failures, and cleanup live outside
Twister's model. That gives Twister reporting without Twister resource safety.

### Why not labgrid yet

Labgrid becomes useful when hardware moves behind remote exporters, several
racks run concurrently, or reservation conflicts become a recurring problem.
Current scope has one local receiver/source pair and no installed labgrid
stack. Adding coordinator, exporter, environment files, drivers, and pytest
plugin now would increase failure surface without changing acceptance power.

Keep resource names and operations explicit so a later labgrid adapter can
replace local fixtures without changing test scenarios or oracles.

### Fixture lifecycle

Use narrow fixtures and register cleanup immediately after each side effect.
Do not put acquisition, flashing, serial capture, audio capture, and streaming
behind one `yield`: setup failure before that yield would skip teardown for
already-acquired resources.

Required ownership order:

1. Session fixture creates run root and acquires exclusive tuple lock. Its
   finalizer always releases the lock after evidence finalization.
2. Device fixtures resolve identities, then open each probe/serial resource.
   Every open gets an immediate `request.addfinalizer()` or `ExitStack`
   callback.
3. Per-test fixture creates row directory and starts receiver/source serial
   captures before any reset. Each process gets an immediate terminate, wait,
   kill-if-needed finalizer.
4. Stream helper registers source stop/disconnect cleanup before issuing START.
5. Runner captures active receiver FLPR offload after source `streaming`, reads
   only CIS-dependent receiver ISO link quality at `scored_complete` while CISes
   remain live, then awaits bounded source teardown and every receiver stream
   summary before collecting terminal `audio status`, `audio perf`, stopped
   `flpr offload`, and `flpr status` diagnostics.
6. Row finalization validates records, writes manifest/JUnit data, then permits
   lower-level finalizers to close resources.

Capture extension adds one capability-scoped fixture between steps 3 and 4. It
opens exact ALSA `hw:` endpoint, validates negotiated format, and immediately
registers closure. Base tests never instantiate this fixture. Runner scenario
and source control remain identical with capture capability absent, mono, or
stereo.

`SIGINT`, `SIGTERM`, timeout, assertion failure, setup failure, and analyzer
failure use the same bounded cleanup path. Cleanup failure changes row verdict
to failed but never deletes primary evidence. Unit tests must inject failure
after every side effect and prove no owned process, descriptor, lock, or active
stream survives.

## Resource and identity contract

### Standing lab nRF hardware authority

The user's standing authority applies to active and future System HIL work:

- Any attached Nordic nRF board may be used, flashed, erased, recovered, reset,
  or debugged without separate per-action or per-run approval. Full erase and
  recovery remain subject to target and tooling support.
- Before any target-changing action, resolve identity with `nrf-probes` or the
  appropriate project identity resolver and retain raw identity evidence. Never
  rely on a static probe-to-board mapping, and never target an unknown or
  non-Nordic device.
- Use `scripts/hil-runner.py` when its end-to-end evidence lifecycle is useful,
  but it is not the only hardware owner. Direct debugger, serial, and manual
  board investigation are permitted alongside runner-owned execution when they
  provide clearer diagnosis or validation.
- Preserve immutable run directories and prior evidence. Erasing or reflashing
  a board does not authorize alteration of evidence from an earlier run.
- A simulator or host-test failure is evidence, not automatic proof of a
  production firmware defect. Where practical, evaluate the suspected failure
  on a physical nRF board before accepting a behavior-changing source fix, then
  retain both simulator and board evidence.
- Existing central-only pairing and streaming requirements remain in force
  unless a later plan deliberately changes them.

These permissions do not weaken row acceptance, warning handling, artifact
integrity, or evidence requirements.

Each run starts from a checked-in logical fixture description plus a local,
gitignored physical binding. Logical roles:

- `receiver`: nRF54L15 CPUAPP plus FLPR, console, probe, and DAC.
- `source`: dedicated nRF5340 BAP client, console, probe, CPUAPP plus CPUNET.
- optional `capture_mono`: existing qualified mono endpoint plus electrical
  summing/attenuation/DC-blocking fixture.
- future optional `capture_stereo`: qualified stereo line endpoint.

Before touching hardware, runner must:

1. enumerate probes and serial ports, plus USB audio devices only when selected
   capability requires capture;
2. resolve each role by stable identity, not current `/dev/tty*` number;
3. require exactly one match per role;
4. reject unknown, duplicate, missing, or cross-wired identities;
5. save raw identity evidence in run directory;
6. acquire an exclusive local lock for whole fixture tuple.

Standing authority permits agents to flash or reset any attached Nordic nRF
board after identity resolution and raw evidence capture. Never target unknown
or non-Nordic hardware. The runner may own fixture-tuple execution when used,
but direct debugger, serial, and board testing are also permitted.

Dedicated source firmware replaces nRF5340DK's current Linux `hci_uart` role.
Milestone runner does not use `btattach`, BlueZ, or `scripts/bap_central.py` for
source traffic. Preflight must reject any `btattach` or other process holding
source serial endpoint. If lab still needs HCI dongle after a run, restoration
is a separate explicit maintenance action, not hidden test cleanup.

## Clean-state policy

Define clean state as known test-owned state, not blank silicon.

Erase and recovery are allowed diagnostic tools when they help establish known
state, including full-chip erase where target and tooling support it. No
separate approval is required under the standing authority, but every action
must be targeted only after Nordic nRF identity is proven and must record board
identity, reason, exact command, and resulting firmware/state. Preserve prior
evidence; erasure or recovery does not authorize altering it or replace required
clean-state and image-verification records.

Use controlled state transitions instead:

1. Verify receiver and source identities.
2. Flash exact allowlisted image tuples through each target's normal supported
   path and verify every image immediately after write.
3. Start serial capture before reset and require expected boot identities.
4. Put source fixture into a defined idle state through its test protocol.
5. Clear receiver bonds through production `bt unpair`; require exact success
   text and advertising mode.
6. Clear source-side bond/session state through a dedicated idempotent test
   command or boot policy.
7. Reboot both fixtures and verify clean counters, no active connection, and
   expected advertising/scanning state.
8. When mono/stereo capture capability is selected, reset capture controls and
   discard only a short pre-run capture window.

Full-chip erase or recovery remains a target-specific diagnostic procedure,
not an unrecorded blanket operation. Record the board identity, reason, command,
and resulting firmware/state whenever it is used.

## Analog feedback extensions

Analog feedback is not required for base transport/runtime acceptance. Runner
exposes optional capture capability so mono feedback can use present hardware
and stereo feedback can be added later without changing source control or test
matrix.

### Existing mono adapter

Attached device reports USB `0d8c:0014` and appears as C-Media/Unitek Y-247A
microphone adapter. ALSA exposes mono S16_LE capture at 44.1/48 kHz, microphone
gain from -12 dB to +23 dB, and AGC.

It is unsuitable for stereo claims, but usable later for aggregate mono smoke:

- left and right source channels retain distinct 997 Hz and 1601 Hz carriers;
- safe passive network sums DAC L and R without tying outputs together;
- attenuation prevents line-level clipping;
- DC blocking prevents microphone bias from reaching DAC outputs;
- AGC is disabled and gain is fixed before threshold qualification;
- mono spectrum can prove both carrier components are present and detect one
  missing component, total silence, gross clipping, or gross dropout;
- mono sum cannot prove physical L/R order, stereo separation, crosstalk, or
  independent per-channel continuity.

Keep adapter disconnected until summing/attenuation/DC-blocking network receives
electrical review and measurement. Base HIL proceeds without it.

Mono qualification must record exact USB and ALSA identity, direct `hw:` mono
S16_LE 48,000 Hz format, mixer state, adapter gain, AGC-off state, network
schematic and measured values, DAC identity/supply, noise floor, clipping point,
carrier detection margins, and repeated 130-second capture stability. Any xrun,
short read, USB reset, kernel warning, format drift, or WAV repair fails fixture
qualification. Limits freeze before candidate runs.

### Future stereo capture

When stereo feedback becomes priority, use capture fixture exposing simultaneous
two-channel line input. Current preferred candidate remains Behringer UCA222 or
UCA202 with fixture-owned passive stereo attenuation cable:

- Behringer documents two analog inputs and outputs. Product manuals specify
  unbalanced RCA line inputs, approximately 27 kOhm input impedance, 2 dBV
  maximum input, fixed 16-bit conversion, and 48 kHz support.
- Fixed line input has no per-channel gain knobs or AGC to drift between runs.
- UDA1334ATS datasheet specifies 0.9 V RMS output at 0 dBFS with 3.0 V supply,
  below UCA 2 dBV (about 1.26 V RMS) maximum input.
- PCM5102A datasheet specifies 2.1 V RMS full-scale line output, above UCA
  maximum by about 4.4 dB. Direct PCM5102A-to-UCA connection can clip.

Use reviewed, enclosed, channel-symmetric pad with at least 10 dB nominal
attenuation. Final resistor values, measured attenuation, channel mismatch,
input loading, wiring, DAC board, USB path, kernel, ALSA format, and calibration
limits become fixture metadata.

UMC202HD remains second future choice. It accepts two line inputs with greater
headroom, but adds two gain knobs, LINE/INST and PAD switches, phantom-power
control, and Linux device-specific streaming quirk. UCA plus fixed pad has less
mutable fixture state.

Stereo qualification adds physical swap, one-channel silence, duplication,
separation, per-channel dropout, level mismatch, and channel-order defects to
mono qualification. Purchase choice is not acceptance.

Human audibility remains supplemental. Base and mono-extension results cannot
replace machine-observable stereo acceptance.

### Grounding sources for future stereo work

- Behringer UCA222 product page:
  <https://www.behringer.com/en/products/0805-AAG>
- Behringer UCA202 manual, published through Music Tribe:
  <https://mediadl.musictribe.com/media/PLM/data/docs/P0484/UCA202_M_EN.pdf>
- Behringer UMC202HD product page:
  <https://www.behringer.com/en/products/0805-AAR>
- NXP UDA1334ATS datasheet:
  <https://www.nxp.com/docs/en/data-sheet/UDA1334ATS.pdf>
- TI PCM5102A product page and datasheet:
  <https://www.ti.com/product/PCM5102A>

## Frozen source codec profiles

Use exact NCS v3.3.0 standard presets already exercised by BabbleSim. Do not
substitute negotiated PipeWire values inside baseline rows.

| Profile | Frame | Per-channel octets | Mono / Mode A SDU | Mode B SDU | QoS |
|---|---:|---:|---:|---:|---|
| `48_4_1` | 10 ms | 120 | 120 bytes | 240 bytes | unframed, RTN 5, latency 20 ms, presentation delay 40 ms |
| `48_3_1` | 7.5 ms | 90 | 90 bytes | 180 bytes | unframed, RTN 5, latency 15 ms, presentation delay 40 ms |

`BT_BAP_LC3_UNICAST_PRESET_48_4_1` and
`BT_BAP_LC3_UNICAST_PRESET_48_3_1` define those values in exact installed
`zephyr/include/zephyr/bluetooth/audio/bap_lc3_preset.h`. Mode B uses same
per-channel codec values with FL|FR allocation and doubled SDU size, matching
`tests/bsim/client/src/bsim_client_main.c`.

The PipeWire gate's observed 7.5 ms `48_5_1` shape uses 117 octets per channel.
That remains separate interoperability evidence. Add an optional `48_5_1` row
only after baseline matrix passes; never mix its result into `48_3_1` evidence.

`48_3_1` is retained for RH3-7p5 diagnostics only. It is not a supported
release shape until that phase closes it: working and reinstated, or removed
from the product.

## Receiver transport limits (frozen 2026-09-03)

Basis: adjacent boards, 2M PHY, RTN 5. Observed healthy 10 ms hardware rows
deliver essentially every submitted SDU (RH2 formal pass: source `sub=764`,
receiver `SDUs=764`, `plc=13` of `decoded=777`, 1.7%). The 90% floor is
deliberately conservative; tighten toward 98% after a twice-pass matrix
establishes run-to-run variance.

Per-stream gates, enforced by the runner at session end:

| Metric | Gate | Rationale |
|---|---|---|
| `rx_valid` | at least 90% of the segment's expected submitted SDUs | Primary delivery metric. Every loss mechanism (controller-unreceived subevents, host-lost events, errored payloads) reduces it, so it measures the fraction of transmitted audio that reached the app. |
| `plc` | at most 5% of `decoded` | Concealed frames are the audible-degradation proxy. Healthy baseline 1.7%; 5% is the v1 ceiling, to be tightened. |
| `rx_error` | 0 | Corrupt-payload callbacks must be absent in a healthy row. |
| `rx_unknown` | 0 | Unclassifiable callbacks indicate stack misuse. |
| `empty_sdu` | 0 | Zero-length valid SDUs are concealment events, not delivery. |
| `decode_err` | 0 | Existing gate. Broken payloads that reach decode fail here. |
| `i2s_underrun` | 0 | Existing gate. Delivered audio must reach I2S. |
| `stream_reset` | 0 | Existing gate. |

Push failures stay gated through the existing post-stop audio-faults check.

Record-only in v1 (reported, not gated): `rx_no_ts`, controller tail counters
`crc_error`, `retransmitted`, `duplicate`, `rx_unreceived`, host `rx_lost`,
and the CIS layout fields. They explain losses; the `rx_valid` ratio already
catches their effect. `crc_error` is absorbed by RTN retransmission and
surfaces in the delivery ratio.

A missing extended-summary field fails closed. A limit violation fails the row
with the exact observed value and the frozen threshold in the failure detail.
Limit values live in one runner-owned constant set; changing any value is a
plan revision, not a test tweak.

## Source signal and control contract

### Deterministic signal

Dedicated source firmware uses one fixed-point phase accumulator and one LC3
encoder per logical channel. Never reset phase at LC3 frame boundaries. Emit
same semantic waveform at 7.5 ms and 10 ms:

- Left carrier: 997 Hz.
- Right carrier: 1601 Hz.
- Mono: left semantic channel, duplicated by receiver to both analog outputs.
- Mode A: left and right signatures on two mono ASEs.
- Mode B: same left and right signatures in one two-channel ASE, encoded L then
  R with independent encoder state.
- Preamble: frame-aligned silence, L-only, silence, R-only, silence, both. Every
  segment duration is a multiple of 120 ms, common to 7.5 ms and 10 ms frames.
- Scored payload: continuous carriers under independent deterministic 120 ms
  binary amplitude envelopes with bounded transition ramps. Seeds and segment
  map are protocol constants and appear in source HELLO output.
- Tail: five-second valid encoded silence after last scored SDU. Source emits
  `scored_complete` at tail entry, keeps streams alive for receiver status
  collection, then tears down automatically. Future capture uses tail to
  distinguish clean stream end from truncated data.

RH1 native tests generate reference PCM, encoded payload, and LC3-decoded
fixtures for both frame durations and all modes. For same profile, mode, seed,
and sequence range, PCM and encoded payload bytes must repeat exactly; volatile
timestamps stay outside those byte comparisons.

### Optional capture oracle

Base milestone does not invoke analog analyzer. Capture extension locates scored
interval from preamble correlation, not host process start time.

Mono capability retains:

- WAV structure, exact 48 kHz mono format, frame count, and scored duration;
- full-scale sample count, noise floor, DC offset, and gross clipping;
- both expected-carrier powers and carrier-presence margins;
- combined amplitude-envelope correlation, lag, and lag drift;
- moving-RMS minimum, low-energy-window count, longest low-energy run, and
  repeat-run variation.

Stereo capability keeps all mono metrics per channel and adds opposite-channel
leakage, channel-map margin, duplication detection, level mismatch, and
independent phase/frequency continuity.

Each extension freezes numerical limits from its own fixture qualification.
Plan intentionally does not invent limits before safe hardware path exists.
Generated analyzer fixtures include known silence, missing carrier, clipping,
truncation, DC offset, clock drift, brief/long dropout, and discontinuity. Stereo
fixtures additionally include swap, duplication, one-channel loss, and leakage.
Every corruption must fail for named metric, while gain and sub-threshold timing
perturbations inside qualification bounds must pass.

Do not use analog capture to infer Bluetooth packet counts. Source and receiver
logs prove protocol/runtime behavior; capture proves emitted audio behavior.

### Source serial protocol

Use versioned ASCII commands with one-line records prefixed `HIL1 ` followed by
JSON. Every record carries protocol version, firmware identity, monotonic time,
command ID, and run ID. Required commands:

- `hello`: firmware/image hashes, board identity, protocol constants, clean or
  active state;
- `idle`: idempotently stop streams, delete group, disconnect, clear volatile
  counters, and report completion;
- `unpair`: delete only fixture receiver bond and report resulting bond count;
- `configure`: exact peer identity, mode, profile, scored SDU count, signal seed,
  and reconnect policy;
- `start`: acknowledge quickly, then run bounded connect, security, discovery,
  configure, QoS, enable, CIS, stream, preamble, scored payload, tail, and stop
  lifecycle on a worker while `stop` and `status` remain responsive;
- `stop`: idempotent bounded early stop for runner cleanup;
- `status`: state plus per-stream sequences, submitted SDUs, send failures,
  callbacks, ASCS responses, disconnect reason, and first error.

Final asynchronous record has one terminal verdict and no later unsolicited
event for same run ID. A later `status` command may return an immutable terminal
snapshot under its new command ID. Unprefixed source logs remain subject to
warning/error scan. Parser rejects duplicate terminal events, command/run
mismatch, missing fields, backward state transitions, counter regressions, or
unsolicited output after terminal verdict.

Bounded abort: a `stop`, timeout, or runtime-error teardown before
`scored_complete` emits the exact state record `{"state":"teardown",
"cause":"stop"|"timeout"|"error"}` instead of synthesizing skipped states.
The abort edge is legal only from an already-started non-teardown segment,
jumps that segment straight to teardown, is terminal for the whole run (no
reconnect, terminal verdict must be `fail`), and is rejected by the parser on
any other shape.

## Milestone 1 phases

### RH0: freeze contracts and dependencies

- Pin pytest and pyserial in project Nix shell. Do not add audio dependencies.
- Define logical fixture schema, local binding schema, run-directory layout,
  lock behavior, stop rules, and JUnit/evidence output.
- Define source `HIL1` schema and capture capability enum (`none`, `mono`,
  `stereo`). Base scenarios select `none`; schema leaves optional capture
  evidence section absent rather than filling it with fake success.
- Add behavior tests for identity rejection, failure after each setup side
  effect, timeout, cancellation, process failure, cleanup failure, parser
  rejection, and evidence finalization.

Exit: runner skeleton proves resource lifecycle without attached hardware.

### RH1: dedicated source firmware

- Derive source from verified NCS v3.3.0 BAP unicast client APIs.
- Support frozen `48_3_1` and `48_4_1` receiver shapes: 48 kHz, 7.5 ms and 10
  ms, mono, Mode A, and Mode B, one frame block per SDU.
- Add deterministic peer selection, finite run control, clean-state command,
  reconnect control, phase-continuous coded source signals, per-channel LC3
  encoders, counters, and `HIL1` final records.
- Keep source firmware independent from receiver internals.

Exit: source passes focused native tests where practical, builds cleanly, and
can run repeatably against receiver without Linux BlueZ or `bap_central.py`.

### RH2: two-device orchestration

- Acquire fixture tuple and verify identities.
- Flash and verify exact source and receiver image tuples.
- Capture both consoles before reset.
- Establish clean state through controlled, identity-proven state transitions;
  targeted erase or recovery is allowed when diagnostic need requires it and is
  recorded.
- Run one short mono connection and prove ordered cleanup after pass, failure,
  timeout, and cancellation.

Exit: one command produces self-contained manifest, logs, hashes, JUnit, and
nonzero exit on any failed boundary.

### RH3: receiver transport/runtime matrix

Mandatory rows for nRF54L15, 10 ms only:

| State | Mode | Profile | Scored SDUs per stream | Minimum scored interval |
|---|---|---|---:|---:|
| fresh pair | mono | `48_4_1` | 12,000 | 120 s |
| fresh pair | Mode A | `48_4_1` | 12,000 | 120 s |
| fresh pair | Mode B | `48_4_1` | 12,000 | 120 s |
| preserved bond | Mode B | `48_4_1` | 12,000 | 120 s |

Every mandatory row is additionally subject to the frozen receiver transport
limits. The matrix is eight child runs (four rows, two passes).

Add explicit disconnect/reconnect and nRF54L15 FLPR hang/stall recovery rows
after healthy matrix passes. Fault-injection rows use named windows and cannot
weaken healthy-row warning rules.

Exit: full matrix passes twice from independently established clean state with
same locked counter rules and no manual intervention. Record verdict
`TRANSPORT_RUNTIME_ACCEPTED`, never `SYSTEM_AUDIO_ACCEPTED`.

### RH3-7p5: 7.5 ms delivery loss (named open question, not release-blocking)

`48_3_1` rows are removed from the mandatory matrix and remain selectable as
single diagnostic rows. Evidence so far:

- HIL source at 7.5 ms: H40 (traced) and H42 (untraced) both completed their
  rows while delivering almost nothing, with zero CRC errors. H42 receiver
  summary: `rx_valid=24` of `16859` submitted, controller `rx_unreceived=18891`,
  `crc_error=0`, `plc=38924`. Those rows passed only because no transport
  limits existed; under the frozen limits they fail loudly, which is correct.
- Counter-evidence that the receiver can receive 7.5 ms at all: the accepted
  BZ2 desktop gate streamed 7.5 ms from a Linux central with nonzero decode
  and zero faults (`docs/development/phase2-stock-desktop-gate-results.md`).
  The loss signature therefore points at the dedicated HIL source side or the
  SW-split central to SDC peripheral combination at 7.5 ms, not automatically
  at the receiver product.

Exit is binary per the standing decision: root-cause and fix, then reinstate
the three `48_3_1` rows in the mandatory matrix, or remove 7.5 ms advertisement
from the production PACS. Until one happens, no release claims 7.5 ms. If it is
removed from the product, the BZ desktop gate re-runs at 10 ms.

### RH4: exact-artifact transport/runtime integration

- Run same accepted HIL matrix against immutable candidate assets, not a local
  rebuild.
- Preserve artifact identities, image hashes, flash logs, source identity,
  receiver/source logs, environment versions, and JUnit in one manifest.
- `run-rh4-matrix` accepts only one receiver FR1 factory ZIP and one
  deterministic HIL-source ZIP. It validates archive member order, metadata,
  manifests, internal checksums, and hashes before staging private images
  outside the repository or touching fixture hardware. It copies original ZIP
  bytes into aggregate evidence and re-hashes original and staged inputs before
  every flash helper call.
- Keep release publication outside HIL runner.

Exit: exact candidate receives transport/runtime verdict only. Publication and
full audio-system acceptance remain separate decisions; partial evidence cannot
be relabeled.

### MA0: qualify existing mono adapter

- Starts only after RH3 works; never blocks RH0-RH3.
- Add `alsa-utils` and NumPy to pinned shell.
- Build and electrically review passive L/R summing, attenuation, and
  DC-blocking fixture before connecting DAC.
- Disable AGC, lock gain, prove direct `hw:` mono 48 kHz capture, and freeze
  mono metric limits.
- Validate analyzer with synthetic missing-carrier, silence, clipping,
  truncation, drift, and dropout defects.

Exit: exact adapter plus electrical network is qualified for mono aggregate
smoke only.

### MA1: mono aggregate matrix

- Reuse RH3 matrix and source bytes unchanged with capture capability `mono`.
- Require both distinct carriers for Mode A and Mode B rows.
- Require expected single carrier for mono rows.
- Preserve WAV, hash, adapter state, and all metrics beside base evidence.

Exit: base matrix plus mono checks passes twice. Record verdict
`MONO_OUTPUT_SMOKE_ACCEPTED`; do not claim channel order or stereo acceptance.

### SA0: future stereo fixture and oracle

- Acquire and qualify simultaneous stereo line-capture hardware.
- Implement capture capability `stereo` behind same runner interface.
- Extend mono analyzer with per-channel mapping, separation, duplication,
  leakage, level, and continuity metrics.
- Validate deliberate swap, one-channel loss, duplication, leakage, clipping,
  truncation, drift, and dropout captures.

Exit: stereo fixture detects each injected defect for right reason.

### SA1: future stereo matrix

- Reuse RH3 matrix, source firmware, source bytes, receiver firmware, control
  protocol, clean-state flow, and evidence schema unchanged.
- Add only stereo fixture binding and stereo oracle result.

Exit: full matrix passes twice with frozen stereo limits. Record verdict
`STEREO_OUTPUT_ACCEPTED`.

## Milestone 1 acceptance

### Base transport/runtime verdict

Every RH3/RH4 healthy row requires following evidence classes.

### Source

- Expected mode, frame duration, ASE/CIS count, SDU size, and duration.
- Exact scored submitted-SDU count on every stream and monotonic sequence range.
- Zero send/configuration/lifecycle errors.
- Clean stop and disconnect.

### Receiver

- Expected codec/mode and stream lifecycle.
- `decode_err=0`, `i2s_underrun=0`, and `stream_reset=0`.
- Zero malformed/configuration/push failures.
- PLC and received-SDU counts present and within fixture limits frozen before
  candidate execution. PLC is never silently ignored or inferred from analog
  output.
- Zero unexplained warnings, errors, assertions, faults, or recoveries.
- Active FLPR evidence is captured while source streaming is active. A later
  post-stop `STOPPED` snapshot is terminal lifecycle evidence, never an active
  offload substitute.
- CIS-dependent ISO link quality is captured only during the live scored tail;
  audio, performance, stopped offload, and handshake diagnostics follow all
  receiver stream summaries.
- At 10 ms, healthy FLPR path is ACTIVE with submit equal success, fallback
  zero, and fault counters zero.
- At 7.5 ms, CPUAPP ASRC fallback is expected because current FLPR payload
  contract accepts 480 input frames, not 360. This expected path must be
  explicit and fault-free, not mislabeled as offload success.

### Evidence integrity

- Exact git commit, NCS/toolchain version, image hashes, physical identities,
  command lines, exit statuses, timestamps, and host versions.
- Raw logs retained outside repository.
- `MANIFEST.md` and `SHA256SUMS` cover retained evidence.
- Test process returns nonzero on any missing field, warning, timeout, identity
  drift, cleanup failure, or parser failure.

Passing these sections means `TRANSPORT_RUNTIME_ACCEPTED`. It proves real
two-device radio/firmware operation through I2S submission boundary. It does
not prove DAC pins toggled correctly, analog signal exists, speakers are wired,
or left/right output is correct.

### Mono output smoke extension

MA1 additionally requires:

- valid mono capture for whole scored interval;
- preamble correlation locating unique scored interval;
- expected combined carrier set for selected mode;
- no silence, gross clipping, gross dropout, or lag drift beyond frozen mono
  fixture limits;
- exact adapter, mixer, network, DAC, and WAV provenance.

Passing means `MONO_OUTPUT_SMOKE_ACCEPTED`. It does not prove physical channel
order, stereo separation, crosstalk, or independent per-channel continuity.

### Future stereo output extension

- Valid stereo capture file for whole scored interval.
- Non-silence after bounded startup allowance.
- Preamble correlation uniquely locates scored interval.
- Expected left/right carrier and coded-envelope signatures with correct mapping.
- No clipping.
- No dropout, phase discontinuity, lag drift, DC offset, leakage, or channel
  mismatch beyond thresholds frozen during SA0.
- Stable level, carrier, envelope, and timing metrics within frozen
  qualification bounds.

These checks prove functional audio emission and continuity. They do not prove
calibrated audio fidelity.

Passing base plus stereo sections means `STEREO_OUTPUT_ACCEPTED`. Raw captures,
WAV hashes, oracle metrics, and stereo fixture identity join base manifest.

## Milestone 2: future transmitter interoperability

Start only after SA1 records `STEREO_OUTPUT_ACCEPTED` for receiver fixture and
analog oracle.

Evaluate a future transmitter as product under test, rather than reusing its
own receiver or decoder as sole oracle.

### Initial topology

```text
future transmitter DUT
  -> LE Audio air interface
  -> accepted nRF54L15 receiver fixture
  -> DAC + qualified stereo capture
  -> accepted analog oracle
```

### High-level phases

1. Freeze transmitter roles, codec modes, controls, and expected peer behavior.
2. Add transmitter resource/firmware identity and serial protocol to HIL
   fixture without changing accepted receiver oracle.
3. Run required 48 kHz mode/frame matrix, reconnect, repeated-run, and negative
   negotiation cases against accepted receiver.
4. Add at least one independent standards peer before claiming broad
   interoperability. Same-repository transmitter-to-receiver success proves
   system compatibility, not ecosystem compatibility.
5. Record transmitter acceptance separately from receiver RH4/SA1 evidence.

Detailed transmitter matrix remains intentionally open until transmitter
requirements identify supported roles, modes, physical hardware, and external
peer targets.

## Stop rules

Stop run immediately on:

- ambiguous, unknown, duplicate, missing, or changed device identity;
- any command targeting an unknown or non-Nordic target;
- any matrix-runner command targeting a device outside the declared fixture
  tuple;
- image verification failure;
- serial capture not armed before reset;
- missing required identity or evidence capture;
- source or receiver warning/error outside named fault-injection window;
- missing machine-readable final status;
- when capture capability is selected, audio format/channel drift;
- when capture capability is selected, clipping, silence, missing carrier, or
  capture truncation;
- for stereo capability only, channel mismatch, swap, duplication, or leakage;
- cleanup or evidence-finalization failure.

Preserve evidence from failed runs. Reruns are the fix-validation mechanism:
after a failure is classified (product defect, fixture defect, or environment
defect) and a fix lands, rerun the same row against the failing baseline, then
rerun the full matrix. What is prohibited is not rerunning but rerunning blind:
repeating an identical unclassified failure in the hope it passes.

## Failure triage and rerun discipline

Every failed run gets a written classification within its result record:

- product defect: receiver or source firmware behavior;
- fixture defect: harness, flashing, serial, identity, or runner problem;
- environment defect: host, toolchain, power, or RF interference problem.

Classification may be bounded-diagnostic (a named, single-purpose run or code
probe is allowed), but the loop must close: classification, fix, same-row
rerun, then full-matrix rerun. A second consecutive unclassified failure of
the same row is a stop point for redesign review, not another identical
attempt.

## Release line (how HIL verifies a release)

One chain, one verdict line. No firmware ZIP is published for nRF54L15 unless:

1. canonical software gate passes on the release commit;
2. RH3 mandatory matrix has current acceptance (twice-pass with frozen limits);
3. RH4 runs the same matrix against the exact candidate ZIP (the staged images
   and the published bytes must hash identically);
4. FR5 publishes only after RH4 records the transport/runtime verdict.

Partial evidence cannot be relabeled. If RH3 is not current (for example, a
diagnostic changed receiver firmware after the last matrix pass), RH4 must be
rerun on the changed build.

## Recommendation

Current phase in sequence: RH3a. Freeze the receiver transport limits in the
runner, scope the mandatory matrix to 10 ms, then execute the matrix and close
RH3. Do not install NCS v3.4.0. Do not start RH4 hardware, MA, or SA work
before RH3 acceptance. Keep verdict names distinct so transport/runtime or
mono smoke can never be mistaken for stereo output acceptance.

## nRF5340 elimination (2026-09-03 decision)

nRF5340 is not a production receiver target for this repository's release line.
Consequences:

- The nRF5340 factory release ZIP is eliminated from the release line; the
  firmware-release plan's eventual FR5 publishes nRF54L15 only.
- No nRF5340 receiver rows exist in any HIL matrix in this plan.
- The nRF5340DK retains exactly one role: the dedicated HIL source fixture
  (SW-split central), which is unaffected by this decision.
- Existing nRF5340 receiver code and builds stay in the repository for now
  (removing them is a separate cleanup decision with its own regression
  assessment), but they carry no release obligation and no HIL obligation.
- The earlier FR4 failure on the nRF5340 mono acceptance is closed as
  consequence of this elimination, not as a diagnosed fix.
