# PB-051: encoded wire ownership and public evidence ledger

## Source and decision

Installed NCS v3.4.1 `gatt.h:2351-2368` declares `bt_gatt_cancel` as
`void`: it does not free params or suppress terminal callbacks. An active
operation timeout must cancel and then retire its stable per-generation
storage only after terminal callbacks; no reuse or next generation while
cleanup is ambiguous. `gatt.c:5568-5585` invokes the cancel completion when
pending. `bt_gatt_unsubscribe` calls the owned notification callback with
NULL synchronously when another BAP CP subscription remains. Hold each
generation's extra `bt_conn_ref` through bounded retirement; release it
only after every outstanding operation and CP closure complete. Eight static
generations maximum, no heap and no private BAP handles. Old callbacks
retain their originating generation and fail closed rather than being
relabeled as current. `bsim_tx_send_count` (not `get_send_count`) is the
verified public fixture TX audit API at `tests/bsim/client/src/bsim_tx.h:119`.

## One-shot verification

Preserve prior native, physical and encoded-wire roots. Modify only the
ASCS client fixture to retain async params and transaction buffers, log
public ASE readbacks/transaction responses and require ordered records,
exactly 30 sends on each registered stream, legal Release/Idle, and owned
CP closure. Keep existing negative response and recovery oracles unchanged.
After bash syntax/diff checks, verify parent `/tmp/opencode` and absent
`pb051-wire-owned-owner-r1` / `pb051-wire-owned-r1`. Create owner root only.
Run only `metadata_length_validation` with the existing fixed ASCS shell
argv, private verified ELF32 `NIX_LDFLAGS` prefix, `run_owned` inside
`DescendantScope`, deadline 900 seconds and 64 MiB combined log. Retain
process/scope records and child build/peer logs. No SDK, production, runner,
canonical, hardware, full gate, commit, stage or push. A timeout must not
recycle any pending params or held connection. Failing tests and warnings
remain evidence, never an acceptance waiver.

## Actual one-shot result

One r1 run built and executed `metadata_length_validation`. Runner returned
zero; client reported `ASCS_CLIENT case=metadata_length_validation assertions=3
phases=1`, receiver `ASCS_RECEIVER phases=1`. Runner's wait loop returning
zero implies all receiver/client/PHY waits returned zero, though individual
numeric child exit codes are not separately recorded and PHY log is empty.
Process/scope records report `ok=true`, no timeout, truncation, cancellation,
adopted descendants or remaining owned process group.

Dynamic discovery: generation 1, MTU 65, CP 28/CCC 29, ASE 1/31 and 2/34.
Exact transaction 1 request `0301010202f0` received `0301010c00` with ATT
write completion error 0. Before and after public ASE reads match bytewise,
including both QoS Configured (state 2):

| ASE | Before and after full raw value |
| --- | --- |
| 1/31 | `0102000010270000027800051400409c00` |
| 2/34 | `0202000110270000027800051400409c00` |

Valid Enable yielded helper CP notifications labeled transaction 0. Both ASEs
advanced to state 4; independent TX audit logged **30 successful sends each**
and unregister code 0 for both. Receiver logged `ASCS_RENDER phase=1
pushes=27`; receiver sink checked nonzero separate-channel energy, distinct
stereo samples, correct sample geometry and no bad pushes before confirming
rendered output. Release transactions 2 and 3 both succeeded (`0801010000`,
`0801020000`); public ASE reads reached Idle (`0100`, `0200`). CP owned
subscription received NULL closure, unsubscribe returned 0, and generation 1
retired with `cp_closed=1` before client PASS. This is one simulated encoded
case, not the full matrix or physical proof. The timeout/cancel and reconnect
paths are implemented but not exercised by this successful case; their
failure behavior and future generations need separate negative tests.

Raw receiver retains exactly one known installed-ASCS
`Invalid application error code: 12` warning, paired with expected `Enable
rejected: err -22, code 12, reason 0`. Each peer build retains three
target-specific experimental controller Kconfig warnings and the known
native-only SoC product-support notice; no compiler, assigned-value or
incompatible-library warning was observed. No other case or retry.

| Evidence | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-wire-owned-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-wire-owned-owner-r1/process-record.json` | `c10c1140e10ace99370d62fcea7a666d3e969adedb078010056b940fcf3996d3` |
| `/tmp/opencode/pb051-wire-owned-owner-r1/scope-record.json` | `063f68b67f2650e70388c524964c01c048d6347747ab094ffd10119c3af12c1e` |
| `/tmp/opencode/pb051-wire-owned-r1/client.log` | `20a4b74748a2ce0865cfe4fd0159e080785bb5e8e258edd6f86a1eeaae30402d` |
| `/tmp/opencode/pb051-wire-owned-r1/receiver.log` | `adba0e6ad5c06f593d27229921a7aeddccb0669833e32bafc1efe3829cb6151d` |
| `/tmp/opencode/pb051-wire-owned-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| receiver image | `f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e` |
| client image | `8a0ca5134ca5d2aa2794199045c53f64210ea76185f986557cf931f643ef4066` |

Complete peer build logs/configs in the fresh case root retain compiler/link
and fixture configuration evidence. The previously passed twelve native public
parser tests were not rerun; their bytes, implementation and result root were
left unchanged in this phase.

After the one permitted run, review found `notify(NULL)` for ACL teardown
must close the owned subscription even if the test has not explicitly begun
retirement. Removed an overly broad sticky-error assignment on that callback
path, without changing the successful unsubscribe path exercised above.
This post-run source adjustment is **not covered by the recorded image hash**;
do not treat r1 as validation of the final source bytes. No second run was
performed under this handoff. The next bounded verification must include
fresh build/behavior evidence for final bytes and explicit disconnected
closure/timeout/cancel negative controls before lifecycle acceptance.

