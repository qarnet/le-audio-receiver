# PB-051: encoded codec and QoS rejection family (2026-10-05)

## Boundary and source contract

Add only the `codec_qos` client test ID already declared by the separate
receiver fixture. Existing metadata and control-frame cases remain unchanged
and unselected by this one-shot run. Keep SDK, production receiver, parser
guard, canonical 17/26, 2 sink/0 source, 16-byte metadata capacity and
MTU 65 unchanged. Build actual raw ASCS Config or QoS requests from the
public left LC3 preset or observed complete public QoS ASE readback, without
locally calling high-level invalid-configuration helpers. The source
contracts are `ascs_internal.h:131-181`, real codec preset data,
`bt_bap.c` codec rejection and `bap_stream.c:200-217` QoS validation.

Seven required steps, each with a single malformed/unsupported raw request,
exact CP response, full both-ASE public readback preservation, fresh valid
progress through 30 sends on each stream and distinct rendered receiver
output, and final legal Release/Idle:

| Case | One changed field | CP response |
| --- | --- | --- |
| codec_octets_19 | LTV type 04 value LE16 19 | 08/02 |
| codec_octets_121 | LTV type 04 value LE16 121 | 08/02 |
| codec_zero_ltv | first codec LTV length = 0, remaining bytes unchanged | 08/02 |
| codec_frequency_length | frequency LTV 02 01 08 becomes 03 01 08 00; update cc_len | 08/02 |
| qos_interval_254 | interval FE 00 00 | 09/03 |
| qos_framing_2 | framing 02 | 09/04 |
| qos_phy_80 | PHY 80 | 09/05 |

Codec Config raw record is nine fixed bytes plus bounded actual preset codec
data. Walk its LTV entries only to find type 04 and first frequency type 01,
requiring exact value widths and complete bounds. Wrong-frequency case
increases the encoded data length and outer cc_len by one without truncating
remaining valid fields. Total CP request must fit the observed ATT MTU minus
three. QoS raw record copies all 15 QoS payload bytes from actual state-2
readback, including CIG/CIS; only the named byte changes. No guessed private
endpoint fields. All seven begin in the specified Idle or fresh QoS state,
preserve both public readbacks, and follow existing `recovery()` for fresh
120-octet LC3 setup, two live CISs, 30 real sends each, receiver-rendered
output, and Idle. Emit one CASE_BEGIN and one CASE_END per completed step;
require seven unique rendered phases and 27 CP assertions before PASS.

## One-shot verification

Before running, syntax/diff check; verify `/tmp/opencode` exists and
`pb051-codec-qos-owner-r1` and `pb051-codec-qos-r1` are absent. Create
only owner root. Execute `ASCS_CASE=codec_qos` once through fixed ASCS
runner with `run_owned` under `DescendantScope` (900 s, 64 MiB combined
log, verified private ELF32 linker prefix). Retain full CMake/Ninja/config
and peer logs plus owner process/scope/source records. No alternative case,
retry, post-run source edit, hardware, full gate, stage, commit or push.
Unexpected warnings or failure remain raw evidence, not waived success.

## Pre-implementation count conflict

The seven-step handoff requests exactly 21 CP assertions (three per step),
but also requires each of the three QoS cases to release from QoS Configured
before fresh configuration/recovery and to release again after rendered
recovery. Existing `one()` increments assertions for each checked public CP
response; `release_all()` checks both ASEs. Thus each codec case needs one
negative plus two final Release responses (3 assertions), while each QoS
case needs one negative plus two pre-recovery Release responses plus two
final Release responses (5 assertions): `4*3 + 3*5 = 27`, not 21.
Skipping counted release responses or using a non-fresh QoS path would
weaken the stated public lifecycle proof. Owner approved 27 as the exact
count on 2026-10-05, preserving both checked Release rounds for every QoS
case. No case source changes or run preceded that correction; the original
21 count is superseded, not satisfied by skipping public checks.

## Actual single r1 result

Only `ASCS_CASE=codec_qos` ran in fresh owner/case roots. Process returned 0;
client reported `ASCS_CLIENT case=codec_qos cases=7 assertions=27 phases=7`
and receiver reported `ASCS_RECEIVER phases=7`. Client log contains seven
matched CASE_BEGIN/CASE_END entries, seven distinct ASCS_RECOVERY phases,
14 successful `sends=30` stream audits and 14 zero unregister returns.
Receiver logged seven ASCS_RENDER phases (27-28 pushes each, production
decoder and distinct-stereo sink boundary), with public Release/Idle reads
after every phase. All negative transactions used generation 1 and the
negotiated MTU 65.

