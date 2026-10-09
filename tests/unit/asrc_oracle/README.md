# PB-045 independent ASRC arithmetic

Run `python3 tests/unit/asrc_oracle/test_asrc_oracle.py` from repository root.
The canonical inventory discovers this Python child automatically. A host C
compiler is required. Compilation uses `-Wall -Wextra -Werror`; errors are not
converted into successful negative-control outcomes.

The expected model uses `Fraction` global source coordinates, authored stereo
signals and independently expressed nearest-integer, ties-away-from-zero
rounding. It quantizes the nominal source increment to Q32, then the signed
ppm delta, as required by the fixed-point arithmetic contract. Expected samples
never come from production code, exported phase or private context fields.
Exact counts and samples are required: tolerance is zero for this quantized
linear-interpolation contract. This is not an unquantized continuous-ideal or
spectral-quality test.

The matrix covers four rate pairs (1:2, 1:1, 2:1 and 48000:47619), six signal
families and eight ppm settings. Each 1920-frame authored stream runs through
regular and irregular partitions. Control changes use fixed global input
offsets. Cold and live-history capacity failures, invalid ppm, reset and real
processor rejection/retry are checked through subsequent complete PCM output,
not private-state comparisons. Guarded output storage and raw produced counts
detect capacity overwrites and invalid count reporting.

`adapter.c` exposes opaque C contexts through a narrow host ABI and links real
`audio_asrc.c`, `flpr_audio_process.c` and ring CRC code. It transfers public
continuity tokens but computes no expected output. Its explicit CPU retry is
only a processor/API continuity test, not evidence of production fallback
selection. Host platform fences satisfy unused ring transport references; this
lane does not exercise shared-memory cache ordering.

The separate `audio_i2s_asrc_oracle` exec suite closes the caller boundary. It
links real sink, offload manager, ASRC, FLPR processor, rate converter and NONE
actuator. `generate_vectors.py` imports only the independent model, never runs
the DUT, and generates authored expected vectors into the build directory.
The fake I2S device exposes full driver-owned PCM before simulated DMA release;
every submitted startup-silence word and data word is checked. Scripted ppm is
an input stimulus, not a test of the PI controller. An in-process remote peer
replaces ring-manager transport only, processes real request PCM with the real
FLPR processor, and injects timeout and malformed post-state responses. Tests
prove actual sink-owned 360-frame fallback and 480-frame offload/fault/recovery
continuity. Public success/fallback counters distinguish remote success from
silent CPU fallback. Input duration stays fixed within each stream.

Compiled negative controls independently alter signed ppm rounding, PCM
rounding sign, ppm direction, boundary history, channel routing, frame advance,
capacity writes and produced-count reporting. Each altered C implementation
must fail the public PCM/count/guard comparison. Compile failures and mutation
anchor mismatches fail the suite instead of satisfying a negative control.

Physical CPUAPP execution of the known fractional rounding witness is separate
evidence. Native remote processing is not physical RISC-V execution, mailbox
transport, RF, I2S electrical timing, DAC fidelity or presentation proof. No
360-frame FLPR support is added. Existing physical HIL/PCM limits and coverage
baseline remain unchanged.