## Review repair and r2 verification contract (2026-10-05)

R1 success precedes final `notify(NULL)` change. Review narrows allowed
closure: only while retiring or after explicitly planned public ACL disconnect;
unexpected active closure must set sticky failure, including stale generation
and mismatched connection. Cross-thread lifecycle/status flags become C
`atomic_bool`, and current context becomes an atomic pointer. No mutex is held
across SDK operations. Recheck transaction-active flag just before writing
callback response bytes. Start the single monotonic 5-second retirement budget
before any cancel or unsubscribe, including synchronous work in its deadline;
on failure keep reference and static params through process exit. Read/state
and response paths continue checking sticky errors, including before PASS.
Use verified `bsim_tx_send_count` and propagate unregister errors unchanged.

Run exactly one fresh `metadata_length_validation` case with the same bounded
process/subreaper owner and private ELF32 link prefix, new exclusive owner
`/tmp/opencode/pb051-wire-owned-owner-r2` and child
`/tmp/opencode/pb051-wire-owned-r2`. Preserve all earlier roots and raw
warnings. Require exact 0c/00 response, full equal QoS readbacks, 30 sends
per stream, distinct stereo rendered sink, public Idle, owned CP close and
retired context before PASS. Record source SHA-256 and newly built image
hashes together. Guard and prior native 12-test result are unchanged; no
extra case, physical action, canonical run, commit, stage or push. Timeout,
cancel, stale-callback and reconnect failures still need separate negative
controls after this positive case.

## Actual r2 review result

One new owned run returned 0, with both images rebuilt. Source SHA-256 for
`tests/ascs_bsim/client/client.c` was
`9633236db9002f5ba7adf2267f208996047d870d5e75ec1704a17f6d074c480e`
before and after the run (see owner `source-record.json`). Retained
`client-ninja.out` confirms `client.c.obj` compiled and linked into the
new image, SHA-256
`c923372dd81772f7d77c7fa07e3b97455d580ea3f23769b5b682d3b87a1fbf22`.
No client source edits after the run. Receiver image SHA-256 remained
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

Observed generation 1 and negotiated MTU 65. Transaction 1 encoded request
`0301010202f0` yielded expected CP `0301010c00`, ATT write error 0. Both
ASEs stayed QoS Configured, identical full public before/after reads:

- ASE 1, handle 31: `0102000010270000027800051400409c00`.
- ASE 2, handle 34: `0202000110270000027800051400409c00`.

Valid Enable/stream path: 30 successful sends and unregister return 0 on
each stream, receiver rendered 27 complete distinct-stereo pushes. Both
legal Release responses succeeded and public ASE readback ended Idle
(`0100`, `0200`). Owned CP closure by `notify=NULL`, unsubscribe 0 and
`ASCS_CLEANUP generation=1 retired=1 cp_closed=1` preceded client PASS
(`assertions=3 phases=1`); receiver PASS (`phases=1`). Parent returned 0,
so its child wait loop observed no nonzero receiver/client/PHY exit;
individual exit-code fields remain absent from this diagnostic runner.
Scope returned `ok=true`, no timeout, log truncation, cleanup errors, adopted
or live owned descendants. This proves positive metadata case on final
reviewed client source, not timeout/cancel/ACL-disconnect/reconnect behavior.

Only expected runtime warning: `Invalid application error code: 12` once,
paired with expected `Enable rejected: err -22, code 12, reason 0`.
Receiver/client CMake each retained exact three target-specific experimental
Kconfig warnings and one known native-only SoC product notice; no compiler,
assigned-value or incompatible-library link warning in retained logs.

| Evidence | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-wire-owned-owner-r2/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-wire-owned-owner-r2/process-record.json` | `ba4b3358217f4024978817e1a67ae0442a41f37b565502b6f08e1936bf929a2e` |
| `/tmp/opencode/pb051-wire-owned-owner-r2/scope-record.json` | `bab8835eb7b4a9819c70021d65691e5f1ef6c4697fc68ec04e8552e32114d67c` |
| `/tmp/opencode/pb051-wire-owned-owner-r2/source-record.json` | `e525bd93e824c68fab4aa3351ae958b0f1be7ade57132b9bd8c837f69736fdeb` |
| `/tmp/opencode/pb051-wire-owned-r2/client.log` | `5da1d0b192b1063e7fcf98fe3633c62558e72ec49e7d44fe8c6cb82e665910c1` |
| `/tmp/opencode/pb051-wire-owned-r2/receiver.log` | `adba0e6ad5c06f593d27229921a7aeddccb0669833e32bafc1efe3829cb6151d` |
| `/tmp/opencode/pb051-wire-owned-r2/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-wire-owned-r2/client-cmake.out` | `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc` |
| `/tmp/opencode/pb051-wire-owned-r2/client-ninja.out` | `63e8d70c4be1e62a88153c1dd1879098abe063824c0c1b42700db4b615c0db4b` |
| `/tmp/opencode/pb051-wire-owned-r2/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-wire-owned-r2/receiver-ninja.out` | `7ed22269b70d53dab9e1c014b213356384ebc60bad593db22595ec5e7cc0b46a` |

Resolved configs and full peer/build logs remain in exclusive r2 case root.
Twelve native parser tests were not rerun because guard and native tests did
not change. Earlier r1 and all baseline evidence roots remain preserved.
