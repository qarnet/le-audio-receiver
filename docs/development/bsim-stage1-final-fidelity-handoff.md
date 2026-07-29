# BabbleSim Stage 1 final fidelity fix

## Production path fidelity

- Remove entire `CONFIG_TEST` invalid-SDU early return from `bt_bap.c`. BSim must
  execute same PLC/decode path as hardware.
- No other production behavior conditional for BSim.

## Honest startup oracle

- Before first nonzero PCM, count zero-energy pushes as `startup_zero` and
  snapshot production `audio_stats.plc_frames` after each.
- Zero-energy after first nonzero PCM is immediate FAIL.
- Count 100 nonzero valid pushes. At PASS require:
  - decode_errors=0;
  - malformed/after_stop=0;
  - startup_zero is bounded and deterministic across two runs (document actual);
  - all PLC frames, if any, occurred before first nonzero PCM;
  - `total_frames == nonzero_pushes + startup_zero` for one-frame-per-SDU case;
  - ordered hash nonzero/not seed and positive deterministic energy.
- Report `startup_zero`, `startup_plc`, final PLC, total, hash, energy. Do not
  relabel startup loss/warm-up as error-free PCM.

## Client/config/scripts

- Client `ASE_SRC_COUNT=0`; `ASE_SNK_COUNT=2` only because this Zephyr version
  requires nonzero client capacity >=2. Runtime still discovers/configures one
  remote sink and creates one TX stream/CIS.
- Remove stale NCS_ROOT double-dirname from scripts.
- Ensure official smoke exits nonzero on known upstream failure and docs say
  PARTIAL.

## Gate

Build and run receiver-specific Stage1 twice. All processes zero; client >=100;
receiver 100 nonzero pushes; invariants above pass with identical startup/hash/
energy across runs. Run both production target builds to prove removal of test
guard has no regression. Update results/status/design and commit. No further
scenario expansion, downloads/packages/NCS edits/push/hardware/security.
