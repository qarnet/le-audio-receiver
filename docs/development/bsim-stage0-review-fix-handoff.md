# BabbleSim Stage 0 review fixes

## Script correctness

- `bsim-env.sh`: NCS root is `realpath "$ZEPHYR_BASE/.."`, not dirname twice.
  Default output `${NCS_ROOT}/tools/bsim`; validate PHY plus core simulator
  binaries actually used, not unrelated optional devices.
- Remove all hardcoded `/nix/store/...` paths. Require toolchain already active;
  optionally invoke `nrfutil ... env` only via PATH with checked command.
- Pass compile arguments as a scalar string understood by `compile.source`,
  including explicit `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n` with recorded reason
  for host `_FORTIFY_SOURCE` at `-O0`.
- Smoke script must propagate every child failure; never `exit 0` after failed
  test.
- Use official ACL-disconnect client/server test IDs with PHY sim length 110e6
  for accepted baseline. Keep normal disable-race run as documented upstream
  observation, not passing gate.
- Correct docs: official main preset is 16 kHz/10 ms/40 octets, radio BSim tests
  use dedicated scripts and are explicitly not Twister-run.
- Ignore root-generated `/flpr_hang_gate_*.log` or move future logs to `/tmp`;
  leave repository status clean.

## Gate

Recompile official binary, run corrected ACL-disconnect smoke at least twice with
unique simulation IDs. Require all client/server/PHY exits zero both times.
Commit fixes/docs/results. No receiver scenario changes, component revision
changes, downloads, packages, push, or hardware.
