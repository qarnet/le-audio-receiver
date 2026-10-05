# PB-051: sysbuild export repair and one wire baseline retry (2026-10-05)

## Source-grounded diagnosis

The first one-shot attempt is immutable at
`/tmp/opencode/pb051-wire-baseline-owner-r1` and its empty case root. Earlier
`pb-051-wire-baseline-handoff-20261005.md` speculated that background build
ordering caused the failed copy. This is incorrect: the child executable was
present at `receiver/zephyr/zephyr.exe`, but outer `zephyr/zephyr.exe` was
missing. Installed NCS v3.4.1
`zephyr/share/sysbuild/cmake/modules/native_simulator_sb_extensions.cmake:17-26`
copies the child image to that outer path only when
`native_simulator_set_final_executable(${DEFAULT_IMAGE})` is called.
`tests/bsim/sysbuild.cmake` contains that call and
`native_simulator_set_primary_mcu_index(${DEFAULT_IMAGE})`; both ASCS apps
need the same small sysbuild file. Do not modify the SDK or canonical cases.

Before another build, preserve the previous receiver build's `cmake.out`,
`ninja.out`, resolved child `receiver/zephyr/.config` and source SHA-256/path
record in exclusive `/tmp/opencode/pb051-build-repair-prechange-r1`. Do not
overwrite earlier evidence. Add the two sysbuild files. Change the ASCS runner
only to retain each receiver/client build's full cmake, ninja and resolved
child config files in its fresh case output root, including on compile failure.
Keep each existing build error and case choice unchanged.

## Warning boundary and bounded retry

The previous receiver build reported `BT_LL_SW_SPLIT`,
`BT_CTLR_SET_HOST_FEATURE` and `BT_CTLR_PERIPHERAL_ISO` experimental Kconfig
symbols. They are deliberate test-fixture controller selections (the client
uses `BT_CTLR_CENTRAL_ISO`); retain `CONFIG_WARN_EXPERIMENTAL=y` and their raw
exact warnings. This is not production SDK qualification or a blanket warning
waiver. The native-only unsupported-SoC CMake notice also stays visible.
First build's final ELF32 runner link logged eight `skipping incompatible`
messages: Nix wrapper searched 64-bit libraries before installed 32-bit
ones. For this private retry only, prepend `-L` flags for verified
`/nix/store/yhawd8dka2563b5mg3vjm5h14sw5lv95-glibc-multi-2.40-224/lib/32`
and `/nix/store/yygma80xg8axc2df157lvdnf181zhx7s-gcc-14.3.0-lib/lib`
to copied environment's `NIX_LDFLAGS`, preserving old flags. Do not hardcode
this in repository source. Stop/report if incompatible-library messages remain
or any unrelated compiler, assigned-value or runtime warnings appear; a
portable linker-path repair for future canonical integration remains pending.

With `/tmp/opencode/pb051-wire-baseline-owner-r2` and
`/tmp/opencode/pb051-wire-baseline-r2` both absent, create owner root only.
Use one external Python supervisor with `run_owned` inside `DescendantScope`,
fixed `bash /home/thomas-workstation/repos/le-audio-receiver/scripts/ascs-bsim-run.sh`,
`ASCS_CASE=metadata_length_validation`, 900 s deadline, 64 MiB combined
owner log and JSON process/scope records. No other case or retry. Inspect
outer exported executables after builds, receiver/client CMake/Ninja warnings,
wire request/response/state/MTU/stream recovery if peers reach that point,
peer logs, and ownership closure. The expected exact-end metadata rejection
failure is diagnostic evidence only, not acceptance or permission for a parser
guard. No production, SDK, hardware, PB-053, canonical gate, commit or push.

## Preserved prechange build and actual retry result

`/tmp/opencode/pb051-build-repair-prechange-r1/` retains receiver
`cmake.out`, `ninja.out`, `receiver.config` and source path/hash `record.json`
(record SHA-256 `e8630fda024566b808cc01ee2fed4d167a4a60cb9e6db5043a32ee410d817b93`).
Previous cmake and ninja digests match the first attempt; previous config
SHA-256 `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821`.

