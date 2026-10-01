# Passive I2S analyzer bring-up results, 2026-09-27

## Scope and safety

This records completed bounded diagnostic runs, not PB-041 fixture identification,
audio-quality qualification or HCI fault diagnosis. No additional hardware
operations occurred while writing this record. User authorized normal streaming
with passive capture,
confirmed common ground and analyzer input ratings for every connected channel
(including rail), and confirmed downstream audio disconnected **or** externally
muted. Evidence does not distinguish which of those last two precautions was
used. Actual analyzer model, electrical condition and input voltage were not
meter-measured. No firmware C changes, nonce feature, HCI adapter, or new
hardware operation belongs to this documentation step.

The initial idle 100 ms capture at
`/tmp/opencode/logic-analyzer-runs/20260927-i2s-passive-r1` showed D0-D2 low,
D3 high and no frames. USB `0925:3881` enumerated with the FX2/Saleae Logic
profile, eight driver channels D0-D7 and a generic, non-unique serial. Scan
loaded volatile `fx2lafw` and device re-enumerated address 13 to 14. Selectors
must be resolved again after each re-enumeration; never store static bus,
probe-serial or role mappings in repository docs. Driver advertised up to
48 MS/s, but only 12 MS/s for 100 ms was tested. That is not a verified
maximum, electrical rating or sustained capture claim.

Existing immutable session binding was reused after all six ordered fresh
runner identity checks. See each HIL run's `identity.json`,
`session-revalidations.jsonl`, `images.json` and raw logs for evidence; archived
IDs are not fresh authorization. The waveform matches the user-declared
wiring in [analyzer setup](../testing/logic-analyzer-setup.md), not a fresh
nonce proof of wiring or DAC presence. PB-041 remains Backlog; its automatic
fixture identification is not implemented.

## Three normal 48_4_1 physical rows

All three existing PB-035 frozen normal 120 s rows used the standalone XIAO
source and NCS v3.4.1 images. HIL runner results **passed** with clean cleanup:

| Row | Immutable HIL root | Source scored/submitted | Receiver counters at stop |
| --- | --- | --- | --- |
| Mono | `/tmp/opencode/hil-runs/la-mono-20260927-r4` | 12000 scored, 12644 submitted (one stream) | RX 12640, decoded 12655, PLC 15 |
| Mode A (two CIS) | `/tmp/opencode/hil-runs/la-modea-20260927-r1` | 12000 scored, 12644 submitted **per stream** | RX 12645/12644; global decoded 25314, PLC 25 |
| Mode B | `/tmp/opencode/hil-runs/la-modeb-20260927-r1` | 12000 scored, 12644 submitted | RX 12644, decoded 25310, PLC 22 |

Mode A second-slot decoded/PLC zeroes in post-stop snapshot are global
counters reset/owned by first slot, **not** a claim of no PLC on second CIS.
All rows reported zero decode errors, I2S underruns, push failures, stream
resets and offload fallbacks; source final `sf=0`, `skip=0`, `out=0`.
These counters do not establish continuous wire integrity over 120 s.

Each row retained two **unaltered**, physically named D0-D3 `.sr` captures,
1,200,000 samples at nominal 12 MS/s (100 ms each). Six captures total 0.6 s:

| Row / analyzer root | First `stream-i2s.sr` SHA-256 | Repeat `stream-i2s-repeat.sr` SHA-256 | Aligned L/R pairs |
| --- | --- | --- | --- |
| Mono `/tmp/opencode/logic-analyzer-runs/20260927-i2s-mono-r4` | `72c9bbb4168825e12ae0ac6876915bd50f03911eb2fe2c62b46effe0bad3266e` | `f83f4419b87b61bfb0c78a3f335ee246af567ca95c4d6e2ecd55e42b58bbffdb` | 4773/4773 equal; 4773/4773 equal |
| Mode A `/tmp/opencode/logic-analyzer-runs/20260927-i2s-modea-r1` | `986dbafbfccbc2af9abfa83334c426742a0b28d33365e4bad77a329658969fb2` | `f1ba6ec8ed22b397913acf92109a338a1bc6fca5967bb985b8d3c7aa86c786a7` | 4767/4767 different; 4765/4765 different |
| Mode B `/tmp/opencode/logic-analyzer-runs/20260927-i2s-modeb-r1` | `986e2bb2363435d5862a7721552c2f3deefbeba3104057844f62ae6a4b67036a` | `eefdedd02fd2cb8964db4af50f0805d5d4d3ee1914fd4296b5fbc627a7379323` | 4767/4768 different; 4767/4767 different |

