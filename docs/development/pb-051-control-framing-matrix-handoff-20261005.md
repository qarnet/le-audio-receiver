# PB-051: encoded ASCS control-frame validation family

## Bounded case contract (2026-10-05)

Complete only `control_frame_validation`, already registered by the receiver.
Keep SDK, production, per-entry guard, metadata, codec/QoS, lifecycle, dual
and canonical 17/26 cases unchanged. Use dynamically discovered ASE A/B IDs,
MTU 65 and a public raw GATT write for every invalid request. Installed
`ascs.c:2998-3068` returns an ASCS CP notification for encoded unknown
opcode or malformed structure, and `is_valid_num_ases:1786-1806` bounds
exposure. These are not empty-opcode or invalid-ATT-offset cases.

Seven required inputs and exact global notification shape
`[opcode, ff, 00, response_code, 00]` (five bytes, no local filtering):

| Case | Raw request (dynamic IDs where marked) | Response |
| --- | --- | --- |
| control_unknown_opcode | `ff01` | `ff ff 00 01 00` |
| control_zero_ase_count | `0300` | `03 ff 00 02 00` |
| control_missing_codec_record | `0101` | `01 ff 00 02 00` |
| control_metadata_outer_truncated | `0301 A 03 02f0` | `03 ff 00 02 00` |
| control_release_trailing | `0801 A 00` | `08 ff 00 02 00` |
| control_count_above_exposure | `0803 A B 00` | `08 ff 00 02 00` |
| control_missing_count | `05` | `05 ff 00 02 00` |

Each starts with both public ASEs Idle, snapshots their full raw values,
requires the ordered exact global CP response and byte-identical readback,
then performs fresh valid 120-octet two-CIS, 30 sends per stream and
independently rendered distinct stereo recovery, checked Release and Idle.
Record one ordered CASE_BEGIN/CASE_END per step; exactly seven steps,
21 checked CP exchanges and seven rendered phases before client PASS.
Retain actual SDK warnings and case-local cause. No blanket suppression or
claim about MTU 64, RF corruption, physical reception or additional families.

## One-shot verification

Before running, bash syntax/diff check and verify `/tmp/opencode` exists,
`pb051-control-framing-owner-r1` and `pb051-control-framing-r1` absent.
Create owner root only. Run fixed `ASCS_CASE=control_frame_validation` via
existing ASCS shell runner under `run_owned` inside `DescendantScope`, 900 s
deadline, 64 MiB combined log and private verified ELF32 link-path prefix.
Retain complete build/config/peer logs and process/scope/source records.
No second run, post-run source edit, unrelated case, hardware, full gate,
stage, commit or push. Unexpected error/warning stays raw evidence; do not
relax response, state or recovery checks to pass.

## Actual single r1 result

One focused `ASCS_CASE=control_frame_validation` built and returned code 0.
Client reported `ASCS_CLIENT case=control_frame_validation cases=7
assertions=21 phases=7`, receiver `ASCS_RECEIVER phases=7`; seven ordered
CASE_BEGIN/CASE_END entries, seven fresh rendered receiver sink phases,
14 exact 30-send TX counts and 14 zero unregister results are in raw logs.
Each negative retained byte-identical full Idle snapshots of ASE 1 (`0100`)
and ASE 2 (`0200`) before/after and followed with actual valid dual-CIS
rendered playback, checked Release and Idle. Negotiated MTU 65,
generation 1, dynamic IDs 1 and 2. No other case ran.

| Step | Raw request | Actual five-byte CP notification |
| --- | --- | --- |
| control_unknown_opcode | `ff01` | `ffff000100` |
| control_zero_ase_count | `0300` | `03ff000200` |
| control_missing_codec_record | `0101` | `01ff000200` |
| control_metadata_outer_truncated | `0301010302f0` | `03ff000200` |
| control_release_trailing | `08010100` | `08ff000200` |
| control_count_above_exposure | `0803010200` | `08ff000200` |
| control_missing_count | `05` | `05ff000200` |

All were encoded raw writes, not locally rejected high-level helper calls.
Five source-specific warnings appeared exactly once each on the receiver:
`Number_of_ASEs parameter value is less than 1` (zero count),
`Malformed params array` (missing Codec record), `Malformed metadata`
(outer truncation), `Number_of_ASEs mismatch` (trailing Release byte),
and `Invalid length 0 < 1` (missing count). Unknown opcode and count above
exposure had no runtime warning (installed LOG_DBG paths). No other runtime
warning/error. Peer CMake logs retained the three known target-specific
experimental controller symbols and native-only SoC notice; no compiler,
assigned-value or incompatible-library link warning appeared.

`client.c` SHA-256 remained
`59227c56ab16719001e44a1c55ff573a4b9d00d65f365d4dc03729b65a3152e7`
before/after run. Client image SHA-256
`2374667e0604cdb217416e268e5bbece42a71890cb6c4e0b086735800e8a4a94`;
receiver image SHA-256
`f5b0c1c3bb4b4bece9b04adb0bee48a1de6083fc2968191d8d600b30c68e9e6e`.

| Retained artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-control-framing-owner-r1/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-control-framing-owner-r1/process-record.json` | `410ceca6ea8c9fe1a6545dd1555de311375077ce1a2a6bbd8b40924329cbdaf8` |
| `/tmp/opencode/pb051-control-framing-owner-r1/scope-record.json` | `4696f3a71c37e14625a463dc363273d6fc094b399b0ddc0b1a04bd541f2ac732` |
| `/tmp/opencode/pb051-control-framing-owner-r1/source-record.json` | `387ea24d707ce0f89d863cf9eedc7b290d7d04482bddfce49b8e30116f3bc2ec` |
| `/tmp/opencode/pb051-control-framing-r1/client.log` | `c06b54663d77f2e78eacfc0f09b954ebf42e454e781c80cdc21f5329430a5df9` |
| `/tmp/opencode/pb051-control-framing-r1/receiver.log` | `642c4e369ad569757f2142d2e44d4ba360aebfb9e385b67c6afb2775dad1635a` |
| `/tmp/opencode/pb051-control-framing-r1/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `/tmp/opencode/pb051-control-framing-r1/client-cmake.out` | `49d5226d38e7fc6fd274b986cb4759d5ad8e5b5b0403e4a54f77989ceee36bc6` |
| `/tmp/opencode/pb051-control-framing-r1/client-ninja.out` | `4e55a810076fd1c9a0462e304f1076bbe1eccc1c7b9d8d18f921a05d782aae5e` |
| `/tmp/opencode/pb051-control-framing-r1/receiver-cmake.out` | `2502259d2eebd47a33b54bc3b145eace51c32afc0e79703b0033ef1e5fe213e8` |
| `/tmp/opencode/pb051-control-framing-r1/receiver-ninja.out` | `96b817649088f1b397d0f5303b908e13104bb690f797106c50043a8bbebceacc` |

Resolved config and raw peer logs stay in the exclusive case root; owner
process and scope both report `ok=true`, no timeout, truncation, signal,
cleanup error or live owned descendants. Separate child numeric exit codes
are not recorded by this diagnostic runner; its child waits returned zero.
No retry, post-run client edit or full PB-051 acceptance. Reconnect,
ownership negative controls and strict required-case accounting remain.
