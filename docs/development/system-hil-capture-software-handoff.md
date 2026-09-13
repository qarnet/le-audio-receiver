# System HIL MA0/MA1 and SA0/SA1 software foundation handoff

Status: host-only implementation handoff. Builds capture tooling and synthetic
oracle validation only. Does not qualify hardware or claim output acceptance.

## Goal

Implement complete optional mono/stereo capture capability behind existing HIL
runner/matrix seams:

- strict capture fixture/binding and accepted-qualification schemas;
- owned direct-ALSA capture process with exact format and cleanup;
- deterministic NumPy analyzer grounded in source signal contract;
- synthetic defect fixtures proving each required metric fails for right reason;
- capture-enabled fixed two-pass matrix commands requiring external accepted
  qualification limits;
- complete evidence integration with no acceptance claim unless real capture
  passes frozen accepted limits.

## Scope

- `flake.nix`: add pinned `alsa-utils` and `python3Packages.numpy` only.
- `scripts/hil/model.py`: capture binding schema.
- New focused capture modules under `scripts/hil/`.
- Runner/matrix/CLI capture integration.
- Checked-in mono/stereo logical fixture examples and local binding examples.
- Host fake/synthetic tests outside hardware execution.
- HIL status docs.

No live ALSA, USB, audio, probe, flash, serial device, Bluetooth, sudo, RF,
hardware qualification, electrical connection, release, publication, or
acceptance run. No invented numerical limits. No commit.

## Grounded signal contract

Exact source facts from `hil/source/core/hil_source_types.h` and
`hil_source_signal.c`:

- 48,000 Hz, signed 16-bit PCM;
- left carrier 997 Hz, right carrier 1601 Hz;
- six preamble segments, each 11,520 samples (240 ms): silence, left-only,
  silence, right-only, silence, both;
- preamble total 69,120 samples (1.44 s);
- scored deterministic amplitude envelopes use 5,760-sample (120 ms) blocks,
  240-sample (5 ms) ramps, xorshift32, seed `0x48A31C5D`, channel masks and
  amplitudes from public constants;
- mono semantic source is left carrier, receiver duplicates it to analog L/R;
- tail is five seconds encoded silence.

Analyzer must recompute expected signal from these public constants. Do not
copy private output hashes.

## Fixture and binding schemas

Keep schema version 1 because no capture binding has shipped.

Logical capture role already uses:

```json
"capture": {"kind":"alsa_capture","channels":1}
```

or channels 2.

Extend physical binding role `capture` with exact shape:

```json
{
  "backend": "alsa",
  "device": "hw:CARD,DEV",
  "channels": 1,
  "sample_rate": 48000,
  "sample_format": "S16_LE",
  "udev": {
    "ID_VENDOR_ID": "0d8c",
    "ID_MODEL_ID": "0014",
    "ID_SERIAL_SHORT": "..."
  },
  "mixer": {
    "control": "Mic",
    "volume": "FROZEN_VALUE",
    "capture_switch": "on",
    "agc_control": "Auto Gain Control",
    "agc": "off"
  },
  "fixture_metadata": "/absolute/outside/repo/capture-fixture.json"
}
```

Stereo same with channels 2. Device must match strict
`^hw:[A-Za-z0-9_.-]+,[0-9]+$`; never `default`, `plughw`, Pulse/PipeWire, or
arbitrary command text. Udev identity requires vendor/model and serial/path.
Mixer fields are data, never shell fragments. Fixture metadata is strict JSON,
regular non-symlink canonical path outside repo, containing capture hardware,
DAC, cable/network schematic identifier, measured attenuation/channel mismatch,
electrical review identifier/date, USB path, kernel/ALSA versions, and notes.
No static `/dev/snd/*` path.

Provide `tests/hil/fixture-mono.json`, `fixture-stereo.json`, and gitignored
binding examples with placeholders. Base `none` fixture behavior unchanged.

## Qualification schema

New strict canonical JSON schema version 1. Required root:

```text
schema_version, status, capability, fixture_id, capture_identity,
fixture_metadata_sha256, qualification_runs, limits, accepted_by,
accepted_at_utc
```

Rules:

- status exactly `accepted`;
- capability mono/stereo matches fixture;
- fixture/capture identity exactly matches resolved binding;
- metadata hash matches current external fixture metadata;
- at least two qualification runs, each 130 seconds, with WAV SHA-256,
  analyzer result SHA-256, timestamp, and pass outcome;
- canonical UTC timestamps;
- limits contain every metric used by selected capability, finite JSON numbers,
  explicit min/max as semantically applicable, no NaN/Infinity, no unknown or
  missing metric;
- accepted_by nonempty human-controlled identifier;
- qualification file and all referenced qualification evidence are regular,
  non-symlink canonical paths outside repo and hash verified;
- runner never writes or auto-accepts qualification files.

Mono required limit keys:

- sample/frame/duration exactness and scored interval bounds;
- preamble correlation minimum and unique-peak margin minimum;
- expected carrier presence minima and unexpected-carrier maximum;
- envelope correlation minimum, lag absolute maximum, lag-drift maximum;
- peak/full-scale count/clipping maximum, noise-floor maximum, DC-offset maximum;
- moving-RMS minimum, low-energy-window count maximum, longest low-energy run
  maximum, repeat-variation maximum.

Stereo adds per-channel versions plus:

- channel-map margin minimum;
- opposite-channel leakage maximum;
- duplication correlation maximum;
- level mismatch maximum;
- per-channel continuity/lag limits.

No numerical defaults in production code or examples. Placeholder examples
must be rejected as unaccepted.

## Capture process ownership

Add `capture.py` with injected command/process boundaries.

Production flow:

1. resolve exact ALSA card/device identity using `arecord --list-devices` and
   `/sys/class/sound`/udev evidence; require exactly one binding match and exact
   direct `hw:` endpoint;
2. query mixer controls with `amixer -c CARD ...`; require exact frozen values,
   AGC off, capture enabled; never mutate controls in runner;
3. launch exact argv, no shell:

```text
arecord -D hw:CARD,DEV -t wav -f S16_LE -r 48000 -c N --period-size 480 --buffer-size 1920 OUTPUT.partial.wav
```

4. register stop cleanup immediately; capture starts before source START and
   must remain active through source teardown/tail;
5. stop with SIGINT, wait bounded, kill only after timeout, require expected
   arecord termination semantics, atomically rename partial to final only after
   valid complete WAV;
6. any xrun, short read/file, stderr warning/error, process failure, USB reset,
   format drift, cleanup failure, or repaired/truncated WAV fails;
7. keep partial WAV and stdout/stderr evidence on failure; never overwrite.

No capture object for capability `none`.

## Analyzer

Add `capture_analyzer.py`, pure NumPy plus stdlib WAV parsing. Reject non-RIFF,
extra/duplicate malformed format chunks, compressed/extensible unsupported
format, wrong rate/channels/width, truncation, trailing corruption, impossible
sizes, or duration shortfall.

Public input: WAV path, row, capability, qualification. Public output canonical
JSON metrics plus per-metric `{value, limit, pass}` and overall pass/fail.

Algorithm:

- decode S16_LE to normalized float64 only for analysis;
- locate unique preamble/scored boundary by normalized correlation against six
  energy/carrier segments, not process start;
- use Hann-windowed coherent/noncoherent spectral projection around exact 997
  and 1601 Hz with frozen bin-neighborhood definition recorded in result;
- reconstruct deterministic scored amplitude envelopes from row seed/public
  xorshift constants; correlate windowed RMS/envelope at 120 ms cadence;
- compute lag and lag drift, moving RMS, low-energy runs, clipping/full-scale,
  peak, DC offset, noise floor, carrier powers/margins;
- stereo computes per-channel metrics, map margin, leakage, duplication,
  mismatch, continuity;
- mono mode expects 997 only; mono aggregate Mode A/B expects both carriers;
- no metric silently omitted. Nonfinite intermediate fails.

Result retains algorithm/schema version and all analysis constants.

## Synthetic oracle validation

Add deterministic WAV generator in tests using public signal constants. Cover
mono and stereo clean captures with gain and bounded timing perturbations.
Every named defect must fail expected metric:

- silence;
- missing expected carrier;
- unexpected carrier for mono;
- clipping;
- truncation;
- DC offset;
- clock/lag drift;
- brief and long dropout;
- discontinuity;
- stereo swap;
- stereo duplication;
- one-channel loss;
- leakage;
- level mismatch.

Use explicit test qualification limits derived solely for synthetic fixtures;
label synthetic and never reusable for hardware. Tests assert public result
metric names/reasons, not private helper calls.

## Runner and matrix integration

Extend one-row runner with optional already-validated capture session factory
and qualification. Capture starts immediately before source `start` command,
stops after source terminal/receiver summary, then analyzer runs. Capture
failure changes row outcome. Evidence adds:

- capture identity/mixer/fixture metadata;
- raw WAV and SHA-256;
- arecord stdout/stderr/argv/status/timing;
- qualification copy/hash;
- analyzer canonical JSON;
- summary capture result.

Add fixed commands:

```text
run-ma1-matrix --fixture ... --binding ... --qualification ...
run-sa1-matrix --fixture ... --binding ... --qualification ...
```

Both use existing fixed two-pass RH3 schedule. MA1 requires mono capability;
SA1 requires stereo. No arbitrary thresholds, analyzer options, ALSA device,
row, skip, or repeat CLI arguments. Local/artifact firmware mode remains
separate; capture commands use current local build images unless later exact
artifact capture command is explicitly planned.

Aggregate matrix passes only when every transport row and every capture oracle
passes. Result may emit verdict field only on full pass:

- MA1: `MONO_OUTPUT_SMOKE_ACCEPTED`;
- SA1: `STEREO_OUTPUT_ACCEPTED`.

Failed/cancelled results have verdict `none`. Never emit
`SYSTEM_AUDIO_ACCEPTED`. MA1 docs state no physical channel-order/stereo claim.
SA1 docs state functional stereo output, not calibrated fidelity.

## Tests

- strict capture logical/binding/metadata/qualification parsing and identity
  drift;
- capture process exact argv, lifecycle, timeout, cancellation, xrun/stderr,
  partial retention, no-overwrite, cleanup;
- WAV parser corruption matrix;
- every synthetic happy/defect case above;
- runner starts/stops capture at correct public lifecycle boundaries;
- qualification mismatch/missing/unaccepted blocks before hardware;
- matrix fail-fast, JUnit, evidence, verdict rules;
- `none`, RH2/RH3/RH4 behavior unchanged;
- no test invokes live ALSA/hardware.

## Verification

```bash
nix develop --command python3 -c 'import numpy; print(numpy.__version__)'
nix develop --command arecord --version
nix develop --command python3 tests/hil/capture_model_test.py
nix develop --command python3 tests/hil/capture_analyzer_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
python3 tests/hil/rh4_artifact_test.py
python3 tests/hil/rh3_matrix_test.py
python3 tests/hil/rh2_test.py
python3 -W error scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/rh2_hardware_test.py
python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
git diff --check
git status --short
```

No live capture. Stop/escalate on missing signal constant, unclear metric,
invented hardware limit, dependency warning, live-device requirement,
architecture invention, or test weakening. Return files, behavior, tests,
deviations, blockers, and no-commit status.
