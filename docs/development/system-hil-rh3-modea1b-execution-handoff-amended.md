# RH3-ModeA1b offload-disabled rerun handoff (amended)

Status: approved one-run hardware diagnostic. This amendment replaces the
absolute-hash pins of the original `system-hil-rh3-modea1b-execution-handoff`
which are structurally impossible to satisfy: `cmake/version.cmake` embeds
`APP_COMMIT` (git rev-parse of HEAD) into `app_commit.h`, so every CPUAPP
image hash is HEAD-dependent. Pins recorded at an earlier commit can never
match after a new commit. The first ModeA1b attempt correctly stopped on this
mismatch before touching hardware; no hardware run was consumed.

## Identity contract (replaces absolute pins)

Image identity for this run is established at execution time with a
reproducibility proof:

1. Record `git rev-parse HEAD` and `git status --porcelain` (must be clean).
2. Build the diagnostic image; record
   `diag_cpuapp=$(sha256sum .../zephyr.hex)` and FLPR hash.
3. Rebuild the SAME diagnostic image a second time (`--pristine` in the
   helper) WITHOUT any repo change in between; the two CPUAPP hashes must be
   byte-identical. If they differ, stop: non-deterministic build is a defect.
4. The runner's own `images.json` in the evidence root is then the
   authoritative record of what was flashed; the result doc cites those
   hashes plus the HEAD commit.
5. Normal-image identity for restoration: rebuild normal after the run under
   the same clean HEAD, double-build determinism not required for the
   restoration build (it is local output only, never flashed in this phase),
   but record its hash and `CONFIG_AUDIO_OFFLOAD_ASRC=y` proof.

Everything else in the original handoff (goal, binary isolation structure,
run ID `rh3-modea1b-20260903-offload-disabled`, row, `--allow-offload-disabled`
invocation, review/classification procedure, restoration requirements,
documentation and commit message) is unchanged. The stale `e67265c1...` /
`7dabfdb2...` / `91223b35...` absolute pins are void; the result doc must not
cite any pin that was not derived under the run's own HEAD.

## Extra documentation duty

The canonical result doc must state the HEAD commit and the derived hashes
with the double-build proof, and the resume-state update must note that
CPUAPP image hashes are HEAD-dependent (commit-embedded version), so future
handoffs derive identities at execution time instead of pinning absolutes.

## Execution sequence (delta only)

After preflight (disk/git-clean/run-ID/path-ownership/fixture/source-hash):

```bash
git rev-parse HEAD   # record
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-offload-disabled.conf"
sha256sum build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex
# determinism proof: identical second build
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-offload-disabled.conf"
sha256sum build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
# prove required config lines (offload unset, ASRC linear, traces unset, RX 3)
```

Then the single runner invocation exactly as the original handoff (with
`--allow-offload-disabled`), read-only review, binary classification,
restoration (normal rebuild + `CONFIG_AUDIO_OFFLOAD_ASRC=y` + FLPR/source
hashes which are commit-independent... FLPR source tree unchanged so its hash
should be stable; verify, and source images are separate builds owned by the
runner), documentation, and the same commit message
`docs(hil): record Mode A offload-disabled rerun outcome`.
