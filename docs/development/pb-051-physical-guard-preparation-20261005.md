# PB-051 physical LTV guard: fresh read-only identity and image preparation

Scope: one external preparation root
`/tmp/opencode/pb051-ltv-physical-guard-r1`. No flash, reset, erase,
console open/write, RF stream, I2S output, or physical parser verdict.
Receiver/source role and DAC presence are **not** inferred. This image is
a standalone CPUAPP public-parser ztest diagnostic for later
identity-checked use, not production streaming firmware. Do not reuse this
session's probe/tty pairing as a permanent board table or as later
target-changing authorization.

## Current session's raw identity

`01-nix-nrf-probes.stdout` SHA-256
`e86deca242fea197215d5cfaf9853a675b7b1c2b009574e7e538665cdad1f79c`
lists two explicit XIAO CMSIS-DAP targets. Both named serials
`8EE9B3FF` and `158D8E1D` returned, by separate bounded
`fingerprint_cmsis_dap` OpenOCD commands, identical raw DPIDR
`0x6ba02477`, AP0 `0x84770001`, AP1 `0x84770001`, AP2
`0x32880000`, AP3 `0x00000000`, FICR PART `0x00054b15`, FICR
VARIANT `0x41414330` (AAC0). Both commands exited 0 with no
OpenOCD warning/failure. Individual raw stdout/stderr/argv/exit records
are `02-fingerprint-8EE9B3FF.*` and
`03-fingerprint-158D8E1D.*`; each raw stdout SHA-256
`5cb809ca4238703ae37016d5182b380453a4e9e371b8cd676434f4bef0c4e559`.

Explicit **diagnostic candidate only** `158D8E1D` matched fresh
`udevadm info --query=property --name /dev/ttyACM3` VID `2886`,
PID `0066`, serial `158D8E1D`, USB interface `02`; raw stdout
SHA-256 `645c9b56f69407d01ed7b6aaa07036498c63e9b78e52cfb80a66572f9c05d3a9`.
`postbuild-usb-resolution.json` SHA-256
`365d3b1b1e538f89695fae0fe986442f0d0c9c883f81aad51a9bc0eb6749e5bc`
binds actual tty sysfs node to its USB interface and uniquely nearest
matching physical USB device `/usb3/3-1`; `/usb3` is the **different**
Linux root hub (VID `1d6b`, PID `0002`), not a second ambiguous DUT.
Full unshortened ancestry is in `candidate-usb-ancestry.json` SHA-256
`b88889a71c89368e06466e7dccc2044020309b6e5de555958d08da7930efd4a1`.
No console was opened; no role provisioning was attempted.

## CPUAPP test image, not executed on board

One owned build command, from repository root, returned 0:

```sh
west build --no-sysbuild -b xiao_nrf54l15/nrf54l15/cpuapp -d /tmp/opencode/pb051-ltv-physical-guard-r1/build tests/unit/ltv_bounds -- -DEXTRA_CONF_FILE=/tmp/opencode/pb051-ltv-physical-guard-r1/physical.conf -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y
```

External `physical.conf` contains **only**
`CONFIG_NRF_PLATFORM_LUMOS=n`. `build-process.json` retains exact argv,
return code and bounded merged output identity. `build.log` SHA-256
`17928e9db13046891d84a45e5d2b4ecaed64aab4abf45c60714eb875c8f9ec44`
contains no compiler, Kconfig, CMake or incompatible-linker warning.
Resolved config pins `CONFIG_ZTEST=y`; `CONFIG_BT`, `CONFIG_I2S`, and
`CONFIG_NRF_PLATFORM_LUMOS` are unset. No physical test execution
occurred, so **12 compiled test methods are not 12 passing board tests**.

Built HEX SHA-256
`54c236f21759d6326db427c149825599be7a5507c231655d100ff69c8439177c`
(`build/zephyr/zephyr.hex`); ARM little-endian ELF SHA-256
`f3514949805634306ed81ffdc4db8cd2954b04cfb0edebbb94836cdb4fc29425`
(`build/zephyr/zephyr.elf`); map SHA-256
`3d3a21c7f1ec37d834f18b6286c43131155e17b72d0be85fa50c458b41074ea6`.
Archive `app/libapp.a` retains test object's undefined public
`bt_audio_data_parse`, repository guard's defined `__wrap_` and
undefined `__real_`, and installed SDK `audio.c`'s defined real parse.
Final ELF has real parser at `0x950a` and wrapper at `0x956a`;
ARM disassembly shows test calls to wrapper and wrapper branch-and-link
to real parser. Actual `build/build.ninja` links
`-Wl,--wrap=bt_audio_data_parse`. Inspect
`09-app-symbols.stdout`, `10-elf-symbols.stdout`,
`11-elf-disassembly.stdout` and `build/zephyr/zephyr.map` for the
raw proof. Seven source identities, including actual installed SDK
`audio.c`, are byte-identical before and after build; both SDK HEAD pins
matched, with tracked Zephyr and nrf status empty before and after.

## Retained diagnostic correction, no rebuild or target action

Initial `prep.py` wrote `preparation-result.json` with *two supervisor
postchecks failing after a successful build*: it incorrectly treated the
root USB hub as a second DUT, and read nonexistent unsuffixed symbol
paths instead of its own `09-`/`10-`/`11-` captured outputs. Preserve
that record; it is not a successful end-to-end verifier result. Separate
read-only `verify.py` retained `postbuild-verification.json` (artifact
proof passed; its first tty-sysfs check assumed the `device` link led
to the tty node). The subsequent read-only `verify-usb.py` used the
actual tty sysfs node plus its `device` link to prove the unique
USB parent without replaying SWD, rebuilding, flashing or touching UART.
No existing raw output or history was rewritten. Artifact proof lives in
`postbuild-verification.json`, and final session-only USB proof in
`postbuild-usb-resolution.json`; read them together. A later flash needs
fresh identity again and a separately authorized target-action owner.
