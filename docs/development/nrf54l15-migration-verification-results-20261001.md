# PB-039/PB-038: all-nRF54L15 clean integration verification

## Scope and provenance

Clean tested checkpoint: `104e67ade0e361093a88d1832b5dcb41552e8e8d`.
Production code checkpoint: `e478e59df4597b6c7a6b4a56a3bb3e9995bdec16`.
The later commit adds partial IPC registration rollback/retry coverage and
documentation, not another firmware change. Validation clones are independent
of the primary implementation tree and its preserved PB-013 user edits.

Active SDK is NCS v3.4.1, toolchain `8285d8ad56`. Receiver is XIAO
nRF54L15 CPUAPP/FLPR; second XIAO alternates standalone source CPUAPP and
Linux HCI CPUAPP, never concurrently. Canonical simulation uses two integrated
SW Split nRF54L15BSim peers. Native/Python tests remain portable host tests.

This record proves local migration verification, not human PR acceptance,
hosted CI, active GitHub draft FR4, publication, analog measurement or audibility.
PB-013 360-frame FLPR support and PB-041 nonce identification remain separate.
Final repair/verification changed no SDK source, SAMD11 firmware, frozen
physical audio recipe or limit. Earlier approved BSim migration added measured
target-native startup recipes while retaining historical recipes and unchanged
payload/PCM/lifecycle contracts; it did not synthesize legacy startup loss.

## Integrated gates

| Boundary | Verdict | Retained evidence |
| --- | --- | --- |
| Clean canonical software | 80 PASS / 0 FAIL / 80 TOTAL | `/tmp/opencode/nrf54-104e67a-canonical-20261001-r1.log`, canonical root `/tmp/opencode/nrf54-104e67a-canonical-20261001-r1/` |
| Canonical BSim | 17 scenarios / 26 runs pass; strict TX hashes, PCM and lifecycle oracle unchanged | `/tmp/opencode/nrf54-104e67a-bsim-20261001-r1/` |
| Physical build shapes | Receiver CPUAPP/FLPR, standalone CPUAPP, HCI CPUAPP pass; resolved receiver contract 73/73 | `/tmp/opencode/nrf54-104e67a-physical/le-audio-receiver/build/`, `/tmp/opencode/nrf54-104e67a-artifacts-20261001-r1/validation.json` |
| HIL host suite | 340 passed / one intentional hardware-opt-in skip | Clean validation above; final primary host rerun `/tmp/opencode/nrf54-migration-final-host-20261001-r1.log` |
| Capture infrastructure | Real capture model/runner/analyzer host tests pass; immutable mono/stereo fixture snapshots retained | `/tmp/opencode/nrf54-capture-contract-evidence-20261001-r1/result.json` |
| Normal-image Linux HCI | Six 120-second fresh/bonded mono, Mode A and Mode B cases pass | `/tmp/opencode/nrf54-104e67a-hci-clean-20261001-r1/` |
| Frozen exact-local-artifact matrix | 20/20 pass, two complete passes; zero failed, cancelled or cleanup children | `/tmp/opencode/hil-runs/nrf54-104e67a-matrix-20261001-r1/` |

Canonical coverage enforces the existing 36-file population and surviving
per-file baseline. No ratio, denominator, recipe, test count, audio threshold
or source send margin was weakened. Receiver development builds retain the
specific documented `BT_CONN_TX_NOTIFY_WQ` experimental notice. Enabled-assert
CMake diagnostics and host-only unsupported-SoC notices remain raw; no general
compiler, Kconfig, runtime or CMake warning waiver is introduced.

## Exact image and archive identities

| Object | SHA-256 |
| --- | --- |
| Receiver archive | `ba7ebf6dbc7d9811d0fa6f2dd632305069b71c2ab127ff221af31d61ce532dfa` |
| Source archive | `1d0004d0c6fb73815e9dbc5d495b85377cb2bd57f94c23d5e34899b7787bc6b0` |
| Receiver CPUAPP HEX | `9adeae61e319bba1fe1f1ed529e2c98ade63d51bfb5bd56b35c8f6992da8e64f` |
| Receiver FLPR HEX | `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a` |
| Standalone source CPUAPP HEX | `57c836e3a70e6a1d3bbf6a15839fa879ef3548899322a05df4277e12b32a69c2` |
| HCI CPUAPP HEX | `243052e606c20ad65d79eee153521827102dcd76ecd297d3376cb7f99e0caefd` |

Public artifact extraction/revalidation passed before matrix launch. Archives
bind exact commit, NCS version, board, members and checksums. These are new
local candidates, not replacements for the existing private draft assets.

## Frozen physical matrix and independent evidence review

