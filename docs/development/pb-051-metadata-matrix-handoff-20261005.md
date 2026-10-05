# PB-051: complete metadata-family encoded ASCS matrix

## Bounded design and acceptance boundary (2026-10-05)

Run only the metadata family in the separately provisioned ASCS BSim lane,
with 13 declared cases and 14 genuinely rendered phases. Existing successful
`metadata_length_validation` is replaced by the full family in that test ID;
the control-frame case is unchanged and unselected. Other codec, QoS, dual
ASE, lifecycle, reconnect and strict runner-accounting families remain later
work. Production receiver, real SDK, per-entry guard, canonical 17/26,
MTU 65, 2 sink/0 source and metadata capacity 16 stay unchanged.

For each of 11 rejected Enable A inputs below, create raw CP request with
dynamic ASE ID and actual metadata length. In fresh Idle->QoS setup, read
both complete public ASE values. Require exact per-ASE CP 0c/reason,
unchanged before/after full QoS values, then correct the *same type* by raw
Enable A and check success plus full Enabling metadata echo. Enable B using
the valid preset metadata, observe both Enabling ASEs, connect both CISs,
audit 30 sends per stream and receiver's distinct-stereo rendered sink,
release both to Idle. One CASE_BEGIN/END ledger entry per name.

| Case | Bad metadata | Corrected metadata | Reason |
| --- | --- | --- | --- |
| metadata_zero_entry | `00` | `03020400` | 00 |
| metadata_clear_overrun | `04f0aa` | `02f0aa` | 00 |
| metadata_exact_end_value | `02f0` | `02f0aa` | 00 |
| metadata_exact_end_type | `01` | `01f0` | 00 |
| metadata_preferred_context_length | `020104` | `03010400` | 01 |
| metadata_stream_context_length | `020204` | `03020400` | 02 |
| metadata_zero_stream_context | `03020000` | `03020400` | 02 |
| metadata_language_length | `0304656e` | `0404656e67` | 04 |
| metadata_parental_length | `0106` | `020601` | 06 |
| metadata_audio_state_length | `0108` | `020800` | 08 |
| metadata_broadcast_immediate_length | `020901` | `0109` | 09 |

Two further cases: `metadata_unknown_enable_update_enabling` enables A with
`02f0aa`, updates A in Enabling with `02f0bb` (same state and exact public
echo), rejects `00` in Update with 0c/00 and byte-identical ASEs, then
validly enables B, renders and releases (one phase).
`metadata_update_streaming` enables both, renders initial phase (30 each,
unregister and independent receiver sink proof), updates A with `02f0bb`
in Streaming with same-state exact echo, rejects Update `04f0aa` while
preserving both complete streaming ASE values, releases both, then uses the
existing fresh valid recovery path for a second rendered phase and final Idle.
No reset of ISO sequence inside a live CIS.

Public Enabling/Streaming values are header `{id,state,cig,cis,metadata_len}`
followed by exact metadata bytes (installed `ascs_internal.h:86-102`). Bind
CIG/CIS to previously read QoS values, not private endpoint structures.
Before `bt_bap_stream_connect`, poll public `bt_bap_ep_get_info` for
matching discovered ID, sink direction and Enabling state (bounded 5 s).
CP success alone does not prove async state completion. Log CASE_BEGIN/END
with monotonic step/name/generation/phase and all full raw reads; do not print
CASE_END or aggregate PASS on partial failure. Expected SDK diagnostics:
one `Invalid application error code: 12` plus `Enable rejected` per rejected
Enable, `Metadata failed` for rejected Update, and `Unknown metadata type
0xf0` only for *valid* corrected unknown types and the three valid unknown
Enable/Update operations across both additional cases. Retain exact raw warning text and bound each to
transaction/case; no generic warning waiver or RF-corruption claim.

## One-shot run

