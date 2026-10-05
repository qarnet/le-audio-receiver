# PB-051: dual-ASE partial-result and recovery family

## Source and bounded behavior (2026-10-05)

Only the additive ASCS BSim client gains test ID `dual` (receiver already
declares it). Existing metadata, codec/QoS, lifecycle, control-frame,
canonical BSim 17/26, production, guard and SDK remain unchanged. Discover
both sink ASE IDs/handles dynamically: A=index 0, B=index 1. For each
ordered raw two-record CP request, require A success and B rejection in
the **same record order as sent**. Neither a successful ATT write nor an
all-or-nothing batch rollback is accepted in place of actual per-ID CP
responses and public ASE readback. Before/after raw values prove B remained
unchanged while accepted A changed only as specified. Each negative B is
corrected, then both channels send exactly 30 SDUs with fresh distinct
stereo sink output and end Idle.

Ten steps: four Enable order A/B vs B/A by CIS connect order forward vs
reverse; two QoS batch order variations with B interval FE0000, A's
original observed QoS accepted; two Disable variations after A alone was
enabled, with B rejected from QoS, then B valid Enable/Disable, followed by
valid Enable of both; two Release variations after A alone reached Codec
Configured, B remaining Idle, then correct B by real public high-level
configuration followed by A, create group/QoS/Enable, render and release.
The literal malformed Enable B metadata contains only `00` in the inner LTV
but has a valid outer metadata length. Do not locally reject it. Public QoS
payload derives solely from actual state-2 reads. No private endpoint or
guessed CIG/CIS mapping. CP accounting is exactly 42 checked exchanges:
four Enable steps *4, two QoS steps *4, two Disable steps *6, two Release
steps *3. Each batch has two records instead of one, yielding 52 exact
per-ID response records. Ten CASE_BEGIN/END entries and ten rendered phases
are required. The bounded single-case simulator ticker remains 240 s.

Installed SDK's structural metadata rejection diagnostics
(`Invalid application error code: 12` plus `Enable rejected: err -22,
code 12, reason 0`) are expected once per Enable batch (four). Other
case-local invalid-state diagnostics must be retained with their actual
source and count; no blanket warning waiver. Corrected source helper
callbacks and all existing public raw/state/renderer gates remain active.

## One owned run

After bash syntax/diff checks, verify `/tmp/opencode` exists and exclusive
`pb051-dual-owner-r1` and `pb051-dual-r1` do not exist. Create only owner
root. Execute fixed `ASCS_CASE=dual` shell runner under `run_owned` inside
`DescendantScope`, 900-second deadline, 64 MiB combined log, private verified
ELF32 `NIX_LDFLAGS` search-order prefix. Retain process/scope/source records,
full peer and build logs, resolved configs and image hashes. Run once; do not
retry, alter source after run, select other cases, flash hardware, run full
gate, stage, commit or push. A failed response, state or new warning stays
raw evidence, not a reason to weaken the oracle.

## Actual single r1 result

One new owned `ASCS_CASE=dual` run built both peers and returned 0. Client
recorded ten ordered CASE_BEGIN/CASE_END entries, 42 checked CP exchanges,
52 per-ID response records and ten recovery phases; receiver reported
`ASCS_RECEIVER phases=10` with ten distinct rendered stereo phases.
All twenty TX registrations logged exact `sends=30` and unregister 0.
Client final marker: `ASCS_CLIENT case=dual cases=10 assertions=42 phases=10`.
The CP owner closed and generation retired before PASS. Source SHA-256
`1c45dfcd85721d7694ed72db44c43cb23249e3f866667ccedf8346c80ebdc1f2`
was identical before/after run; client image SHA-256
`3ba4ca410ceb545dd68d9b3c136245606938cf50440885e9e44786aa0a2a6c41`;
receiver image SHA-256
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

All batch response records were checked against expected order, not sorted
by ID. Discovered IDs were A=1/B=2; QoS public CIG=0 and CIS A=0/B=1.
Examples from raw log, forward then reverse:

| Family | Forward request / response | Reverse request / response |
| --- | --- | --- |
| Enable | `0302010403020100020100` / `0302010000020c00` | `0302020100010403020100` / `0302020c00010000` |
| QoS | `0202` + actual A QoS + modified B QoS / `0202010000020903` | `0202` + modified B QoS + actual A QoS / `0202020903010000` |
| Disable | `05020102` / `0502010000020400` | `05020201` / `0502020400010000` |
| Release | `08020102` / `0802010000020400` | `08020201` / `0802020400010000` |

Client log retains every complete before/after B readback and accepted-A
public state/metadata observation, corrected B request and response,
followed by actual dual-CIS streaming and legal Release/Idle for each case.
QoS B's corrected request reproduced its original public bytes; A's valid
QoS matched its original snapshot. Disable cases observed A return to QoS
and B remain QoS, then B's valid Enable/Disable and both ASEs' subsequent
valid Enable. Four source `ASCS_DISABLED` observer records cover those
legal operations; they are not acceptance by callback count. Release cases
reconfigured B first, then A, through public `config_expect` before group,
QoS, streaming and final Idle; no private endpoint state was substituted.

Expected runtime diagnostics appeared exactly: four
`Invalid application error code: 12` paired with four
`Enable rejected: err -22, code 12, reason 0` on malformed B metadata,
plus two `Invalid operation in state: qos-configured` on invalid B Disable.
No other runtime warning or error. CMake retained three known experimental
controller symbols and the native-only SoC support notice per peer; no
compiler, assigned-value or incompatible-library link warning occurred.

| Retained artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-dual-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-dual-owner-r1/process-record.json` | `be0288cf785fe338827c14ff8884ea86dec89ff68975ad58488bf73d0577a415` |
| `/tmp/opencode/pb051-dual-owner-r1/scope-record.json` | `318d2808d670697b812466752d347611a523ab0248ce537cac1c46283a7d3f99` |
| `/tmp/opencode/pb051-dual-owner-r1/source-record.json` | `44376b2441f44c7780e98ad735fe264b7f327e69d8fc06d85af09aa3f8465ff2` |
| `/tmp/opencode/pb051-dual-r1/client.log` | `2d630c10b14c813d9fa56f3a198c2f4f7ef31d194650b4a1c7d7e8b6a4cf6b31` |
| `/tmp/opencode/pb051-dual-r1/receiver.log` | `d6190d6f2fc49e58857958b2a898439639e3624ba920c325116ca73ccfedda85` |
| `/tmp/opencode/pb051-dual-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-dual-r1/client-cmake.out` | `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc` |
| `/tmp/opencode/pb051-dual-r1/client-ninja.out` | `e3bf137f53f20812b7e01c18d65f0bae6379cc00796df11224ad5c59f7965157` |
| `/tmp/opencode/pb051-dual-r1/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-dual-r1/receiver-ninja.out` | `aa5db2f5f4017d304623ff5af55d8ab529dd9addabbebebb3b774b7eb9fe64bb` |

Raw resolved configs and peer/build logs remain in exclusive case root.
Owner process record and descendant scope both report `ok=true`, no timeout,
truncation, signal, cleanup error or live owned descendants. The runner's
child waits succeeded but separate numeric peer exit codes are not recorded.
No post-run source edit, second attempt, hardware action or full gate. This
validates only the dual-ASE family in simulation; reconnect, stale ownership,
full required-case accounting and physical/release acceptance remain open.