One coincidentally equal Mode B pair is normal, not an error. All **complete**
halfwords have 16 BCLK rising edges, thus 32 per full stereo frame. First and
last partial capture boundaries are excluded; aligned decoding gives zero
warnings. D3 stayed digitally high in each capture. Nominal analyzer-timebase
estimates are about 1.52 MHz BCLK and 47.7 kHz LRCK, **not** calibrated ppm,
latency, voltage, power-good, analog output, DAC presence, codec fidelity or
presentation conformance. Detailed counts, signed sample stats and crop offsets:
`/tmp/opencode/logic-analyzer-runs/20260927-i2s-observations-r1/final-v2.json`.
No WAV was used: installed `i2s/pd.py` WAV header hardcodes 16 kHz / 32-bit,
not this nominal 48 kHz / 16-bit capture format.

## Failure history and offline correction

Mono attempts r1-r3 stopped before flashing/streaming on tool PATH preflight;
those failed records remain intact. Host tool resolution used absolute paths
and appended host paths *after* Nix paths so Nix OpenOCD kept priority; actual
root tool check then passed. No firmware change was needed.

Original Mode A diagnostic supervisor verdict remains **FAILED** while its HIL
row **passed**. Raw decoder began mid-left-word with 13 bits and reported
`Received 16-bit word, expected 13-bit word` on first full right sample
113-239. Raw complete halfwords all have 16 edges. Offline v2 analysis first
checked **entire raw** complete-word geometry, then cropped an analysis copy
at first observed WS rising edge within 32 BCLK edges. Crop offset and raw
mapping are recorded per capture; `.sr` originals and original decoder logs
and verdict remain unchanged. Aligned decoder saw complete words with no
warnings. A synthetic regression on the installed real decoder reproduced
the mid-word artifact and rejected an interior 15-bit word, rather than
silencing warnings generally. No blind repeat hardware Mode A run followed.

## Last known state, limits and private checkpoint

After latest Mode B row, last **known**, not current verified, flashed image
tuple: receiver CPUAPP
`e02ab5f15213c05038d6e85cbcef585cf1a8db7f41b3653571dfcdb7f3a43aa6`,
receiver FLPR
`c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`,
standalone source CPUAPP
`805f2ed940a6fef965c51fc857bbcd7619b796df4e7f1b753978a2f5d198955c`.
Runner verified actual flash and matching pre/post hash snapshots. These are
PRIMARY diagnostic images, **not** clean-clone receiver `716d...` / HCI
`c210...` image proof. Source idle after run, no HCI adapter started; board
roles remained receiver and standalone source at last check. Re-identify
before any further target-changing action.

Private ignored checkpoint
`.session-checkpoints/2026-09-27-i2s-bringup/` mirrors three capture roots,
idle baseline, observation summary root and three HIL run directories. Its
`manifest.json` lists 944 files / 39,375,443 copied bytes with per-file
SHA-256; all copies and six `.sr` hashes verified, zero missing/unreadable or
non-regular entries, directories 0700 and files 0600. Keep checkpoint private:
it can contain identity, bond or raw session data. Do not stage, upload or
treat archived binding as live authority. Original `/tmp` runs remain
immutable. Earlier mono preflight failures remain at their original roots,
not in this bounded checkpoint.

Clean software gate 80/0/80 at `daf7cd9` was **not** rerun: production code
unchanged by this documentation step. Three rows are not full 20-row matrix,
7.5 ms, reconnect, fault, RH4/FR4, analog or public acceptance. PB-019 HCI
UART fault remains paused and unmeasured by these I2S taps. No backlog state,
firmware build, board operation, commit or release followed this evidence write.
