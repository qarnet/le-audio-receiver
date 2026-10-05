# PB-051: partial-state ACL reconnect and fresh valid reuse

## Bounded source contract (2026-10-05)

Add only the additive ASCS client `reconnect` family; receiver already
registers it. Metadata, codec/QoS, lifecycle, dual, control-frame, SDK,
production parser guard and canonical 17/26 remain unchanged. Run four
ordered cases: disconnect after two Codec Configured ASEs; after two QoS
Configured ASEs; after A Enabling and B QoS; after A Streaming and B
Enabling, with **only A's CIS connected**. For the partial Streaming case,
register a single source stream, accept exactly ten valid 48-kHz/10-ms
120-byte sends and unregister before disconnect. No ARM/CHECK complete
stereo assertion for that partial window. These are source acceptances,
not peer-delivery or complete-render evidence. Restore two-stream TX policy
before fresh recovery.

Allocate one distinct static context for initial generation and one per
fresh connection (five total, below eight), never reuse GATT params. Before
intentional teardown, set the current context's `disconnect_expected`,
retire its CP subscription/pending operations under the existing total
five-second bound, then call public `bt_conn_disconnect` and require a fresh
disconnect semaphore completion. Canonical callback clears its connection
reference/pointer before giving that semaphore. An ASCS-only released
observer identifies its stream and original active generation after SDK
`bt_bap_stream_reset` (no dereference of cleared endpoint/conn), signals a
two-stream released barrier for **teardown coordination only**. Do not
accept a new generation until both stream releases finish, the old
unicast group is deleted through public API (only -EBUSY retried within
five seconds), and old context is safely retired. On reconnect clear old
client-side endpoint cache and reset connection, MTU, security and sink
discovery semaphores, then require fresh public connect/security/discovery,
negotiated MTU 65 and both newly read Idle ASE values. Reused numeric handles
alone are not new-discovery evidence.

Each of four new generations subsequently configures both streams from
fresh Idle, renders thirty sends per stream through independent stereo
sink observation, releases both and returns to Idle. Four ordered
CASE_BEGIN/END, five distinct generation contexts, eleven checked raw CP
exchanges (partial A Enable once, partial A/B Enable twice, plus eight
healthy final releases), and four fresh rendered phases. Retain complete
pre-disconnect state snapshots, owned closure, group deletion, new discovery
and new Idle readback. No private endpoint internals or fixed sleeps as
feature proof. Existing 240-second simulator ticker remains unchanged.

## One owned run

After syntax/diff check verify `/tmp/opencode` and absent
`pb051-reconnect-owner-r1` / `pb051-reconnect-r1`, create owner root only.
Run fixed `ASCS_CASE=reconnect` under `run_owned` inside `DescendantScope`,
900-second deadline, 64 MiB combined log and verified private ELF32 linker
prefix. Retain process/scope/source records, full peer/build/config logs
and image hashes. No extra case, retry, post-run source edit, hardware,
full gate, stage, commit or push. Any missing teardown callback, stale
context, failed public state or unexpected warning remains raw failure,
not a reason to weaken acceptance.

## Actual one-shot r1 result

Single `ASCS_CASE=reconnect` built both peers and returned 0. Client
logged four ordered CASE_BEGIN/CASE_END entries, five fresh owned generation
contexts, eleven checked raw CP exchanges and four fresh recovery phases;
receiver reported `ASCS_RECEIVER phases=4` with four rendered distinct
stereo phases. Every full recovery audited thirty sends per stream and
successful unregister (eight registered stream audits). Client final marker:
`ASCS_CLIENT case=reconnect cases=4 assertions=11 phases=4`.