After edits and diff checks, verify absent
`/tmp/opencode/pb051-metadata-matrix-owner-r1` and
`/tmp/opencode/pb051-metadata-matrix-r1`; create only owner root. Execute
one `ASCS_CASE=metadata_length_validation` via fixed ASCS runner under
`run_owned` inside `DescendantScope` (900 s, 64 MiB combined log), with
private verified ELF32 linker paths. Retain full build/peer logs and hashes,
process/scope records, 13 ordered step entries, 14 rendered phases and
public wire/state evidence. If 240-second simulator ticker is insufficient,
retain its failure, do not silently extend or retry. No other case, hardware,
full gate, SDK/production/runner edit, stage, commit or push in this phase.

## One-shot r1 result and warning hold

One run compiled both peers and exited 0, with 13 `CASE_BEGIN` and 13
`CASE_END` records in declared order, 14 unique `ASCS_RENDER phase=1..14`
records, 28 send-count records (`sends=30`) and 28 successful TX unregister
records. Client reported
`ASCS_CLIENT case=metadata_length_validation cases=13 assertions=67 phases=14`;
receiver reported `ASCS_RECEIVER phases=14`. Simulated completion occurred at
66.187224 seconds, within the existing 240-second ticker. Before/after
full QoS public readbacks matched for every negative case; the raw CP
response ledger and corrected same-type retry success, Enabling/Streaming
metadata echo and post-recovery Idle values are in the retained client log.
Selected exact outcomes: first zero entry request `0301010100` ->
`0301010c00`, corrected `0301010403020400` -> `0301010000`;
exact-end request `0301010202f0` -> `0301010c00` and corrected
`0301010302f0aa` -> `0301010000`. Update in Enabling:
`0701010302f0bb` -> `0701010000`, then `0701010100` -> `0701010c00`,
with A Enabling raw `010300000302f0bb` preserved; B remained QoS. Update
in Streaming: `0701010302f0bb` -> `0701010000`, then
`0701010304f0aa` -> `0701010c00`, A Streaming raw
`010400000302f0bb` and B Streaming raw
`020400010403020100` preserved. Fresh release/reconfiguration then rendered
phase 14 and returned both ASEs to public Idle. Source SHA-256
`259ef866e3dcb8c7abb5707c89bf783025a2964b802ec78d6a5ea71b4d973be6`
was identical before and after run; new client image SHA-256
`b71bb77199c13db5525c77008a0b1b49f2c22eb9cbbe60e9707c0fbbe6dcbc18`.
Receiver image SHA-256
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

Actual negative CP notifications by case step (all from generation 1, client
raw log; corrected retry and both-ASE preservation are checked before each
CASE_END):

| Step | Case | Transaction | Raw rejection |
| --- | --- | ---: | --- |
| 1 | metadata_zero_entry | 1 | `0301010c00` |
| 2 | metadata_clear_overrun | 6 | `0301010c00` |
| 3 | metadata_exact_end_value | 11 | `0301010c00` |
| 4 | metadata_exact_end_type | 16 | `0301010c00` |
| 5 | metadata_preferred_context_length | 21 | `0301010c01` |
| 6 | metadata_stream_context_length | 26 | `0301010c02` |
| 7 | metadata_zero_stream_context | 31 | `0301010c02` |
| 8 | metadata_language_length | 36 | `0301010c04` |
| 9 | metadata_parental_length | 41 | `0301010c06` |
| 10 | metadata_audio_state_length | 46 | `0301010c08` |
| 11 | metadata_broadcast_immediate_length | 51 | `0301010c09` |
| 12 | metadata_unknown_enable_update_enabling | 58 | `0701010c00` |
| 13 | metadata_update_streaming | 63 | `0701010c00` |