| Step | Case | Raw transaction/CP response | Public state preserved |
| --- | --- | --- | --- |
| 1 | codec_octets_19 | 1 / `0101010802` | ASE 1 `0100`, ASE 2 `0200` |
| 2 | codec_octets_121 | 4 / `0101010802` | ASE 1 `0100`, ASE 2 `0200` |
| 3 | codec_zero_ltv | 7 / `0101010802` | ASE 1 `0100`, ASE 2 `0200` |
| 4 | codec_frequency_length | 10 / `0101010802` | ASE 1 `0100`, ASE 2 `0200` |
| 5 | qos_interval_254 | 13 / `0201010903` | ASE 1 `0102000010270000027800051400409c00`, ASE 2 `0202000110270000027800051400409c00` |
| 6 | qos_framing_2 | 18 / `0201010904` | same full QoS byte values |
| 7 | qos_phy_80 | 23 / `0201010905` | same full QoS byte values |

Each before/after readback pair appears in raw client log. Codec cases
retained only 2 checked final Release responses apiece; QoS cases retained
both checked pre-recovery and final Release rounds, yielding exact approved
27 assertions. Actual invalid requests are preserved in ASCS_REQUEST raw
lines: 19/121 LE16 octets, zeroed first LTV length with unchanged outer
codec size, and wrong-width frequency with cc_len incremented to 17;
QoS copied observed CIG/CIS and changed only the specified interval,
framing or PHY bytes. Receiver logged four codec reject diagnostics
`Codec config rejected: code 0x08 reason 0x02`; no runtime warning/error was
emitted for these cases. No case, response, recovery, limit, or source was
weakened to pass.

Both builds retained only the exact target-specific BSim experimental
Kconfig selections and known native-only SoC product-support CMake notice.
No compiler, assigned-value, incompatible-library link, or unexplained
runtime warning was found. `client.c` SHA-256 was
`a44c9eb20c98c8fdb68310a65cfb735162a51d21ef8f03aa898f08191e1dd865`
before and after run; client image SHA-256
`168484a74d5e6ebf0f2c7a09a150a4028da125181288c548d8f7e5c4cfd06818`.
Receiver image SHA-256
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

| Evidence | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-codec-qos-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-codec-qos-owner-r1/process-record.json` | `847ce089b8239cd17fd6f7f5c247fcf05b37e8c5463f066324f5242379403c4b` |
| `/tmp/opencode/pb051-codec-qos-owner-r1/scope-record.json` | `0948bdaccafabc152df24876a36186b1f89f1f0b386aad3b7794b7a9c34bd4bb` |
| `/tmp/opencode/pb051-codec-qos-owner-r1/source-record.json` | `64d7d7611b4877ecfcd0c062afba248f41efdd7bf654a7fee76551f9e16cf3e4` |
| `/tmp/opencode/pb051-codec-qos-r1/client.log` | `dce0ff2d92b0c7319884d2292337d31dfa4ce8a8d818868775cb0bd312df390f` |
| `/tmp/opencode/pb051-codec-qos-r1/receiver.log` | `f11dab5ede702ae6484fc066686f796b623c88f06b7c15eae0f4215328b80d6b` |
| `/tmp/opencode/pb051-codec-qos-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-codec-qos-r1/client-cmake.out` | `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc` |
| `/tmp/opencode/pb051-codec-qos-r1/client-ninja.out` | `283f770f8bd0aa04ad1f190693fa786aba031d057aadb9c71a6f4f5dc64b9ad2` |
| `/tmp/opencode/pb051-codec-qos-r1/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-codec-qos-r1/receiver-ninja.out` | `87695c332a72d37cb2c1d52aab268b535121282b6cf901c30c2a3b4f9f582827` |

Full config and raw peer logs remain in the fresh case root; process scope
reported `ok=true`, no timeout, truncation, signal, cleanup errors, adopted
or live owned descendants. Diagnostic runner does not record individual
child numeric exit codes, but its three waits all succeeded. No source
edits or second attempt after run. Other PB-051 families, ownership negative
controls and strict case accounting remain outside this phase; this one
simulated family is not complete PB-051 or physical qualification.