| Step | Pre-disconnect public A/B state | Teardown and fresh-generation evidence |
| --- | --- | --- |
| reconnect_after_codec | 1 / 1 (full codec values in raw log) | generation 1 to 2, CP closed, released mask 3, no group existed, new MTU 65 and both Idle |
| reconnect_after_qos | 2 / 2 (full QoS values) | generation 2 to 3, same owned barriers and new discovery/Idle |
| reconnect_after_partial_enable | 3 / 2 (full A metadata/B QoS) | generation 3 to 4, owned barriers and new discovery/Idle |
| reconnect_after_partial_streaming | 4 / 3 (only A CIS connected) | generation 4 to 5, owned barriers and new discovery/Idle |

Partial A streaming logged `ASCS_PARTIAL_SOURCE generation=4 index=0
accepted=10 expected=10 peer_delivery=unproved` and checked unregister 0.
No backchannel complete-stereo ARM/CHECK was used for that partial window;
the later fresh fifth-generation full stream is the rendered proof. For each
of four transitions the old CP subscription delivered NULL on owned close,
`retire()` completed before public ACL disconnect, disconnect returned 0
and its callback cleared `default_conn`, both SDK stream-release callbacks
were observed (`released_mask=3`) before the group was deleted where one
existed (QoS, Enabling and partial Streaming; Codec had no group). Each
new connection independently rediscovered CP 28/CCC 29 and sink ASE IDs
1/2 on handles 31/34, negotiated MTU 65 and read both actual Idle values
before fresh valid streaming. Repeated numeric handles were recorded only
as fresh readbacks, never treated alone as proof of renewal.

No runtime warning or error was observed. Peer CMake logs retained only
the three target-specific experimental controller selections and known
native-only SoC product-support notice; no compiler, assigned-value or
incompatible-library link warning. Source SHA-256 for `client.c` matched
before and after run:
`a872f11120d6567a4922cfbfc63655ab7a1adcf417745eea62c4624fff118b3d`.
Client image SHA-256
`d04bcae7f735bd9c345f9596fb22055c1e867f7c8e45f121b37ef7b20c6705ed`;
receiver image SHA-256
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

| Retained artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-reconnect-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-reconnect-owner-r1/process-record.json` | `aa8f2494d78fb098c314f8a1b4546da906ff85671572f42449c0e0e7ac72c6af` |
| `/tmp/opencode/pb051-reconnect-owner-r1/scope-record.json` | `56cb79d14f4f920a2b722054f1eefe6ace02477a26134a39867b1591dee57670` |
| `/tmp/opencode/pb051-reconnect-owner-r1/source-record.json` | `e46e399db0487078a412d48f10d9f0be1fb50bee0a90d540629f093028f358f4` |
| `/tmp/opencode/pb051-reconnect-r1/client.log` | `cb624d0481342b54ca02675ea2a8bbcbe2171cac9d08613325f25f1f5cde4349` |
| `/tmp/opencode/pb051-reconnect-r1/receiver.log` | `90782b1f4d83820d6888df081ab120ced2e596a8b86ff489d44b99923ea4c16a` |
| `/tmp/opencode/pb051-reconnect-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-reconnect-r1/client-cmake.out` | `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc` |
| `/tmp/opencode/pb051-reconnect-r1/client-ninja.out` | `9fb140ee3f1226cc270685dd31b43dd0cfe12f1d8dc3071245a53e3b081a2969` |
| `/tmp/opencode/pb051-reconnect-r1/receiver-cmake.out` | `2502259d2eebd47a33b54bc3b145eace51c32afc0e79703b0033ef1e5fe213e8` |
| `/tmp/opencode/pb051-reconnect-r1/receiver-ninja.out` | `940693e7f2d21c8ed20344bbca2a87cf6211b5dc69b24bca5432f71d109e1a8c` |

Owner process and descendant scope report `ok=true`, no timeout, log
truncation, signal, cleanup error or live owned descendant. Child waits
all returned zero although runner has no separate numeric per-peer exit
fields. No second attempt or post-run source edit. This is one simulated
positive reconnect family, not timeout/cancel, stale callback, independent
physical RF or full required-case accounting acceptance.
