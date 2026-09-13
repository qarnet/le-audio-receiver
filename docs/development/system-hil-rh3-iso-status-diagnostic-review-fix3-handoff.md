# RH3 ISO-status diagnostic native header-config fix handoff

Status: focused correction after review-fix2. Do not change diagnostic logic or
start hardware work.

## Corrected grounding

Removing `CONFIG_BT` prevented native user-channel startup, but failed compile:

```text
error: ‘CONFIG_BT_BUF_EVT_RX_COUNT’ undeclared here
error: ‘CONFIG_BT_BUF_ACL_TX_COUNT’ undeclared here
error: expression in static assertion is not an integer
```

`zephyr/include/zephyr/bluetooth/audio/audio.h` unconditionally includes
`zephyr/bluetooth/buf.h`; its static assertion needs generated Bluetooth buffer
Kconfig macros. Header-only BAP enum use therefore needs `CONFIG_BT=y`, but no
Bluetooth runtime driver or crypto stack.

Exact NCS v3.3.0 valid test-only configuration:

```conf
CONFIG_BT=y
CONFIG_BT_DRIVERS=n
CONFIG_BT_HOST_CRYPTO=n
CONFIG_ENTROPY_GENERATOR=n
```

Grounding:

- `CONFIG_BT=y` supplies default buffer values: event RX `10`, ACL TX `3`.
- `BT_DRIVERS` depends only on `BT`, has default `y`, and no selector forces it;
  explicit `n` prevents native user-channel source/boot warning.
- `BT_HOST_CRYPTO` otherwise defaults on and selects PSA/Mbed TLS/CSPRNG;
  explicit `n` removes that entropy selection path.
- NCS native_sim only weakly implies `ENTROPY_GENERATOR`; explicit `n` wins and
  prevents fake entropy boot warning.
- Suite calls no Bluetooth runtime API. This configuration is invalid only for
  a runtime Bluetooth test, not this header/type-only test seam.

## Scope

Only `tests/unit/audio_stream_session/prj.conf` and this handoff document may
change.

## Exact change

Replace current comment/config block after `CONFIG_BSIM_SINK_POOL_LIMIT=2` with
exactly:

```conf
# BSim observer needs public BAP enum declarations, whose headers require BT
# buffer defaults. This suite has no Bluetooth or random runtime dependency;
# keep native_sim from opening a user-channel HCI device or fake entropy.
CONFIG_BT=y
CONFIG_BT_DRIVERS=n
CONFIG_BT_HOST_CRYPTO=n
CONFIG_ENTROPY_GENERATOR=n
```

Do not add explicit buffer counts, BT role/controller/ISO settings,
`CONFIG_BT_HCI_RAW`, `--bt-dev`, fake entropy settings, or production changes.

## Required verification

Use fresh outside-repository build directory and retain it:

```bash
build_dir="$(mktemp -d /tmp/le-audio-receiver-rx-status-review3.XXXXXX)"
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d "$build_dir" tests/unit/audio_stream_session -p -t run
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
python3 -m py_compile scripts/hil/receiver.py tests/hil/hil_fakes.py tests/hil/rh2_test.py
```

Acceptance requires no Kconfig assigned-value, compiler, Bluetooth-device, or
test-entropy warning. Existing named negative-path `audio_stream_session`
`<wrn>` output remains expected test evidence; do not mute it. Nix dirty-tree
notice is external shell state from required dirty-worktree preservation.

No full gate, firmware build, HIL, flash, serial, reset, pairing, or hardware
command. No commit, push, reset, stash, cleanup, `STATUS.md`, source, parser,
matrix, production Kconfig, or devicetree edit. Stop and report if any new
warning/error remains.
