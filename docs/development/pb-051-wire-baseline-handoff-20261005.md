# PB-051: encoded-wire metadata-length baseline (2026-10-05)

## Boundary and prior evidence

Run only `ASCS_CASE=metadata_length_validation` through existing
`scripts/ascs-bsim-run.sh`, without modifying test, application, SDK, or build
configuration. The client discovers actual ASCS CP/ASE handles, sends raw
`03 01 ASE_ID 02 02 F0`, compares CP response `0c00` and preserved QoS,
then attempts valid stream recovery. The receiver builds production BAP,
session and decode and records rendered sink output. This runner is diagnostic
only: six receiver IDs but two client IDs; it has no final case accounting.
Neither this run nor native public-parser evidence proves complete ASCS
acceptance, hardware/RF delivery, or codec conformance. Canonical 17/26 remains
unselected. Existing native 6/1/7 and historical physical padded-logical-input
2/1/3 evidence remain immutable. PB-041 ADC and PB-042 Windows/rights holds
remain unchanged.

## One-shot ownership

Verify `/tmp/opencode` exists and both
`/tmp/opencode/pb051-wire-baseline-owner-r1` and
`/tmp/opencode/pb051-wire-baseline-r1` are absent. Create owner root only.
Use external one-shot Python supervisor in owner root, importing
`scripts/bluez_host_process.run_owned` and
`scripts/bluez_host_descendants.DescendantScope`. Execute fixed argv
`['bash', '/home/thomas-workstation/repos/le-audio-receiver/scripts/ascs-bsim-run.sh']`
under whole-call descendant scope. Pass environment copied from current shell
with `ASCS_OUTPUT_ROOT=/tmp/opencode/pb051-wire-baseline-r1` and
`ASCS_CASE=metadata_length_validation`. `run_owned` timeout 900 seconds,
max combined log 64 MiB, owner `run.log`; persist returned process record
and descendant scope record JSON even on failure. Runner creates fresh child
root and owns receiver/client/PHY logs. Do not retry or run another case.

Inspect build/link output, warning/error text, runtime and any response,
MTU/state/marker evidence actually present, child log hashes and remaining
owned-process status. Expected response mismatch is diagnostic failure, not
acceptance. `ascs.c:2146-2149` logs `Unknown metadata type 0x%02x` for
unknown metadata; retain any instance as case-local evidence, not a general
warning waiver or final warning disposition. Unexplained build/runtime
warnings and failed build must be reported raw, not bypassed. Do not change
SDK, tests, canonical gates, hardware, PB-053, or previous evidence. No
commit, stage, or push. Escalate any provider notification unchanged.

## One-shot outcome and retained failure

Supervisor exited 1 after `run_owned` returned code 1 in 24.9 seconds;
scope completed successfully (no adopted descendants, cleanup errors, timeout,
signal or live owned group). Owner records and combined log are retained in
`/tmp/opencode/pb051-wire-baseline-owner-r1/`:

| Artifact | SHA-256 |
| --- | --- |
| `run.log` | `8db1fe3b0516009b97409280478bea86821edf6d441fdfb7d9b7b2baf18c92a5` |
| `process-record.json` | `95f44cbbbedba8a3aa089a94875f8d00cf162468dc687c7153d6f045787e7c08` |
| `scope-record.json` | `c4f0794de3b1b0595168284d731d1a82e671817f095dbc3ca9810cf966b5bcc1` |

Raw error from `run.log`:

```text
cp: cannot stat '/home/thomas-workstation/ncs/v3.4.1/zephyr/bsim_out/tests/ascs_bsim/receiver/bs_nrf54l15bsim_nrf54l15_cpuapp_ascs_receiver/zephyr/zephyr.exe': No such file or directory
```

Receiver CMake and Ninja logs retained at
`/home/thomas-workstation/ncs/v3.4.1/zephyr/bsim_out/tests/ascs_bsim/receiver/bs_nrf54l15bsim_nrf54l15_cpuapp_ascs_receiver/`:
`cmake.out` SHA-256
`2502259d2eebd47a33b54bc3b145eace51c32afc0e79703b0033ef1e5fe213e8`,
`ninja.out` SHA-256
`d6ec420928f981140fd0054ff9d5b36383555eed281b50c90a076fc425f4bcb9`.
Ninja reached `[412/412]` and `Completed 'receiver'`; static ELF and native
simulator executable were linked, and `receiver/zephyr/zephyr.exe` exists
afterward. The runner's immediate copy still failed. This is consistent with
the runner's background `compile` / `wait_for_background_jobs` sequence, but
no cause or successful copy is established by this attempt. Client build,
runtime validation and peer launch never occurred. Child root
`/tmp/opencode/pb051-wire-baseline-r1/` exists but contains no receiver,
client or PHY logs; no negotiated MTU, encoded request, CP response, ASE state,
child exit code, or test marker was observed.

`cmake.out` retains experimental Kconfig warnings for `BT_LL_SW_SPLIT`,
`BT_CTLR_SET_HOST_FEATURE`, `BT_CTLR_PERIPHERAL_ISO` and the known native-only
unsupported-SoC CMake product notice. `ninja.out:421-428` retains linker
`skipping incompatible` messages for host `libdl`, `libm`, `libgcc_s`,
`libpthread`, `libc`; no general warning waiver or final disposition is
claimed. No second run, alternate case, source/config edit, or wire-baseline
verdict. Diagnose build-copy orchestration and warning disposition in a later
authorized phase, preserving this failed attempt.
