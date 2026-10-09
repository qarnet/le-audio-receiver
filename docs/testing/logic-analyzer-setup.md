# Central logic-analyzer debugging setup

Updated: 2026-09-27; initial declaration: 2026-09-25. This file owns the current
physical wiring declaration, discovery procedure, safety limits and proposed
fixture-identification contract.
Other plans should link here rather than copy the channel table.

Product work: [PB-041: identify lab DUT and verify analyzer wiring](../product/backlog/tasks/pb-041%20-%20Identify-lab-DUT-and-verify-logic-analyzer-wiring-per-session.md),
currently Backlog pending refinement. Backlog file owns status, not this snapshot.

## Initial 2026-09-25 documentation checkpoint: status and scope

Bullets below describe the initial documentation-only checkpoint, before the
2026-09-27 captures. For current observations and safety attestation, see the
[2026-09-27 addendum](#2026-09-27-observed-passive-i2s-session-addendum) and
[bring-up results](../development/logic-analyzer-i2s-bringup-results-20260927.md).

- User reports analyzer connected and software previously used successfully.
- Analyzer described as cheap 24 MHz unit. Exact model, electrical input limits,
  USB identity, channel IDs and sustainable capture rate not established here.
- Wiring below is **user-reported, not verified by a capture**.
- Host checks ran: `sigrok-cli --version`, `sigrok-cli --help`, and
  `sigrok-cli --show -P i2s`. Installed versions: sigrok-cli 0.8.0,
  libsigrok 0.6.0, libsigrokdecode 0.6.0. I2S decoder present, inputs
  `sck`, `ws`, `sd`; WAV binary output available.
- No device scans, serial commands, captures, resets, flash operations or pin
  changes performed while preparing this document. No diagnostic command below
  has been implemented by this documentation task.
- Scope: identify fixture, board roles and observed wires. PCM fidelity, ASRC
  stability and transport timing belong to the separate
  [independent audio validation plan](../development/independent-audio-validation-plan.md).

## Current wiring: single authoritative declaration

All signal connections are on a breadboard, tapped on the DAC-side I2S wiring.
Normalize the user's spelling `BLCK` to standard `BCLK`; preserve `3v0` label
as reported until the exact breakout is inspected.

| Analyzer channel | User-connected point | Meaning | Intended receiver output |
| --- | --- | --- | --- |
| **CH0** | **BCLK** | I2S bit clock | XIAO **D0 / P1.4** |
| **CH1** | **WSEL** | I2S word select, also LRCK | XIAO **D1 / P1.5** |
| **CH2** | **DIN** | DAC data input, MCU SDOUT | XIAO **D2 / P1.6** |
| **CH3** | **3v0** | Passive observation of DAC power-rail label | **Not an MCU signal output** |
| **GND** | User-confirmed 2026-09-27 | Required common analyzer/board/DAC ground | Reconfirm after wiring or session changes |

The MCU pin column comes from `docs/hardware-wiring.md:45-65` and the receiver
overlay, not a live continuity test. Analyzer CH0 is not the same naming domain
as XIAO D0. Record actual driver channel IDs rather than assuming a printed CH0
is called `D0` or `0` by sigrok.

**CH3 high is not a power-good measurement.** It only means voltage exceeded
analyzer's digital threshold at sampled instants. It does not prove 3.0 V,
3.3 V, regulator health, DAC identity, or absence of short supply glitches.
Adafruit's UDA1334A guide labels its regulator output **3Vo** and describes a
3.3-V output; this may explain the label, but the current clone/model is unverified.
Use a meter for DC voltage and an oscilloscope for ripple/brownout investigation.

The receiver also routes **MCK to D3/P1.7**. Do not treat D3 as spare, attach an
identification driver there, or change it merely because the DAC uses three wires.

## Electrical safety and breadboard limits

Before attaching or changing probes, power down and confirm the analyzer's input
voltage limits and common ground. Connect passive high-impedance inputs only.
Do not connect an analyzer power output to the board or drive CH3's supply rail.
Avoid back-powering an unpowered DAC through MCU I2S outputs. Confirm the actual
breakout, supply arrangement and safe mute/disconnect procedure before active tests.

Breadboards and long parallel leads can create crosstalk, ringing and bad ground
return paths. A logic analyzer may reveal extra/missing edges or decode failures,
but cannot rule out electrical problems or uniquely identify their cause. Shorten
leads, keep a nearby ground return, avoid long parallel clock/data runs, and use
an oscilloscope at the DAC pins when investigating voltage/edge quality. Changing
analyzer sample rate and USB cable helps distinguish capture artifacts from DUT
failures but is not an electrical qualification.

No firmware-controlled analog DAC mute was found. VCP mute zeros ordinary PCM;
it does not make arbitrary GPIO waveforms safe at the DAC. Disconnect downstream
headphones/amplifier or use a verified external mute for active identification.
Do not disconnect DAC power while continuing to drive its digital inputs.

## Fresh session discovery: never reuse role guesses

Distinct facts need distinct evidence:

1. **Probe and silicon:** fresh CMSIS-DAP/DP/AP/FICR evidence identifies candidate
   hardware, not its current source/receiver role.
2. **Serial endpoint and firmware:** USB ancestry plus a live firmware response
   binds UART endpoint, firmware role/build and boot instance to that candidate.
3. **Observed wiring:** fresh nonce in captured I2S/pin waveform binds that board's
   declared outputs to analyzer channels.
4. **DAC presence/function:** separate evidence, not implied by successful output.

Current helper lives in `~/repos/nix-nrf-dev`:

```sh
nix-nrf probes
nix-nrf probes PROBE_SERIAL_A PROBE_SERIAL_B
```

Commands above are verified interfaces, not executed for this document.
`nix-nrf probes --find nrf54l` cannot assign roles when two XIAOs match. The older
name `nrf-probes` persists in some docs and in JSON backend vocabulary; current
public helper is `nix-nrf probes`. Its text table does not expose the full AP map.

Reuse `scripts/hil/discovery.py` and `scripts/hil/session.py`: they supplement
helper output with raw explicit-probe OpenOCD fingerprinting, match USB/udev serial
identity, re-resolve tty paths, and reject ambiguity. Do not infer roles from
`/dev/ttyACM0`, an HCI index, enumeration order, previous probe serials or SoC family.

Proposed new flow: inventory unassigned candidates first, then challenge each
eligible diagnostic-capable candidate sequentially and bind the observed harness.
Compare live firmware role with wiring evidence. A source image attached to the
DAC harness is a **role mismatch**, not permission to silently flash it as receiver.
No waveform from a candidate means unverified, not proof that it is the source.
If identity fails, firmware lacks diagnostics, or multiple candidates match, stop
automatic assignment and report the evidence needed.

Once roles are established, use the existing session creation interface with
fresh verified serials:

```sh
python3 scripts/hil-runner.py create-session \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --session-id UNIQUE_SESSION_ID \
  --receiver-probe VERIFIED_RECEIVER_SERIAL \
  --source-probe VERIFIED_SOURCE_SERIAL \
  --session-root /tmp/opencode/hil-sessions
```

Run from repository root after inspecting binding and verifying external output
parent. This is a future hardware workflow, not something to run blindly while
writing docs. Revalidate candidate evidence during the transition to bound session.

Current `devices.json` is schema v1, strictly validated and immutable. Do not add
ad-hoc analyzer fields. Prefer separate versioned immutable `harness.json` plus
raw captures, bound to `devices.json` hash and role identity. Current logical
fixture `capture_capability: none|mono|stereo` describes **analog ALSA capture**,
not logic-analyzer support. Do not relabel it `stereo` because I2S is decoded.

Revalidate USB/probe binding and firmware boot identity before a new run. Recheck
nonce wiring after every session start, board swap, reset/reflash that invalidates
identity, or cable change. Never overwrite earlier evidence to update the mapping.

## Proposed onboard identification interface

**Design proposal, not runnable shell syntax.** Prefer firmware command over
OpenOCD register pokes: firmware can enforce stream lifecycle, ownership and cleanup.

Logical operations, with final wire schema to be fixed during refinement:

| Operation | Required observable response |
| --- | --- |
| `identity` | Request ID, firmware role/build, boot generation, hardware identifier, board profile, diagnostic capability |
| `identify_i2s` | Fresh host nonce, bounded duration, actual pin profile, start/result status; emits recognizable valid I2S pattern |
| `cancel` | Cancels owned diagnostic, restores hardware ownership, reports terminal state |
| `presence` | Explicit evidence and independent states for harness, rail, DAC presence and function; unknown when not observable |

Receiver shell adapter can build on `src/audio_shell.c`; standalone source needs
compatible diagnostic capability via its strict `hil` protocol or isolated command.
Neither currently implements these operations. Linux HCI firmware's H4 transport
is **not a shell**; never send text commands to it. Report unsupported firmware
role and require an explicit controlled image transition if diagnostics are needed.

### Preferred waveform: valid I2S with fresh identity challenge

- Host generates unpredictable per-attempt nonce, preferably 128 bits. Firmware
  emits framed nonce plus profile/identity tag and integrity check in a short,
  bounded, low-amplitude sample sequence, distinguishable left/right.
- Capture must be armed before emission. Host verifies nonce **from wire data**,
  not merely from UART echo. Decode possible BCLK/WSEL/DIN assignments, require
  correct clock ratios and payload framing, and return one unambiguous mapping.
- Use the existing output owner or a safely acquired diagnostic owner. Emit exact
  diagnostic words at the post-ASRC I2S boundary, not through a variable resampler
  that would change nonce bits. This is fixture identification, not PCM-quality test.
- Pattern amplitude, duration and safe analog-load state require explicit limits
  before implementation. Low-amplitude content does not waive analog precautions.
- Reject active streaming or conflicting diagnostics without changing pins. An
  explicit controlled stop may precede retry. `audio stop` drains the audio path;
  it does not automatically transfer pinctrl ownership to arbitrary GPIO code.
- Bound execution locally so a disconnected UART/host cannot leave pins toggling.
  Timeout, cancellation, reset and errors must terminate safely; restore idle
  peripheral/pinctrl state, do not unexpectedly restart playback.

If individual GPIO signatures remain necessary, use a separate opt-in diagnostic
mode with exclusive peripheral release, verified external mute, and explicit
allowlist restricted to intended I2S pads. Each signature must carry fresh nonce
and pad ID. Never scan all GPIOs, toggle rails, or touch UART/SWD/RF-control pins,
RADIO, reserved timers or unallocated GRTC/GPPI resources. A GPIO signature proves
that pad path, not correct normal I2S pinmux; follow with valid I2S confirmation.

OpenOCD remains identity/debug fallback, not default waveform generator. Halting
CPU, driving pins behind an active peripheral, or restoring guessed registers
can disturb Bluetooth, DMA and output. No raw register-write recipe is approved here.

## DAC detection: honest contract

Three-wire I2S provides clocks and data **into** this DAC, with no ACK or identity
reply. Identical MCU output can be captured with DAC absent. Reading MCU output
levels back, watching CH3 high, or estimating input loading cannot reliably prove
DAC attachment or functionality.

Recommended status separates:

- `harness_binding`: verified / unknown / mismatch, backed by fresh captured nonce.
- `rail_observation`: high / low / unstable / unknown, from analyzer, not fabricated
  by MCU command when MCU has no sense input.
- `dac_presence`: confirmed / absent / unknown, with named method and evidence.
- `dac_function`: confirmed / failed / untested, requiring return-path evidence.

For current unchanged wiring, `presence` must be allowed to return **unknown**.
It can still identify the board driving the user-declared DAC harness and mark it
as the DUT candidate, with explicit physical-attachment attestation if available.
Do not claim automatic DAC detection or 100% electrical identity from this alone.

To require automatic physical presence confirmation, choose an additional mechanism:
connector sense/ID physically integrated with the DAC assembly, readable peripheral
identity on different hardware, or a safe analog return measurement. A loose ID
resistor/loopback identifies its harness, not a functioning DAC. Analog qualification
remains PB-023/PB-025; choosing a presence sensor is a refinement decision, not
authorization to modify hardware now.

## Capture and decode recipe

Firmware uses standard I2S, 16-bit stereo. Expect 32 BCLK periods per full LRCK
period, roughly 1.52 MHz BCLK and 47.6 kHz LRCK before measured clock variation.
Begin at **12 MS/s**, 1,200,000 samples (100 ms); 24 MS/s offers more timing
resolution but may strain FX2 USB streaming. For commanded ID, arm sufficient
duration to include UART dispatch and entire waveform; 100 ms is only a running-
audio smoke capture. Record acquisition losses/truncation and fail closed.

Offline/host-only commands already verified:

```sh
sigrok-cli --version
sigrok-cli --help
sigrok-cli --show -P i2s
```

Future hardware commands, **templates only**. Resolve installed device's actual
driver/channel identifiers and use explicit selected analyzer; do not scan unrelated
instruments. FX2 is a candidate driver, not a verified identity for this unit.

```sh
# LA_DRIVER: verified driver for this analyzer, e.g. fx2lafw if confirmed.
sigrok-cli --driver "$LA_DRIVER" --scan
# LA_DRIVER_SPEC: selected analyzer selector returned/supported by discovery.
sigrok-cli --driver "$LA_DRIVER_SPEC" --show

# LA_CHANNEL_SPEC: actual channel IDs renamed BCLK,WSEL,DIN,RAIL_OBS.
# RUN_DIR: new externally owned run directory, never an old evidence directory.
sigrok-cli --driver "$LA_DRIVER_SPEC" \
  --config samplerate=12m --channels "$LA_CHANNEL_SPEC" \
  --samples 1200000 --output-file "$RUN_DIR/i2s.sr"

# Offline decoding after capture; channel names must match saved capture.
sigrok-cli --input-file "$RUN_DIR/i2s.sr" \
  --protocol-decoders i2s:sck=BCLK:ws=WSEL:sd=DIN
```

For identification, preserve neutral physical channel names in the raw capture
and derive assignments rather than declaring desired mapping to be observed truth.
Run-duration limits, acquisition startup handshake, stderr/exit status, cancel and
process cleanup belong to the implementing host coordinator.

Minimum evidence: session/image/boot identity, analyzer VID/PID/serial or recorded
USB path if serial absent, driver/software versions, actual sample rate/count,
channel IDs, nonce, expected and observed map, raw `.sr` hash, command responses,
power/mute/ground checks, capture integrity and explicit verdict/reason. USB path
is session-local, not globally unique identity. Repeat fresh nonce after replug.

Digital capture does not measure analog noise or oscillator ppm accurately without
timebase characterization. Keep raw traces even if exporting WAV; exported WAV
does not preserve all timing information and may use a nominal rate.

## Next agent entry points and references

- `scripts/hil/discovery.py:336-378,443-607`: fresh probe/USB/tty resolution.
- `scripts/hil/session.py:206-312,711-879`: strict manifest and binding lifecycle.
- `scripts/hil/model.py:20-67,115-122`: fixture capability and backend vocabulary.
- `src/audio_shell.c:65-73,143-151`: shell/lifecycle seam.
- `hil/source/app/src/hil_source_app.c:2203-2255,2323-2335` and
  `scripts/hil/source_client.py`: source protocol adapters.
- `~/repos/nix-nrf-dev/bin/commands/nix-nrf-probes`: helper implementation.
- [PulseView manual](https://sigrok.org/doc/pulseview/unstable/manual.html): FX2
  capture limits, decoder requirements and I2S export.
- [Adafruit UDA1334A pinouts](https://learn.adafruit.com/adafruit-i2s-stereo-decoder-uda1334a/pinouts):
  input-only I2S, regulator output and hardware mute; current breakout still unverified.

 Current harness does **not** observe source HCI UART RX P1.8. Do not treat this
 I2S setup as closing PB-019's UART-wire observability requirement. See the
 PB-019 report's preserved observability boundaries
 (`docs/development/pb-019-hci-resume-results.md`; the 2026-09-25
 observability snapshot this line cited was retired at Git rev `a94f010`).

## 2026-09-27 observed passive I2S session (addendum)

Earlier statements about no scan/capture and unverified wiring above describe
the 2026-09-25 documentation state, not the later session. See
[I2S bring-up results](../development/logic-analyzer-i2s-bringup-results-20260927.md)
for raw run roots, hashes, strict HIL counters and diagnostic failure history.
User authorized normal streaming/passive capture, confirmed common ground and
input ratings for all connected channels including rail, and confirmed
downstream audio disconnected **or** externally muted; which option was used
was not recorded. No voltage was meter-measured, and exact analyzer model and
electrical condition remain unattested beyond that safety confirmation.

USB `0925:3881` enumerated via FX2/Saleae Logic profile, with eight channels
D0-D7 and generic non-unique serial. `fx2lafw` volatile load changed USB
address 13 to 14. Refresh device selector after each scan/re-enumeration;
do not preserve bus addresses or probe-to-role associations as fixture identity.
Driver advertised up to 48 MS/s, but only 12 MS/s / 100 ms acquisitions were
tested. Initial idle D0-D2 low, D3 high, no frames; three normal 120 s rows
then provided six 100 ms `.sr` windows. D0-D3 are raw physically named channels
in saved captures; observed geometry is consistent with declared wiring, not
proof from fresh nonce or a DAC-presence test. Every complete halfword has 16
BCLK rises and aligned I2S decoder has no warnings. The original Mode A raw
decode warning is retained: capture began partway through a left word; only
after full raw geometry validation was an analysis copy aligned to observed
WS edge. Do not suppress generic warnings or alter original `.sr` files.

D3 high is digital threshold only, not measured voltage or power-good. Clock
rates from nominal uncalibrated analyzer timebase are not ppm or latency
measurements. Six windows total only 0.6 s, not continuous 120 s wire proof;
decoded words are not codec/analog quality or DAC-function evidence. No WAV
was used: installed `i2s/pd.py` WAV header hardcodes 16 kHz / 32-bit instead
of this nominal 48 kHz / 16-bit format. PB-041 automatic nonce identification
remains proposed, not implemented. Existing bound manifest was reused only
after six ordered fresh identity checks; archived IDs never authorize a new
session. Source UART P1.8 was not tapped and PB-019 is still open.