Unmodified `run-rh4-matrix` consumed both exact archives through the public
resolver. Ten frozen rows ran twice, including mono, Mode A, Mode B, preserved
bond, all three 7.5-ms modes, reconnect, FLPR hang and FLPR stall. All **20
children passed**; scheduled/attempted/passed are each 20, failed/cancelled are
zero and aggregate/child cleanup failures are empty. Source 3000/2000-us
margins, row recipes, transport/error/PLC limits and fault definitions stayed
unchanged. No row retry, skipped fault, diagnostic image or shortened duration
was substituted.

Independent read-only review:
`/tmp/opencode/nrf54-104e67a-matrix-evidence-review-20261001-r1.json`.
It rehashed every listed child payload plus 12 aggregate payloads, rechecked
archive identities and per-child exact commit/NCS/image inventories, verified
the same external session snapshot in every child, and verified **120 ordered
identity checkpoints**, six per child with distinct paired roles and retained
raw DP/AP/FICR evidence. Aggregate result SHA-256:
`112a69acddb1830bb1bcb4382621db4688216084da5def17745bf9bfbd6da51d`.
Supervisor ended `inactive/dead`, `ExecMainStatus=0`. Boards were left with
this exact receiver CPUAPP/FLPR and idle standalone source, not simultaneous
HCI/source roles. No runtime board mapping is reusable without fresh identity.

This closes the local exact-artifact transport/runtime migration gate. It does
not qualify the existing GitHub draft, ALSA capture hardware, MA1/SA1 analog
metrics or public release. Capture fixtures/model/runner migration has separate
host evidence above; digital I2S captures are not analog capture substitutes.

## HCI fixture qualification

Normal tracked `scripts/bap_central.py` ran through session-bound helpers,
stock SAMD11 bridge, UART20 H4 at 1,000,000 baud 8N1 without flow control.
Six cases emitted **72,000 writer frames**, with **PLC 428** and nonzero
loss inside unchanged limits. Decoder errors, I2S underruns, stream resets,
case alerts and bounded kernel HCI/SMP alerts are zero. Fresh Connect-led
bonding and preserved-bond reconnect both passed; no diagnostic CLI override,
RAM-trace firmware or altered UART sentinel substituted for production images.

Raw evidence includes bounded kernel journal, receiver console, source logs,
image hashes, fresh identity, system-manager containment and scoped adapter/
process cleanup. Runtime identity includes DPIDR `0x6ba02477`, AP0/AP1
`0x84770001`, AP2 `0x32880000`, AP3 `0x00000000`, PART `0x00054b15`,
VARIANT `0x41414330`, plus live USB identity and explicit paired roles.
No static probe or tty mapping is authority for future runs.

Qualification is bounded development-fixture proof. It is not generic
flow-control-free UART reliability, public consumer adapter support,
PipeWire/WirePlumber desktop acceptance or analog output qualification.

## Repairs and retained failures

- [Source peer-enqueue guard](nrf54l15-source-batch-guard-results-20261001.md)
  fixes cooperative TX scheduling interference without changing 3000/2000-us
  margins or receiver limits.
- [FLPR reload and lifecycle repair](nrf54l15-flpr-fresh-reload-results-20261001.md)
  restores fresh boot context/notification handling, drains heartbeat state and
  moves idle restart off the heartbeat workqueue. Native regressions and frozen
  trace-free physical hang/stall diagnostics pass.
- `2a0e792` exact matrix failed source under-lead after one passing row;
  `b21c7a7` failed FLPR hang recovery after eight passing rows. Both remain
  failures, not acceptance or rewritten evidence.
- Clean `e478e59` canonical 79/1/80 failed branch coverage only. Meaningful
  partial-registration rollback/retry regression restored clean `104e67a`
  80/0/80; production code and committed baseline remain unchanged.

[Reference audit](nrf54l15-final-reference-audit-20261001.md) classifies retained
history, unsupported-input regressions and external SDK context separately from
active target selection. Historical builds, results and archives are retained.

## Product and repository closure boundary

PB-019, PB-034 through PB-037 and PB-039 have checked evidence-backed criteria
and Final Summaries in **Review**, not accepted Done. PB-038 records aggregate
technical criteria and rationale without silently changing its product-owned
Backlog status or bypassing predecessor human acceptance. PB-018 remains
explicitly superseded, not falsely qualified or erased. Product PR/merge
lifecycle is separate from the completed local engineering verification.

Final closure changes documentation/backlog evidence only. Tested firmware
provenance remains exact `104e67a`; no newer documentation commit is substituted
as a firmware-build or hardware identity claim. User-owned PB-013 remains
byte-for-byte preserved and unstaged, and private graph/raw lab evidence stays
untracked/external. No push, PR creation, merge or publication was performed.
