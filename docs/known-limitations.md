# Known limitations

An honest list of current gaps, caveats, and open questions. Current product
work is tracked in the [product backlog](product/README.md); this document
records limitations, not task status.

## 1. 48 kHz only

The receiver currently supports a single sample rate: **48 kHz**. PACS
capability advertisement, codec-config validation, the LC3 decoder, and the
I2S/sink path all hardcode 48 kHz. A source that negotiates any other rate is
rejected. Multi-rate support is tracked in
[PB-001](product/backlog/tasks/pb-001%20-%20Support-additional-LC3-sample-rates.md).

## 2. nRF54L15: 7.5 ms / 360-frame streams fall back to CPU ASRC

On the nRF54L15, the FLPR (secondary processor) ASRC offload contract takes
**480 input frames** per call. Stream shapes that produce a different call
size (specifically **360-frame (7.5 ms)** calls) fall back to the identical
cpuapp ASRC implementation. This is a documented behavior, not a failure; the
audio still plays, just without the FLPR offload for those streams. 7.5 ms
transport is hardware-validated end to end (see the developer documentation
on the System HIL track); the FLPR simply does not participate in the rate
conversion for those streams.
Policy decision is tracked in
[PB-013](product/backlog/tasks/pb-013%20-%20Decide-360-frame-FLPR-offload-policy.md).

## 3. Volume curve is linear

Volume is currently applied as **linear PCM scaling** (each sample multiplied
by volume/255). Reported behavior: the first roughly **third of the volume
slider** causes most of the perceived loudness change, and adjustments near
the top of the range are barely audible. The mechanism is the linear scaling;
no specific psychoacoustic cause is asserted. A perceptual/logarithmic curve
is tracked in
[PB-002](product/backlog/tasks/pb-002%20-%20Use-perceptual-fixed-point-volume-mapping.md).

## 4. Startup pop after long idle (observed, root cause unconfirmed)

An audible pop has been observed when playback starts after a long idle
period. The root cause is **not yet confirmed**. A likely area to investigate
is DAC/I2S idle and power sequencing (stop/start order, startup prefill,
silence/ramp/mute sequencing), but this is an investigation direction, not a
confirmed cause. Tracked in
[PB-003](product/backlog/tasks/pb-003%20-%20Eliminate-long-idle-resume-pop.md).

## 5. Duplicate BONDING advertisements (observed, root cause unconfirmed)

During BONDING, **two scanner entries / advertisements have been observed**,
and only one of them pairs. The root cause is **unconfirmed**; it may be an
advertising-set, identity/address, or central-side discovery artifact. The
invariant we want is exactly one connectable receiver advertisement at any
time. Tracked in
[PB-004](product/backlog/tasks/pb-004%20-%20Remove-duplicate-BONDING-advertisements.md).
This bug is distinct from the intentional NORMAL/BONDING advertising-payload
distinction.

## 6. Source-device availability limits

LE Audio source devices are still uncommon, and classic Bluetooth audio
capability does not imply BAP unicast-source support. Finding a consumer
device that can actually stream to this receiver may be the hardest part of
using it. See [Supported source devices](user-guide.md#supported-source-devices)
and the researched Linux source hardware in [Supported LE Audio sources on
Linux](supported-sources.md).

## 7. Release binaries not yet published

Ready-made firmware binaries are planned but **not yet available**. The
firmware currently must be built from source with the developer toolchain.
Release path is tracked in
[PB-006](product/backlog/completed/pb-006%20-%20Create-replacement-nRF54L15-release-candidate.md),
[PB-007](product/backlog/tasks/pb-007%20-%20Accept-exact-candidate-through-RH4-and-FR4.md),
[PB-008](product/backlog/tasks/pb-008%20-%20Provide-public-friendly-nRF54L15-flashing.md),
and [PB-009](product/backlog/tasks/pb-009%20-%20Publish-first-public-firmware-release.md).

## 8. nRF54L15 scope and Nordic guidance caveat

The nRF54L15 build supports **point-to-point** LE Audio, but the chip has **no
dedicated Audio PLL**, and Nordic's official position is that the nRF54L
series is therefore **not its ideal/recommended platform for all LE Audio /
audio-streaming uses**: Nordic states that a subset of LE Audio use cases can
be supported, and recommends the nRF5340 for audio today. This is a scope
caveat, not a claim that Bluetooth ISO/BAP is impossible. Not every LE Audio
use case (for example TWS-style synchronized playback between two earbuds)
is covered. See [Technology: nRF54L15](technology/nrf54l15.md) for the
architecture, Nordic's guidance with citation, and what this means in
practice.
