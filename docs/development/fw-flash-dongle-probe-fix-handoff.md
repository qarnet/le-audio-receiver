# `fw-flash-dongle` probe-selection fix handoff

## Goal

Fix discovered public tooling defect in `scripts/bin/fw-flash-dongle`: script
uses J-Link backend but resolves any nRF53 target through `nrf-probes`, which
only reports CMSIS-DAP probes in this lab.  It therefore selects E83 receiver's
Pico CMSIS-DAP serial and passes it to `interface/jlink.cfg`, producing
`No J-Link device found`.

R1 is accepted at `c788a8d`; this is narrow post-review tooling repair before
R2.  No receiver firmware behavior changes.

## Grounding

- `scripts/bin/fw-flash-dongle:48-50` calls `nrf-probes --find nrf53`.
- Same script always invokes `-f interface/jlink.cfg` because nRF5340DK dongle
  uses onboard Segger J-Link.
- R1 raw evidence proved default J-Link auto-detection finds
  `J-Link OB-nRF5340-NordicSemi`, DPIDR `0x6ba02477`, and flashes/verifies both
  cores successfully.
- `nrf-probes --help` exposes only CMSIS-DAP target discovery; it does not
  enumerate/select J-Link probes.
- Shared `scripts/probe-serial.local` is receiver-target override and may name
  a CMSIS-DAP probe.  It must not control J-Link dongle flashing.

## Exact implementation

### Script

Edit `scripts/bin/fw-flash-dongle`:

1. Remove all use of `scripts/probe-serial.local` and `nrf-probes`.
2. Default to J-Link auto-detection: no `adapter serial` OpenOCD command.
3. Support one explicit, session-local override environment variable:

   ```bash
   FW_DONGLE_JLINK_SERIAL=<jlink-serial> fw-flash-dongle
   ```

4. When override is non-empty, validate it against
   `^[[:alnum:]_.:-]+$`; reject invalid values with clear error before
   OpenOCD.  Add exactly one `-c "adapter serial $FW_DONGLE_JLINK_SERIAL"`
   argument after J-Link config and before target init.
5. Print whether J-Link auto-detection or explicit override is used.  Never
   print/claim `nrf-probes` target identity for dongle.
6. Keep net-first/app-second programming, paths, verify, reset, and all other
   OpenOCD arguments unchanged.

### Public docs

Update `dongle/README.md` flashing section with default auto-detection and
optional `FW_DONGLE_JLINK_SERIAL` override.  Explicitly state
`scripts/probe-serial.local`/`nrf-probes` select CMSIS-DAP receiver targets and
are not used by dongle J-Link flash.

Do not mark STATUS open follow-up resolved until clean committed script passes
hardware verification.

## Behavior tests

Add production-script behavior tests to existing Python gate child
`tests/unit/gate/test_gate.py` (do not add a 48th canonical child).

Test through public script execution in a temporary repo layout:

- copy actual `fw-flash-dongle` and `fw-common.sh` under temp
  `scripts/bin/`;
- create empty required app/net HEX paths under temp `build/dongle/`;
- provide temp valid `ZEPHYR_BASE` and fake executable `west`/`openocd` on
  PATH;
- fake OpenOCD records exact argv and returns 0;
- fake `nrf-probes` records invocation and fails loudly if called.

Required cases:

1. **Default:** exit 0; nrf-probes never invoked; argv contains
   `interface/jlink.cfg`, target nrf53 config, net-first/app-second program +
   verify, reset/shutdown; no `adapter serial` command.
2. **Explicit override:** exit 0; exact override appears once as one OpenOCD
   `adapter serial` command; nrf-probes never invoked; programming order
   unchanged.
3. **Invalid override:** nonzero exit with stable clear error; OpenOCD and
   nrf-probes not invoked.
4. Existing missing-build-artifact and missing-dev-shell behavior must remain
   unchanged; add assertions only if easy through same harness.

Tests prove observable command construction, not copied shell constants.

## Verification and commits

1. Run:

   ```bash
   python3 tests/unit/gate/test_gate.py
   bash -n scripts/bin/fw-flash-dongle
   git diff --check
   ```

2. Inspect status/diff/log and commit script + test + README + this handoff:

   `fix: select J-Link correctly for dongle flash`

3. On clean implementation commit, run authorized hardware verification:

   ```bash
   fw-flash-dongle
   ```

   Preserve full output.  Require onboard J-Link auto-detected, both cores
   programmed and verified in net-first/app-second order, reset run, exit 0,
   and no unexplained diagnostics.
4. After hardware pass, update STATUS open follow-up to RESOLVED with exact
   implementation commit and observed result.  Commit docs-only evidence:

   `docs: record dongle flash probe fix verification`

No full G1 rerun: narrow shell tool is covered by public behavior tests and
physical flash; production firmware/build artifacts unchanged.

## Non-scope and safety

- No receiver source/test/Kconfig/devicetree changes.
- No dongle firmware source/build changes.
- No mass erase, recovery, UICR change, probe-rs, or receiver flash.
- No canonical child-count change.
- No static probe serial in tracked files.
- No push, amend, merge, PR, force-push, or attribution.

Stop and escalate for multiple J-Links with ambiguous auto-detection,
OpenOCD warning/error, wrong target identity, verification failure, or need to
change firmware/build layout.  Do not weaken tests or silently restore
`nrf-probes` selection.
