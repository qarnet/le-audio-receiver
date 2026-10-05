# PB-051: encoded ASCS integration lane

## Source and claim boundary

Freeze a separately provisioned regression lane against installed NCS v3.4.1 and
production receiver 2-sink/0-source exposure, metadata capacity 16. Normative
background is ASCS v1.0, adopted 2021-09-14, confirmed from the complete official
HTML at https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/23166-ASCS-html5/out/en/index-en.html.
Do not label that source 1.0.1/latest or claim formal qualification. Section 3.2
requires failed transitions to preserve state/parameters; section 5 permits more
detailed configuration responses 07/08/09. Installed response choices are recorded
as regression contracts, not a complete normative conformance verdict.

The new lane executes real production BAP, ASCS stack, receive/session and LC3
decode over encoded GATT/ISO in BSim. It does not substitute BlueZ host-only
results. Existing canonical 17 IDs/26 runs, recipes, physical HIL/PCM limits and
coverage baseline stay unchanged. Existing checked-in LC3 streams remain bounded
regression stimuli, not independent codec reference material. No LC3plus.

## Public seams and orchestration

Connect, secure, dynamically discover ASCS primary service/characteristics/CCCs,
read sink ASE IDs, subscribe CP/ASE values, and retain full ATT completion plus
raw CP responses and ASE readback. Public APIs: `bt_gatt_discover`,
`bt_gatt_subscribe`, `bt_gatt_write`, `bt_gatt_read`, and `bt_bap_ep_get_info`.
No hardcoded attribute handles or cached connection-generation objects.

Use observed MTU 65 for the secured Zephyr lane: public exchange requests maximum
buffer-supported MTU, and SMP Kconfig minimum is 65. Do not claim the surveyed
BlueZ exact-MTU64 case was reproduced. Observe async state transitions before
deterministic legality tests; per-ASE responses do not imply batch rollback.

Existing client valid connect/group/CIS/TX helpers may be included under renamed
canonical installer symbols. The 17 old cases remain unmodified and unselected.
New encoded requests, response parser, per-ID state/history and PASS composition
are independently authored. New receiver has separate entry/completion and sink
observation, not private canonical teardown-count acceptance.

## Bounded required families

| Family | Required public behavior |
| --- | --- |
| CP framing | Unknown opcode, zero count, missing record, surplus trailing byte and variable-length truncation return exact global 01/02 responses; ASE values unchanged |
| Codec/QoS | Actual 19/121-octet LC3 requests reject 08/02; unequivocal interval254/framing02/PHY80 QoS rejects 09 with reason03/04/05; valid corrected setup succeeds |
| Metadata structure | Zero LTV, clear overrun and exact-end missing value reject 0C/00 without changing QoS/metadata; corrected Enable succeeds |
| Metadata semantics | Zero streaming context and wrong known-type lengths reject 0C/type; well-formed unknown F0 accepts under installed contract and remains observable |
| Update/state/direction | Valid Update preserves enabling/streaming state and updates values; malformed Update preserves previous values; invalid state/direction fails correctly |
| Lifecycle | Legal Disable/Release from admitted states, illegal/repeated procedures after observed state, and subsequent fresh valid use prove cleanup/recovery |
| Dual ASE | Valid A plus invalid B yields per-ID partial result in either request order; repair B then stream both; reverse CIS ordering is separately exercised |
| Partial reconnect | Disconnect after codec, QoS, partial Enable and partial Streaming; reconnect/rediscover/subscribe and freshly reuse both ASEs |

Each negative case must make valid progress through remaining codec/QoS/Enable/CIS
steps and real rendered output. Reconfiguration is not assumed: production has
no `.reconfig` callback; release-to-Idle or corrected initial setup first. Receiver
Release completion to Idle is installed behavior; normative specification also
permits cached Codec Configured completion. Unknown metadata is not automatically
unsupported. Invalid requests must not be manufactured through high-level BAP
helpers that reject locally before transmission.

## Conditional parser repair boundary

Installed `zephyr/subsys/bluetooth/audio/audio.c:53` checks `i+len>size` before
consuming the length octet. Inner bytes `02 F0` with declared size 2 can therefore
expose a one-byte value beyond logical buffer and return success. Exact encoded
Enable stimulus is `03 01 ASE_ID 02 02 F0`. Production Enable ignores metadata
and Update callback returns success, so stack validation is load-bearing.

First retain actual native public-parser and encoded-wire failure. Use padded
backing bytes for pure-parser diagnosis to expose logical range violation without
relying on uncontrolled memory access. Evaluate a small CPUAPP parser diagnostic
on freshly identified Nordic hardware before production behavior change when
practical; pure-parser target evidence is not physical ASCS acceptance.

If measured, a repository-owned GNU linker guard can preflight the complete LTV
range before calling actual SDK parser, preserving valid callback order and
early cancellation. Return type is int (0, -EINVAL, -ECANCELED), not bool.
Malformed suffix rejection before any callback is an explicit stricter structural
policy. Do not patch SDK. Undefined-reference wrapping does not catch same-object
calls from `bt_audio_data_get_val` or `bt_audio_valid_ltv`; do not claim SDK-wide
hardening. Verify actual ASCS link/disassembly and public behavior. Any needed
additional public-boundary guard must be source-grounded and explicitly tested.

## Warning and execution accounting

Retain raw logs. Negative-path diagnostics require per-case exact message,
argument and bounded-count evidence, not global regex suppression. The SDK's
unknown-type acceptance warning and metadata-invalid application-error diagnostic
quirk are separate named expectations; unexpected compiler/Kconfig/runtime
warnings still fail. Trace each request/response/readback, negotiated MTU,
connection generation, source/config/image identities and rendered-output proof.

Runner owns bounded peer/PHY lifecycle, timeout/cancellation, immutable external
results and exact required-case accounting. Empty/missing/duplicate/partial cases,
wrong response/state, stale notifications, stale handles and failed recovery
cannot pass. Add lane to applicable gates without altering canonical recipes.
Implementation Plan and current status remain in PB-051, not this document.

## Dated parser policy update (2026-10-05)

The complete-buffer preflight in the conditional research proposal at lines
74-77 was **not implemented**. Installed `audio.h:277-284` promises callbacks
for each parsed entry and `-ECANCELED` on early cancellation; a malformed
suffix cannot suppress an already-delivered valid prefix or override earlier
callback cancellation. The native 6/1/7 baseline, historical CPUAPP 2/1/3
baseline and encoded ASCS R4 request `0301010202f0` / response `0301010000`
justify a per-entry check instead. A repository-owned `--wrap` validates the
next entry's complete logical length before forwarding that one entry to the
actual SDK parser. SDK source remains untouched. Same-object SDK callers
remain beyond the undefined-reference wrapping seam; no blanket hardening
claim. See `pb-051-per-entry-ltv-guard-handoff-20261005.md` for focused
verification and its separate physical/wire proof boundaries.
