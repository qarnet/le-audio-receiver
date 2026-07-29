# BabbleSim Stage 0 — pinned environment and official baseline

## Goal

Provision NCS v3.3.0-pinned BabbleSim components without installing host
packages, build simulator, and pass official Zephyr BAP unicast radio test.

## Exact procedure

1. Record current west manifest group filter.
2. Temporarily enable `+babblesim`, run `west update` only for inactive
   `babblesim_*` component projects listed by NCS manifest. Do not update NCS,
   Zephyr, nrfx, or other revisions. Restore original group filter afterward.
3. From `${ZEPHYR_BASE}/../tools/bsim`, run repo Makefile `make everything` with
   bounded parallelism. No package manager, installer, downloaded script, sudo,
   or revision change.
4. Set:
   - `BSIM_OUT_PATH=${ZEPHYR_BASE}/../tools/bsim`
   - `BSIM_COMPONENTS_PATH=${BSIM_OUT_PATH}/components`
   - `BOARD=nrf5340bsim/nrf5340/cpuapp`
5. Run official focused audio compile script and official
   `bap_unicast_audio.sh`. Require client+server+PHY processes exit success.
6. Preserve logs and component commit IDs.

Add repo `scripts/bsim-env.sh` that derives NCS root from current `ZEPHYR_BASE`
and exports only variables above; it must fail with clear message when component
binaries are absent. Add `scripts/bsim-official-smoke.sh` invoking pinned official
compile/run paths. No Twister claim.

Document setup commands and why dedicated scripts replace Twister for radio
tests. Commit repo scripts/docs only after baseline passes. No application/test
scenario edits yet, no unpinned downloads, push, security changes, or hardware.
