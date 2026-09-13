# RH3-14 nRF54L15 devicetree warning repair handoff

Status: software-only repair before the authorized RH3-14 trace execution. No
physical HIL, flash, reset, serial, Bluetooth, RF, or power action is allowed
in this phase.

## Trigger

RH3-14 preflight built both trace and normal nRF54L15 images successfully, but
stopped before the runner because dtc emitted these unexpected warnings:

```text
unit address and first address in 'reg' (0x20030000) don't match for /memory@20028000
Warning (simple_bus_reg): /soc/reserved-memory: missing or empty reg/ranges property
Warning (avoid_unnecessary_addr_size): ... unnecessary #address-cells/#size-cells ...
Warning (simple_bus_reg): /soc/memory@20028000: simple-bus unit address format error, expected "20030000"
```

The runner was not invoked. No RH3-14 evidence directory, JUnit, flash, reset,
or hardware action exists. Normal local nRF54L15 output was restored after the
blocked preflight.

## Grounding and fixed design

The project currently places shared ICMsg leaves under `/soc/reserved-memory`
in both CPUAPP and FLPR overlays. `/soc` is a `simple-bus`, so that container is
not a valid bus child. Standard Zephyr reserved memory belongs at root:

```dts
/ {
	reserved-memory {
		#address-cells = <1>;
		#size-cells = <1>;
		ranges;
	};
};
```

Installed NCS reference:
`nrf/snippets/hpf/gpio/icmsg/soc/nrf54l15_cpuapp.overlay` and
`nrf54l15_cpuflpr.overlay`. The ICMsg binding requires only `tx-region` and
`rx-region`; do not add `no-map`.

The Nordic `nordic-flpr` CPUAPP snippet declares
`cpuflpr_sram_code_data: memory@20028000`, while the project changes its `reg`
to `0x20030000` plus `0x10000`. The FLPR board similarly declares
`cpuflpr_sram: memory@20028000` before the project changes its `reg`. A node
unit address must match the first `reg` address. Delete and redeclare both as
`memory@20030000`, preserving labels and every memory address, size, and
phandle relationship.

Memory layout is fixed:

```text
0x20000000..0x20028000  CPUAPP SRAM, 160 KiB
0x20028000..0x2002A000  shared RX/TX region, 8 KiB each
0x2002C000..0x20030000  shared PCM ring, 16 KiB
0x20030000..0x20040000  FLPR execution SRAM, 64 KiB
```

## Exact implementation

### CPUAPP shared leaves

In `boards/nrf54l15dk_nrf54l15_cpuapp.overlay`, move the existing `sram_rx`,
`sram_tx`, and `pcm_ring` declarations from `/soc/reserved-memory` to root
`/reserved-memory`. Retain labels, `reg` values, IPC `tx-region`/`rx-region`,
mailboxes, and `dcache-alignment` exactly. Root reserved memory must contain
`#address-cells = <1>`, `#size-cells = <1>`, and empty `ranges;`.

### CPUAPP FLPR snippet replacement

In `boards/nrf54l15dk_nrf54l15_cpuapp_vpr_memory.overlay`:

1. Delete snippet-created `&soc` child `reserved-memory`.
2. Delete `&cpuflpr_sram_code_data`.
3. Add `cpuflpr_code_partition: image@165000` to existing root
   `/reserved-memory`, with unchanged `reg = <0x165000 DT_SIZE_K(96)>`.
4. Redeclare `cpuflpr_sram_code_data: memory@20030000` under `/soc` with
   unchanged `compatible = "mmio-sram"`, `reg = <0x20030000 DT_SIZE_K(64)>`,
   address/size cells, and `ranges = <0x0 0x20030000 0x10000>`.
5. Retain the existing `&cpuapp_sram` 160 KiB override.

`&cpuflpr_vpr` must still resolve `execution-memory` to
`&cpuflpr_sram_code_data` and `source-memory` to `&cpuflpr_code_partition`.

### FLPR shared leaves and execution SRAM

In `src/flpr/boards/nrf54l15dk_nrf54l15_cpuflpr.overlay`:

1. Move shared `sram_tx`, `sram_rx`, and `pcm_ring` leaves to root
   `/reserved-memory` with cells and empty `ranges;`.
2. Delete inherited `&cpuflpr_sram` and redeclare the same label at root as
   `memory@20030000`.
3. Preserve `compatible = "mmio-sram"`, `reg = <0x20030000 DT_SIZE_K(64)>`,
   cells, `ranges = <0x0 0x20030000 0x10000>`, and `status = "okay"`.
4. Preserve swapped FLPR IPC region references and VEVIF mailbox wiring.

Do not change `sysbuild.cmake`, NCS snippets, source C, linker layout, memory
sizes, Kconfig, HIL runner behavior, or the physical fixture.

### Build-contract regression coverage

Update `scripts/check-build-contract.py` and
`tests/unit/build_contract/test_build_contract.py` without changing the
96-assertion contract count:

- Existing `54l15-025` checks must also reject a shared-memory leaf outside
  root `/reserved-memory` and reject a leaf whose unit address differs from its
  first `reg` address.
- Existing `54l15-026` must require `cpuflpr_code_partition` under root
  `/reserved-memory` as well as its unchanged `reg`.
- Existing `54l15-027` must require FLPR `cpuflpr_sram` to have both the
  unchanged `reg` and matching `memory@20030000` unit address at root.
- Update synthetic CPUAPP and FLPR DTS fixtures to the corrected shape.
- Add negative unit-address and nested-reserved-memory coverage. Keep existing
  interval-missing and overlap coverage valid.

Update `docs/testing/behavior-contract.md` BUILD-005 wording to state that
shared leaves are root reserved-memory children and resolved unit addresses
match their first `reg` address. Do not change unrelated historical counts.

## Verification

Run sequentially from repository root. No hardware actions.

```bash
python3 tests/unit/build_contract/test_build_contract.py
python3 scripts/check-test-matrix.py --repo-root .
git diff --check

nix develop --command fw-build-54l15
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
```

The normal nRF54L15 build must have no unexpected dtc warning, especially none
of the four trigger signatures. Verify resolved CPUAPP and FLPR DTS nodes,
labels, unit addresses, parent paths, and all memory ranges. Record exact new
normal CPUAPP and FLPR SHA-256 hashes.

Then build the trace image without hardware:

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-hci-remove-iso-path.conf"
```

Verify all trace configuration requirements from
`system-hil-rh3-14-prompt-prefixed-drop-parser-trace-execution-handoff.md`,
the unchanged source hashes, the fragment hash, no unexpected dtc warnings, and
record exact trace CPUAPP and FLPR SHA-256 hashes. Restore normal nRF54L15
output with `fw-build-54l15`, verify the recorded normal hashes, trace gate
off, HCI log levels 3, and no trace source in map/autoconf.

Only after exact normal and trace hashes are known, update the unexecuted
RH3-14 execution handoff with those new values. Do not alter its fixed run ID,
scope, output paths, or one-run authorization. Do not execute its runner
command in this repair phase.

## Worktree rules

Worktree is intentionally dirty. Touch only files named here plus the unrun
RH3-14 execution handoff for verified hash updates. Do not stage, commit,
push, merge, tag, release, reset, stash, clean, delete evidence, or alter
`STATUS.md`.
