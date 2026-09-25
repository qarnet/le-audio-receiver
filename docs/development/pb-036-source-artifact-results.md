# PB-036 / PB-035: source artifact archive slice (2026-09-24)

## Scope and result

Host-only source packager and RH4 archive resolver now agree on source schema
version 2: board `nrf54l15dk/nrf54l15/cpuapp` (the XIAO source uses the DK
target plus its wiring overlay), pinned source NCS `v3.3.0`, firmware ID
`le-audio-hil-source-rh1`, and exactly one `cpuapp` image. The input build
member is `zephyr/zephyr.hex`; the ordered ZIP members are `cpuapp.hex`,
`source-manifest.json`, `SHA256SUMS`. CPUAPP flash order is 0. The source
commit stays a caller-supplied canonical 40-character lowercase hex string;
the receiver and source commits may differ. This does not establish clean
build provenance or validate any real dirty-worktree build as accepted.

Manifest keys remain `board`, `firmware_id`, `git_commit`, `images`,
`ncs_version`, `schema_version`. Each image still carries `filename`,
`flash_order`, `role`, `sha256`, `size`. ZIP metadata, byte-level checksum,
input and path protections, atomic no-clobber output, external staging and
revalidation remain enforced. Resolver rejects the old, valid-checksummed
dual-core v1 archive, incorrect version/board/firmware ID/commit, image
role/order/hash/key drift, extra or missing members, invalid HEX, changed
archive, and changed staged image before use. Receiver artifact contract is
unchanged. Archive-internal hashes are consistency checks; they do not alone
prove external source provenance or establish clean-commit acceptance.

## Host checks

Commands on dirty primary tree, no firmware build, flashing, or output image
changes:

```text
nix develop -c python3 scripts/test_package_hil_source_artifact.py
  4 tests, OK
nix develop -c python3 -m pytest -q tests/hil/rh4_artifact_test.py
  15 passed
git diff --check
  exit 0
```

## Remaining integration (not accepted)

`scripts/hil/runner.py` still restricts RH4 artifact mode to nRF5340 source
and passes dual-core source overrides to the legacy helper. Next coordinated
PB-035/PB-036 slice must update runner image/flash selection, source flash
helper and its tests, active capture fixtures, schema/capture/build/public
guidance, then run representative exact-artifact physical RH4 acceptance with
immutable evidence. Existing physical matrix is running with fixed images;
none of those paths or images were changed in this slice. PB-035 full physical
matrix and clean-commit source artifact provenance remain pending. Do not
claim PB-036 criteria or RH4 physical acceptance from host-only tests.

## 2026-09-25 continuation boundary

The earlier runner restriction above describes this slice, not the current
tree. Later runner/default/capture changes and tests are present on the dirty
primary branch; diagnostic RH3 matrix passed with fixed source/receiver images.
No clean-commit source archive provenance or physical **exact source artifact**
RH4-compatible acceptance was performed. Dirty-tree baseline enforcement
stopped before builds; a clean exact commit requires explicit local commit
authorization. Do not substitute the fixed-image RH3 matrix or host resolver
tests for AC4. See `nrf54l15-only-continuation-20260925.md`.
