# PB-051: public ASCS state and direction lifecycle matrix

## Declared scope and public boundary (2026-10-05)

Add a separate `lifecycle` client test ID to the existing ASCS BSim lane;
the receiver already declares the ID. Metadata, codec/QoS, control-frame,
canonical 17/26, SDK, production receiver and per-entry guard stay unchanged.
All rejected requests use raw GATT writes through the dynamically found CP,
with exact ordered per-ASE responses and complete both-ASE public before/after
readbacks. Success does not follow CP notification alone: poll public ASE
state (and cached client endpoint info where applicable) before testing a
repeated procedure. Every negative progresses through fresh valid setup,
two ISO streams, exact 30 sends each, distinct rendered sink and final Idle.

The exact 19 cases and expected replies/state transitions are the caller's
dated lifecycle handoff: four Idle invalid operations, Codec Configured
invalid Enable, QoS Configured invalid Disable, Enabling invalid Config/QoS,
Streaming invalid Config/Enable, two invalid sink ready-direction operations,
legal Disable from Enabling and Streaming with repeated invalid Disable,
legal Release from Codec/QoS/Enabling/Streaming with repeated invalid
Release, and invalid ASE ID zero. The valid Codec Config request uses the
bounded left preset builder; valid QoS from Enabling reuses a previously
observed QoS state snapshot. Idle invalid QoS uses only the specified
legal fixed-profile fields because no public QoS exists at Idle; it never
claims observed CIG/CIS binding there. Receiver is 2 sink/0 source.

Checked CP response arithmetic: four Idle steps each require negative 1 +
final releases 2 = 12; Codec and QoS invalid steps 5/6 each add two
pre-recovery releases (5+5); Enabling invalid Config/QoS steps 7/8 each
include raw Enable A, negative, two pre-recovery and two final releases
(6+6); Streaming invalid Config/Enable steps 9/10 each include negative,
two pre-recovery and two final releases (5+5); direction steps 11/12 likewise
(5+5); legal Disable from Enabling includes raw Enable A, success, repeated
failure, two pre-recovery and two final releases (7), Disable from Streaming
has success, repeated failure, two pre-recovery and two final releases (6);
Release from Codec/QoS has success, repeated failure, one remaining-ASE
release and two final releases each (5+5); Release from Enabling includes
raw Enable A (6), Release from Streaming (5); invalid ID and final releases
(3). Total **91** checked CP responses. Four initial Streaming phases plus
19 per-step valid recovery phases give **23** distinct render phases. Do not
remove checked release rounds to meet counts.

Use public `bt_bap_ep_get_info` for the ASCS-client-only disabled callback,
require owned generation, sink ID/direction, QoS state and log completion;
public wire/readback still decides success. No fake source ASE, private
endpoint state, callback-count acceptance, generic warning waiver or
unbounded local retry.

## Single owned run

After bash syntax/diff checks, verify `/tmp/opencode` and absent
`pb051-lifecycle-owner-r1` / `pb051-lifecycle-r1`, create owner root only.
Run exactly `ASCS_CASE=lifecycle` via the fixed ASCS shell runner under
`run_owned` inside `DescendantScope`, timeout 900 seconds and 64 MiB combined
log, with verified private ELF32 linker prefix. Retain full CMake, Ninja,
resolved config, peer logs, source/image hashes, process/scope JSON and
case/order/warning evidence. Existing simulator ticker is 240 s and is not
changed silently. No other family, retry, post-run source edit, hardware,
full gate, stage, commit or push. If warnings or lifecycle expectations
fail, preserve and report raw evidence without weakening the oracle.

## Actual r1 focused result

One `ASCS_CASE=lifecycle` run built both peers and returned code 0. Client
logged all **19 ordered CASE_BEGIN/CASE_END** steps and final
`ASCS_CLIENT case=lifecycle cases=19 assertions=91 phases=23`; receiver
logged `ASCS_RECEIVER phases=23`. All 23 unique ASCS_RECOVERY phases had
matching rendered receiver sink phases, each with two successful 30-send
audits and zero TX unregister result (46 of each). Four Streaming setup
steps contributed an additional initial rendered phase before their
rejection/recovery. No simulator ticker increase was needed: test finished
at `00:01:53.157224`, before the original 240-second bound.

