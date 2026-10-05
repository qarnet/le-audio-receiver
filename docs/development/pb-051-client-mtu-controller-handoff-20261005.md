# PB-051: client controller-length repair, single r3 diagnostic (2026-10-05)

## Source boundary

Installed NCS v3.4.1 `zephyr/subsys/bluetooth/controller/Kconfig:611-617`
limits `BT_CTLR_DATA_LENGTH_MAX` to `BT_BUF_ACL_RX_SIZE` when RX size is below
251. ASCS client `prj.conf` sets RX 69 and L2CAP TX MTU 65, but inherited
canonical client overlay requests controller maximum 251. R2 client configure
rejected that assignment, resolved 69 and stopped before any peer execution.
Keep canonical overlay and its ISO 255 settings unchanged. Add ASCS-only
overlay containing the source-grounded maximum 69 and apply it *after* the
canonical overlay in `scripts/ascs-bsim-run.sh`. Do not change advertised MTU,
increase RX, reduce ISO, turn off data length update or edit test bytes.
Previous r1/r2 owner/case roots, native result, physical parser evidence and
prechange build archive remain immutable. Neither this fixture repair nor a
single wire diagnostic constitutes ASCS acceptance.

## One-shot contract

After `bash -n` and `git diff --check`, verify `/tmp/opencode` exists and
`pb051-wire-baseline-owner-r3` and `pb051-wire-baseline-r3` are absent.
Create only the owner root. Execute one external Python supervisor with
`run_owned` entirely inside `DescendantScope`, fixed argv `bash` plus the
repository's `scripts/ascs-bsim-run.sh`, `ASCS_CASE=metadata_length_validation`,
fresh case root, 900 s deadline, 64 MiB combined log. Copy the environment
and prepend the already verified glibc32/gcc32 `-L` paths to `NIX_LDFLAGS`.
Retain process/scope JSON even on failure. Runner retains receiver/client full
CMake, Ninja and config logs; if peers start, retain their receiver/client/PHY
logs. Inspect resolved client ACL RX 69/controller max 69/L2CAP TX MTU 65,
raw warnings, exported binary hashes, and *observed* negotiated MTU/encoded
request/CP response/ASE state and outcome. Treat any new build/runtime warning
or failure as evidence, without another case, retry or suppression. Keep
fixture experimental-symbol and native-only notices visible. No parser guard,
production/SDK/hardware/PB-053/canonical changes, commit, stage or push.

## Actual single r3 outcome

One run completed build and executable export for receiver and client, then
ran receiver, client and PHY. Client resolved config: `BT_BUF_ACL_RX_SIZE=69`,
`BT_CTLR_DATA_LENGTH_MAX=69`, `BT_L2CAP_TX_MTU=65`,
`BT_CTLR_ISO_TX_SDU_LEN_MAX=255`. Receiver retained its independent
`255/251/65/255` config values in the same order. Both CMake logs retain one
each `BT_LL_SW_SPLIT` and `BT_CTLR_SET_HOST_FEATURE` experimental warning;
receiver has one `BT_CTLR_PERIPHERAL_ISO`, client one `BT_CTLR_CENTRAL_ISO`.
Each retains the documented native-only SoC support notice. No assigned-value,
compiler or incompatible-library link warning found in retained build logs.

**First runtime failure occurred before encoded request.** `client.log` shows
Bluetooth initialization, sink ASE 0 at `00:00:03.396824`, sink ASE 1 at
`00:00:03.956824`, then exactly:

```text
d_01: @00:00:10.636924 ERROR: (CMAKE_SOURCE_DIR/client.c:534): ASCS_CLIENT case=metadata_length_validation error=-5 assertions=0 phases=0
d_01: @00:00:10.636924  The TESTCASE FAILED (test return code 2)
```

`receiver.log` shows connection/pairing and `TESTCASE NOT PASSED at exit
(test return (1) indicates it was still in progress)`. `phy.log` is empty.
Runner returned 2; per-peer logs indicate client test code 2 and receiver
unfinished test return 1. PHY exit code is not individually recorded by this
diagnostic runner. No logged negotiated MTU, raw encoded request, CP response,
ASE state comparison, valid recovery, or ASCS verdict. Do not infer exact
cause of client `-5` from these logs or call this an exact-end parser wire
baseline. No additional run or fixture change in this handoff.

| Retained artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-wire-baseline-owner-r3/run.log` | `2d9b41d8f4fa1af1ee879f875876da1ebadcf575270b75881a055fb6cc12b5bd` |
| `/tmp/opencode/pb051-wire-baseline-owner-r3/process-record.json` | `9f50297dcc92dcaead2090fec0f1a67465e7f9a4662c7acc7add88332b85dc24` |
| `/tmp/opencode/pb051-wire-baseline-owner-r3/scope-record.json` | `7796fff1b02fbe10870ea9315f3b217f178a9768f1200cc8a866c03f674d5d01` |
| `/tmp/opencode/pb051-wire-baseline-r3/receiver-cmake.out` | `48cf45d6cc951c397676a4c755d8e5c6133e080274cda19d09bfa91444e546e4` |
| `/tmp/opencode/pb051-wire-baseline-r3/receiver-ninja.out` | `fad901acd6a179b25a31ea0e241dc8deee077f72fbf519f5e2917f2df184e13a` |
| `/tmp/opencode/pb051-wire-baseline-r3/receiver.config` | `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821` |
| `/tmp/opencode/pb051-wire-baseline-r3/client-cmake.out` | `61a5aee67de12e586d0946ac161fe4e9da8d9a288ee7936430a04c1c62a5d849` |
| `/tmp/opencode/pb051-wire-baseline-r3/client-ninja.out` | `7bda6c2b2f724a3c53c88e9116cda05df8fd2529f5d0594306c15455f68577fd` |
| `/tmp/opencode/pb051-wire-baseline-r3/client.config` | `e41bf8290281f90e6d421bb56cdd2828bb9e56830926db3d13314497bc91250b` |
| `/tmp/opencode/pb051-wire-baseline-r3/receiver.log` | `4e876a7802e3d57a596ae06aadc19f59457c26e2c5868d4ee4f1da3e80f673ce` |
| `/tmp/opencode/pb051-wire-baseline-r3/client.log` | `6e0ce0c877d41eb33f14e0410677307702c3d80083723c5c32fb38ef5f61dd8d` |
| `/tmp/opencode/pb051-wire-baseline-r3/phy.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

Exported image SHA-256 after run: receiver
`c3cb486abfe336b3b854cbd4c1508b46e76fc9a66e842d7c0cf0834303dcc3ad`,
client `8d9dc35b785fd96d5ef508da94811b9d574f99ba6c8ebe194af3129c87cfcf45`.
Process record: return code 2, no timeout, truncated log, signal, cleanup
error or remaining owned group. Descendant scope `ok=true`, no adopted or
remaining live descendants. Next phase needs read-only diagnosis of client
`-5` before any further scoped run; previous roots and results stay intact.
