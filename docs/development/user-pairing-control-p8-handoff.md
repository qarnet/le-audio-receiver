# P8 handoff — hardware acceptance and closeout

Base commit: `e1dbbb0` (P7 accepted plus documentation arithmetic correction;
worktree clean). User has authorized repeated receiver/dongle flashing and
normal lab operations. No mass erase, recovery, settings-partition erase,
probe-rs, destructive command, or static probe mapping.

Execute P8 from `docs/development/user-pairing-control-plan.md`. Preserve raw
logs under `/tmp` during execution; commit only final repo evidence/docs after
all acceptance rows pass.

## Safety and identity

- Run `nrf-probes` before each target flash. Record raw probe serial, DPIDR, AP
  map, and FICR PART/VARIANT evidence returned by tool; never document a static
  probe↔board table.
- Use only `fw-flash-54l15`, `fw-flash-5340`, and existing OpenOCD reset paths.
- Start resilient serial capture before any reset/flash. XIAO console
  `/dev/ttyACM0` 115200; E83 console `/dev/ttyUSB0` 115200.
- List serial ports before opening. Close serial connections when done.
- Do not normalize any compiler, Kconfig, flashing, boot `LOG_WRN`, controller,
  audio, or shell warning.

## Automated nRF54L15 preparation

1. Verify clean exact commit and fresh P7 build/contract evidence.
2. Start `/dev/ttyACM0` resilient capture, then flash nRF54L15 app + FLPR.
3. Capture full boot. Require BLE ready, settings load, pairing full-stack start,
   NORMAL access, LED inactive command, advertising start, no fatal/reboot loop,
   no GPIO/input failure, no stack/heap/assert/fault warning.
4. Query available shell diagnostics including thread/stack data when enabled.
   Record pairing workqueue stack high-water evidence if runtime API exposes it;
   otherwise record limitation and use sustained transition tests as runtime
   evidence. Heap 0 must show no allocation failure.
5. Run `bt unpair`; require synchronous sequence: suspend, disconnect if needed,
   delete bonds, RESETTING for one second, BONDING advertising active, exact
   shell success. Then reset device to establish NORMAL + zero bonds.

## Central setup

Attach nRF5340DK HCI UART central exactly per repo instructions. Verify:

- `hci0` powered, LE, secure-conn, cis-central;
- dongle identity `C0:AA:BB:CC:DD:EE`;
- `scripts/bap_central.py` runs without sudo (raw helper may use sudo).

Capture central stdout/stderr separately per row.

## Automated XIAO rows

Where button/LED physical action is required, automate capture and tell
Orchestrator exact ready window; user supplies only physical press/visual/audible
observation. Do not ask user to operate central or run commands.

Required rows:

1. NORMAL zero-bond boot: advertising visible; direct unbonded connection/pair
   rejected. Record receiver + central evidence.
2. Short physical press (<3 s): no transition/connect-policy change. User later
   confirms LED remained inactive.
3. Hold through 3 s then release before 8 s: receiver suspends advertising,
   disconnects active peer first when applicable, enters BONDING OPEN, starts
   slow LED. Verify existing persisted bonds remain.
4. Fresh Just Works pair in BONDING: receiver requests L2; bonded completion
   enters NORMAL without disconnect; LED off. Stream Mode A for at least 30 s;
   require clean decode/audio/offload counters and no underrun/reset/fault.
5. Re-enter BONDING with existing bond, reconnect preserved-bond central:
   successful security change + bond existence completes NORMAL without
   disconnect. Stream Mode B at least 30 s cleanly.
6. Continuous physical hold through 8 s: BONDING fires at 3 s, RESET supersedes
   at 8 s, any intervening peer disconnects before deletion, advertising remains
   stopped through one-second feedback, then BONDING OPEN. Old bond fails;
   fresh pairing succeeds.
7. `bt unpair` repeats same RESET owner/ordering and exact synchronous output.
8. Reboot after saved fresh bond: NORMAL/BONDED_ONLY accepts saved bond; distinct
   unbonded identity rejected.

User-only observations requested after instrumented runs:

- short press LED stayed off;
- BONDING LED approximately 500 ms on / 500 ms off;
- RESET LED approximately 100 ms on / 100 ms off for one second (five flashes),
  then slow BONDING pattern;
- audio audible for Mode A and Mode B, if speakers/headphones connected.

## nRF5340 parity

1. Start `/dev/ttyUSB0` capture, flash app + net core with `fw-flash-5340`.
2. Require feature-off boot/advertising and no new warnings.
3. Run representative Mode A stream at least 30 s using autonomous central;
   require clean decode/audio counters and existing behavior.
4. `bt unpair` must retain legacy feature-off output/path.
5. Record BSim/software evidence by commit reference; no need to rerun canonical
   gate unless repo files change before closeout.

## Failure handling

- Preserve exact logs and stop on fatal reboot, assert, allocation failure,
  stack warning, GPIO error, pairing policy mismatch, unexpected disconnect,
  audio warning, or failed acceptance row.
- Do not erase bonds/settings except through production `bt unpair` RESET.
- Do not weaken timing/identity tests or repeat until a flaky pass without root
  cause.
- If physical input/observation is next required step, return exact current
  device state, capture status, requested press duration/action, and observation
  choices to Orchestrator. This is expected, not failure.

## Closeout

After all automated and user-observation rows pass:

- write `docs/development/user-pairing-control-p8-results.md` with raw-log paths,
  timestamps, commands, identities, transitions, central outcomes, counters,
  stack/heap evidence, and user observations;
- update `STATUS.md`, `README.md`, `AGENTS.md`, design/behavior-contract and plan
  status only where current behavior changed;
- run documentation hygiene on feature-touched active docs;
- run final canonical gate/build contract only if closeout edits affect
  executable inputs (docs-only closeout needs `git diff --check` and targeted
  doc consistency checks);
- commit P8 handoff first, then hardware results/closeout separately.

No push, PR, amend, force-push, attribution footer, or destructive recovery.