**Warning policy remains open.** Receiver emitted exactly 13 installed SDK
`Invalid application error code: 12` warnings (11 Enable rejection errors
and two Metadata failed errors), plus six known-type-disposition warnings
`Unknown metadata type 0xf0` for valid unknown metadata. New client runtime
warning `bt_bap_unicast_client: No callback for metadata_updated set`
appeared **twice**, at `00:00:54.917732` (Enabling Update) and
`00:01:00.797732` (Streaming Update). This warning was not part of the
predeclared case-local set and is **not waived** by the successful process
exit; its installed-source cause and disposition require a separate review.
No change, retry or test weakening was performed after encountering it.
Both CMake logs retained expected target-specific experimental selections
and native-only SoC notice; no compiler, assigned-value or incompatible
library warning appeared in the build logs. One case family passing in this
diagnostic runner is not full PB-051 acceptance or complete external-result
accounting.

| Artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-metadata-matrix-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-metadata-matrix-owner-r1/process-record.json` | `e819d64c5344df85c6f28c01e94cbca0bd2ce1357bc75052e56c25f1c8739500` |
| `/tmp/opencode/pb051-metadata-matrix-owner-r1/scope-record.json` | `1ecb8f460fecd9a24e9e7c5b851903621f749eb0f4734f422992f92d69ef7fcb` |
| `/tmp/opencode/pb051-metadata-matrix-owner-r1/source-record.json` | `ca10c9882efe340c9edac4d83e637ef1002226435f88ba3abcced8e00a86fe52` |
| `/tmp/opencode/pb051-metadata-matrix-r1/client.log` | `ce41d1bedfa1d563c18850f0f50109e814b5908ebf3b695720c301c3875087d2` |
| `/tmp/opencode/pb051-metadata-matrix-r1/receiver.log` | `279bf8e33c26fc1f97e975161bcacc98a8a158cfc7631ad2aff1e2b5cf212a57` |
| `/tmp/opencode/pb051-metadata-matrix-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-metadata-matrix-r1/client-cmake.out` | `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc` |
| `/tmp/opencode/pb051-metadata-matrix-r1/client-ninja.out` | `8df207c4a1b4f1507e6833c7e201b93f4f2ae3c45037c1205c3ba3b55e64bd11` |
| `/tmp/opencode/pb051-metadata-matrix-r1/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-metadata-matrix-r1/receiver-ninja.out` | `20f3529d6fd96c97f350b11494620ba0779eefa6031f02a8a046bbcd1bbe3bfa` |

Raw configs and all peer logs remain in exclusive r1 case directory.
Owner scope returned `ok=true`, no timeout, log truncation, cleanup errors,
adopted/live owned descendants; no individual receiver/client/PHY exit-code
fields are recorded by this diagnostic runner (its wait loop returned 0).
No repository source edits after run. Next phase must diagnose/dispose the
unexpected client warning without suppressing it and add remaining families
and fail-closed required-case accounting independently.

## Dated fixture observer repair and r2 contract (2026-10-05)

Installed NCS v3.4.1 `bap.h:995-1002` exposes
`bt_bap_stream_ops.metadata_updated`. Installed
`bap_unicast_client.c:1126-1129,1172-1175` calls it for same-state
Enabling/Streaming metadata notifications and emits the r1 warning only when
the application omitted that callback. Keep the historical two warnings in
r1 raw evidence. In the additive ASCS client only, register an actual
observer after `client_setup()` and before connect, through the existing
translation-unit-private stream ops. Check public endpoint info against the
active owned generation, one of the two streams, discovered ASE ID, sink
direction and Enabling/Streaming state, log its original generation and
observed identity. Any mismatch sets sticky wire failure; callback counts
are not case acceptance. Raw CP response, metadata echo/readbacks, 30 sends
per stream, independent rendered output and release remain the oracles.

Verify with one fresh owner `/tmp/opencode/pb051-metadata-matrix-owner-r2`
and child `/tmp/opencode/pb051-metadata-matrix-r2`, same exclusive
one-shot 900 s / 64 MiB / DescendantScope / private ELF32 link path contract.
Expected: all 13 steps, 67 ordered CP assertions, 14 rendered phases,
no `No callback for metadata_updated set` warning, and public Enabling
and Streaming observer records. Preserve r1 and all older evidence; no
retry, unrelated cases, post-run code edit, hardware, full gate, commit,
stage or push. Do not waive new warnings or change acceptance checks.

## Actual r2 observer result

The single fresh run completed with process code 0 and clean descendant
scope. `tests/ascs_bsim/client/client.c` SHA-256
`062e79212b7baeb8d1f319155d7c71a8afe4db52b2c5ad1c18e1631d86b07b78`
was identical before and after build; newly built client image SHA-256
`72ef46331adf0e41216a9c448866c24d0b42bba72acaec782db05e3b003878ec`.
No source edit followed this run.

Client logged **one public observer** record for matching discovered sink
ASE 1/index 0/Enabling state 3 at `00:00:54.917732` and **one** for
the same ASE/Streaming state 4 at `00:01:00.797732`. No
`No callback for metadata_updated set` warning or callback error remains.
These callback records prove observer delivery, not metadata acceptance by
themselves. Raw CP responses, byte-identical full ASE readbacks, corrected
same-type retries, state/metadata echo and actual sink output remained
load-bearing. Client reported `cases=13 assertions=67 phases=14`, with
13 ordered CASE_BEGIN/END pairs, 28 exact `sends=30` audits and 28
successful unregisters. Receiver reported 14 rendered phases and final
`ASCS_RECEIVER phases=14`; CP owner closed and retired before client PASS.
The actual rejection table in the r1 section is unchanged; r2 raw client
log contains the same ordered responses and full public metadata bytes.

Only the declared receiver diagnostics remain: 13 `Invalid application
error code: 12`, 11 `Enable rejected` plus two `Metadata failed`, and six
`Unknown metadata type 0xf0` on valid inputs. Both build CMake logs retain
three fixture experimental warnings plus the native-only SoC support notice;
no new compiler, assigned-value, incompatible-link or unexpected runtime
warning was found. This is **metadata-family-only** evidence, not full
ASCS matrix, complete runner accounting, physical RF or release acceptance.

| Retained r2 artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-metadata-matrix-owner-r2/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-metadata-matrix-owner-r2/process-record.json` | `b2b7e4b84e26877783d4104181b0be8ef82f97a0456b087537b2305a0edbc089` |
| `/tmp/opencode/pb051-metadata-matrix-owner-r2/scope-record.json` | `00c9c52e9a1f6a6504e9f9814e76fb09a436bb6de6dbfbdb5bdb39b06c4b1429` |
| `/tmp/opencode/pb051-metadata-matrix-owner-r2/source-record.json` | `c9068c10566a9bc029d7bb759aa6c8f5af747cca642a212a0a989a0b44155eac` |
| `/tmp/opencode/pb051-metadata-matrix-r2/client.log` | `4a95a4b89ebdce3b6a88c79f3cd06d18905020a82fbbf7d34299022164f1fa8b` |
| `/tmp/opencode/pb051-metadata-matrix-r2/receiver.log` | `279bf8e33c26fc1f97e975161bcacc98a8a158cfc7631ad2aff1e2b5cf212a57` |
| `/tmp/opencode/pb051-metadata-matrix-r2/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-metadata-matrix-r2/client-cmake.out` | `1519e81606edb1fd161fe152c5f44a6db04d800cb21f21e8bbb90ba0b033eae3` |
| `/tmp/opencode/pb051-metadata-matrix-r2/client-ninja.out` | `3ee5f451fa0f980b0c575b043e4577835856a57907ccd0b6974833c5953ceef8` |
| `/tmp/opencode/pb051-metadata-matrix-r2/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-metadata-matrix-r2/receiver-ninja.out` | `9eea5e5a8e61a883191f70f0f6eb32a8a236587bbe1a35e92eef1c066cd2eced` |

Full peer logs and resolved config remain under the exclusive r2 case root;
the runner's child waits completed successfully but it does not retain
separate per-peer numeric exit-code records. Previous r1 failure remains
immutable. No other case or retry in this phase.