Single r2 supervisor run returned 1 after 19.2 seconds: receiver built,
linked and exported to outer `zephyr/zephyr.exe` successfully (also copied
to `tools/bsim/bin`). Its full retained `receiver-ninja.out` contains no
`skipping incompatible` messages with the private linker search-order flags.
Client configure stopped before Ninja due to an **assigned-value Kconfig
warning** in the inherited client overlay, not due to ASCS behavior:

```text
warning: BT_CTLR_DATA_LENGTH_MAX (defined at /home/thomas-
workstation/ncs/v3.4.1/nrf/modules/../samples/common/mcumgr_bt_ota_dfu/Kconfig:125, /home/thomas-
workstation/ncs/v3.4.1/nrf/modules/../subsys/bluetooth/fast_pair/Kconfig.fast_pair:125,
subsys/bluetooth/controller/Kconfig:611) was assigned the value '251' but got the value '69'. See
warning: user value 251 on the int symbol BT_CTLR_DATA_LENGTH_MAX (defined at /home/thomas-workstation/ncs/v3.4.1/nrf/modules/../samples/common/mcumgr_bt_ota_dfu/Kconfig:125, /home/thomas-workstation/ncs/v3.4.1/nrf/modules/../subsys/bluetooth/fast_pair/Kconfig.fast_pair:125, subsys/bluetooth/controller/Kconfig:611) ignored due to being outside the active range ([27, 69]) -- falling back on defaults
error: Aborting due to Kconfig warnings
```

The source assignment is `tests/bsim/client/overlay-bt_ll_sw_split.conf:3`.
No config or test change and no retry under this handoff. Receiver CMake
retains one warning each for `BT_LL_SW_SPLIT`, `BT_CTLR_SET_HOST_FEATURE`,
`BT_CTLR_PERIPHERAL_ISO` and the documented native-only SoC notice. Client
CMake retains one each for `BT_LL_SW_SPLIT`, `BT_CTLR_SET_HOST_FEATURE`,
`BT_CTLR_CENTRAL_ISO`, plus the two assignment/range warnings above. No
compiler warning observed in completed receiver build. `client-ninja.out` and
`client.config` do not exist because configure failed; runner reported both
missing. The new case root contains retained build files only, no peer logs;
no runtime validation, negotiated MTU, encoded request, response, ASE state,
valid recovery, or test markers occurred.

| Artifact | SHA-256 |
| --- | --- |
| `/tmp/opencode/pb051-wire-baseline-owner-r2/run.log` | `2ecb01cc5a1cba8f6cc68e5a83bc07ced98adcc4c7947f27be1694d375421106` |
| `/tmp/opencode/pb051-wire-baseline-owner-r2/process-record.json` | `a1a6dd073c9b7928077cf59677aa470e678322b67a2759f164dce609e2d4d046` |
| `/tmp/opencode/pb051-wire-baseline-owner-r2/scope-record.json` | `1ae007bd992e61d0a0fa252e89a69f3107a2ee34c704ebc0cb6e3573531567b3` |
| `/tmp/opencode/pb051-wire-baseline-r2/receiver-cmake.out` | `c8629eac09c3755440d30066a084f3461835ac07d343e381fbc767bfe394459d` |
| `/tmp/opencode/pb051-wire-baseline-r2/receiver-ninja.out` | `b80b3635a521be539a08ec3bf1e24980fb05d2f5354326b08e5ff1fc91c76cfe` |
| `/tmp/opencode/pb051-wire-baseline-r2/receiver.config` | `95a9e883abc0e17225e3d453f3036216b498d2ea1ec1d41bbfd65c41066c5821` |
| `/tmp/opencode/pb051-wire-baseline-r2/client-cmake.out` | `025ef9d52dd8054f4441773e3b9e68f70805fae1b405ff911c61d9d155f3ac56` |

Process record: returncode 1, no timeout, truncation, signal, cleanup error,
or live group. Scope record: `ok=true`, no adopted or live descendants.
Follow-up: source-grounded client controller-length/config repair and fresh
exclusive run, with previous roots intact. Portable ELF32 link-order repair
for later canonical integration remains pending; this private run does not
license hardcoded Nix store paths in repository code.
