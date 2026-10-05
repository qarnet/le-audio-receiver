# PB-051: CP subscription fixture repair, single r4 wire diagnostic

## Source-grounded reason

R3 client booted, dynamically found sink ASE 0 and 1, then failed `-EIO`
with zero assertions/phases before any encoded request. Installed Zephyr
`subsys/bluetooth/host/gatt.c:5401-5472` registers another local subscription
and returns zero without CCC write or subscribe callback when an existing
subscription for that value handle already has equal or greater CCC value.
The valid high-level `discover_sinks()` path can already subscribe to CP.
Waiting unconditionally for raw subscription callback is therefore a fixture
error, not evidence of a receiver or production parser defect.

Repair only `tests/ascs_bsim/client/client.c`: dynamically bound CP descriptor
discovery by next characteristic/service end, require one CCC, use its actual
handle for `bt_gatt_subscribe`, retain callback result separately, read the
public CCC as two little-endian bytes with notify bit after subscribing, and
require a real raw CP notification on subsequent encoded exchange. Capture
stage, actual handles, ATT errors, return codes, MTU and generation in logs;
retain all existing request, response, state and recovery oracles. Do not use
private high-level BAP handles or change SDK/production code. Missing CCC,
wrong value or callback error must fail. Previous r1-r3 roots remain intact.

## Single run contract

After `bash -n scripts/ascs-bsim-run.sh` and `git diff --check`, verify both
`/tmp/opencode/pb051-wire-baseline-owner-r4` and
`/tmp/opencode/pb051-wire-baseline-r4` are absent and parent exists. Create
owner root only. Run fixed argv `bash scripts/ascs-bsim-run.sh` by absolute
path, with copied environment, `ASCS_CASE=metadata_length_validation`, new
case root and private verified 32-bit `NIX_LDFLAGS` prefix. Use `run_owned`
inside `DescendantScope`, deadline 900 s, 64 MiB combined log, process/scope
JSON retained. Run once only. Inspect retained build/peer logs, warnings,
binary hashes, actual request/response/state and cleanup. No source guard,
test rewrite, physical run, canonical gate, commit, stage or push. If new
error/warning or no wire exchange, record raw evidence without guessing or
trying another case.

## Single r4 observed result

Both ASCS images built and exported; client dynamically found CP value handle
28, next-characteristic bound 29 and CP CCC descriptor handle 29 inside ASCS
service 27-35. Public CCC read before and after `bt_gatt_subscribe` yielded
`0001` (notify); subscription returned 0 and no callback occurred on the
existing-subscription branch, as installed host implementation permits.
Discovery reported sink ASE IDs/handles `1/31`, `2/34` and negotiated MTU 65
in generation 1. Raw CP notifications then reached the client during valid
codec/QoS setup. No private BAP handle was used to establish the raw CP seam.

At `00:00:07.036884` client sent exact encoded request
`ASCS_REQUEST name=metadata-length-validation generation=1 raw=0301010202f0`.
At `00:00:07.176844` client received real raw CP notification
`ASCS_CP generation=1 raw=0301010000`: opcode 03, count 01, ASE 01,
**success 00/00 rather than required rejection 0c/00**. Client recorded
`ASCS_CLIENT case=metadata_length_validation error=-74 assertions=0 phases=0`
at `client.c:636`, test return code 2. Receiver log shows two configured
ASEs, QoS, then one `<wrn> bt_ascs: Unknown metadata type 0xf0`, followed by
`Enable: stream[0] meta_len 2` and an unfinished test return 1 at exit.
The warning is the installed `ascs.c:2146-2149` unknown-type diagnostic for
this exact case; retained, not granted a general runtime warning waiver.
Because response differed, client stopped before post-rejection ASE readback
and valid-stream recovery. Pre-request QoS setup is observed; *QoS state
preservation and recovery were not proved*. PHY log is empty; runner returned
2, and no individual PHY exit code was recorded. This is a failing encoded
wire baseline only, not PB-051 acceptance or authority to claim physical RF,
parser hardening, or completed matrix.

Each image CMake retained one exact experimental warning for
`BT_LL_SW_SPLIT`, `BT_CTLR_SET_HOST_FEATURE`, and receiver
`BT_CTLR_PERIPHERAL_ISO` or client `BT_CTLR_CENTRAL_ISO`, plus documented
native-only SoC notice. Neither build emitted assigned-value or compiler
warnings, nor `skipping incompatible` linker messages. Previous roots r1-r3
remain untouched. No other case, retry, parser guard, or hardware action.

| Retained artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-wire-baseline-owner-r4/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-wire-baseline-owner-r4/process-record.json` | `89eee346bf16b51ece8caa709322cf2d62d0e8316494bbda1307ffe511fd3485` |
| `/tmp/opencode/pb051-wire-baseline-owner-r4/scope-record.json` | `7b4f43edb3759e5bbf7f6bb5d12ee7327ac66c3b4c3963eaa98a90f01ff3279d` |
| `/tmp/opencode/pb051-wire-baseline-r4/receiver-cmake.out` | `48cf45d6cc951c397676a4c755d8e5c6133e080274cda19d09bfa91444e546e4` |
| `/tmp/opencode/pb051-wire-baseline-r4/receiver-ninja.out` | `47021c5e96cb065cb880554ed0b18e9a70c06bbb0ce9f26b42f2148e11ea5e66` |
| `/tmp/opencode/pb051-wire-baseline-r4/receiver.config` | `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821` |
| `/tmp/opencode/pb051-wire-baseline-r4/client-cmake.out` | `4740a5c407564d3daececf103a565a39987e45f4cc535cf39ff5dab371fe69cc` |
| `/tmp/opencode/pb051-wire-baseline-r4/client-ninja.out` | `ac104cecb668ca4b90fd0790db9dadd7132b069a4e4f125fe76fbd5414396a44` |
| `/tmp/opencode/pb051-wire-baseline-r4/client.config` | `e41bf8290281f90e6d421bb56cdd2828bb9e56830926db3d13314497bc91250b` |
| `/tmp/opencode/pb051-wire-baseline-r4/client.log` | `0b0e93a054d5821f10eff62b99849fa62dedb35c56004a8824e9f58a5928c520` |
| `/tmp/opencode/pb051-wire-baseline-r4/receiver.log` | `2d4f20c637a41550c0cb5e1e61fd844bc2b60c8219278c21439cf5f0a2e5bc` |
| `/tmp/opencode/pb051-wire-baseline-r4/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

Exported receiver image SHA-256
`c3cb486abfe336b3b854cbd4c1508b46e76fc9a66e842d7c0cf0834303dcc3ad`;
client `6939175e5f9784aec724ea32f35612cabe7d3dce92de1895610f2d3c4f493067`.
Process record: return code 2, no timeout, log truncation, signal, group
cleanup error or live group. Scope `ok=true`, no adopted/live descendants.
Next phase: design per-entry length check/guard preserving already delivered
prefix and cancellation; validate independently before treating ASCS response
and post-rejection state/recovery as proved.