Public CP and ASE ledgers reside in raw client log. The four Idle and both
Codec/QoS invalid-state cases returned 04/00, as did Config/QoS/Enable
invalid-state and repeated Disable/Release cases after their legal
transitions. Sink-only ready-direction opcodes 04 and 06 returned 05/00.
Raw invalid ASE ID zero returned 03/00 with echoed ID zero. Each
negative retained identical complete before/after reads of both ASEs, and
the following `recovery()` reached fresh valid dual-CIS 120-octet streaming,
distinct rendered output and both ASEs at public Idle. Legal Disable from
Enabling and Streaming returned 00/00, reached public QoS state 2 and
produced two ASCS-client `ASCS_DISABLED generation=1 id=1 state=2 index=0`
observer records. Legal Release from Codec/QoS/Enabling/Streaming returned
00/00, reached public Idle after Releasing when applicable, then repeated
Release returned 04/00 without altering both full ASE readbacks. Callback
records are additional lifecycle evidence, not a substitute for raw CP and
public state checks. Full case-specific request/response and full public
state bytes remain in `client.log`.

Expected SDK runtime warnings were exactly 11, each bound to invalid
operations: one `Invalid operation in state: codec-configured`, three
`qos-configured`, two `enabling`, two `streaming`, one
`Start failed: invalid operation for Sink`, one
`Stop failed: invalid operation for Sink`, and one `Unknown ase 0x00`.
No other runtime warning or error appeared. Both CMake logs retain three
known experimental controller symbols plus the native-only SoC product
notice; no compiler, assigned-value or incompatible-library link warning.

The client source SHA-256 was identical before and after the one run:
`eafd383b4a345438f4eac8524f1f5c507c05160d4917048f416c258f8e504b13`.
New client image SHA-256
`09a582714f6861d60886798e6230aca81d403f0413e84cbf96d9e8b26e67fcc1`,
receiver image SHA-256
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

| Retained evidence | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-lifecycle-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-lifecycle-owner-r1/process-record.json` | `b56616efb8267cc1dcfb8f53b56497dd76651cd956ac4d7a2568dcad7b5f1efa` |
| `/tmp/opencode/pb051-lifecycle-owner-r1/scope-record.json` | `a061c853ef58796f4dd544fe693e2e86d930b8e3b00ea20a871adb95528d7e38` |
| `/tmp/opencode/pb051-lifecycle-owner-r1/source-record.json` | `d791ae6079fd9d9aff32ea2feff42e99c080dcf5bfeb183fcd811b75c334e6dc` |
| `/tmp/opencode/pb051-lifecycle-r1/client.log` | `ea5ad6a9243685b4bed20e9fe151676dafb2af66b9708273568e3de80a2c26b8` |
| `/tmp/opencode/pb051-lifecycle-r1/receiver.log` | `4979e3375b314d27d1d993fb17ef4200ecaf5310752ed11e6491e499c01f09e3` |
| `/tmp/opencode/pb051-lifecycle-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-lifecycle-r1/client-cmake.out` | `00e7b31331668985e9dbef2180501b365e16c59f51a4e389f16a5a18b1659ecf` |
| `/tmp/opencode/pb051-lifecycle-r1/client-ninja.out` | `83754c0f96163f061af0e9dd3a74576a587156c0f7c035855c4d0b77b2b2565f` |
| `/tmp/opencode/pb051-lifecycle-r1/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-lifecycle-r1/receiver-ninja.out` | `b6cce7256c845cfe9b77a02e47e94f62b097f19e39a7f25ee42cbd92c723066b` |

Resolved configs and raw logs remain in exclusive case root; supervisor
scope returned `ok=true` with no timeout, truncated log, cleanup error,
adopted or live owned descendants. Individual child exit codes are not
separately recorded by diagnostic runner, though its wait loop returned
zero. No other case, retry or source edit after run. This is only the
lifecycle family; remaining dual/reconnect families, ownership negative
controls and fail-closed runner accounting still need their own evidence.
