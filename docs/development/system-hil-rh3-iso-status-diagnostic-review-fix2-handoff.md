# RH3 ISO-status diagnostic native-sim warning fix handoff

Status: focused software-only review correction. This follows
`system-hil-rh3-iso-status-diagnostic-review-fix-handoff.md`; it exists only
because focused native test startup still emitted avoidable environment-driver
warnings.

## Grounding

After Kconfig assignment repair, focused native test passed 47/47 but startup
printed:

```text
WARNING: Using a test - not safe - entropy source
Warning: Bluetooth device missing.
Specify either a local hci interface --bt-dev=hciN,
a UNIX socket --bt-dev=/tmp/bt-server-bredrle
or a valid hci tcp server --bt-dev=ip_address:port
```

These are not diagnostic behavior under test.

Exact NCS v3.3.0 grounding:

- `zephyr/boards/native/native_sim/native_sim.dts` enables
  `zephyr,bt-hci-userchan` for native_sim.
- `zephyr/drivers/bluetooth/hci/Kconfig` defaults `BT_USERCHAN=y` when
  `CONFIG_BT=y` on native_sim; `drivers/bluetooth/hci/userchan.c:424-444`
  prints Bluetooth-device-missing at device init without `--bt-dev`.
- Audio-stream-session unit test needs only unconditional public BAP enum
  declarations through `tests/bsim/src/bsim_observer.h`. Its local
  `CONFIG_BSIM_SINK_POOL_LIMIT=2` deliberately avoids the production BT ASCS
  Kconfig dependency. It makes no Bluetooth API call.
- `drivers/entropy/fake_entropy_native_sim.c:73-94` prints test-entropy warning
  when native fake entropy driver initializes.
- `nrf/Kconfig.nrf:119-124` weakly implies `ENTROPY_GENERATOR` on native_sim.
  A user assignment `CONFIG_ENTROPY_GENERATOR=n` overrides that imply.
- With `CONFIG_BT` absent, no BT host crypto / PSA / CSPRNG chain selects
  entropy back on. Unit sources do not use random APIs.

`nix develop` will still print external dirty-tree notice because worktree is
intentionally dirty. That notice is not compiler, Kconfig, firmware, or test
runtime output and cannot be removed without prohibited worktree cleanup.

Existing named negative-path unit tests deliberately emit
`audio_stream_session` `<wrn>` logs while proving recovery/error handling.
They are pre-existing test observations, not boot diagnostics. Do not mute,
relax, or change them in this phase.

## Scope

Only these files may change:

- `tests/unit/audio_stream_session/prj.conf`
- this handoff document, if a factual correction becomes necessary

## Exact change

Replace current Bluetooth-only comment/config block in
`tests/unit/audio_stream_session/prj.conf` with:

```conf
# BSim observer uses unconditional public BAP enum declarations only. This
# suite has no Bluetooth or random runtime dependency; keep native_sim from
# opening a user-channel HCI device or enabling fake entropy.
CONFIG_ENTROPY_GENERATOR=n
```

Remove `CONFIG_BT=y`. Do not add `CONFIG_BT_DRIVERS=n`, `CONFIG_BT_HOST_CRYPTO`,
`CONFIG_FAKE_ENTROPY_NATIVE_SIM`, a Bluetooth role, a controller, an HCI device,
or command-line `--bt-dev` workaround.

No production Kconfig or source changes.

## Required verification

Run from repository root in existing NCS v3.3.0 dev shell using a fresh,
outside-repository build directory retained for diagnosis:

```bash
build_dir="$(mktemp -d /tmp/le-audio-receiver-rx-status-review2.XXXXXX)"
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d "$build_dir" tests/unit/audio_stream_session -p -t run
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
python3 -m py_compile scripts/hil/receiver.py tests/hil/hil_fakes.py tests/hil/rh2_test.py
```

Acceptance:

- 47 native tests pass.
- No Kconfig assigned-value warning, compiler warning, test-entropy warning,
  or Bluetooth-device-missing startup warning.
- Fake-lab suite, matrix checker, Python compile, and whitespace check pass.
- Existing named negative-path `<wrn>` lines may remain only where tests
  intentionally exercise those production error branches. Do not suppress them.

No full gate, firmware build, HIL, flash, serial, reset, pairing, or hardware
command.

## Constraints

Preserve all unrelated dirty changes. No commit, push, merge, PR, reset, stash,
cleanup, `STATUS.md` edit, threshold change, parser change, source change, or
production Kconfig/devicetree change. Stop and report if `ENTROPY_GENERATOR=n`
is rejected or any new warning remains.
