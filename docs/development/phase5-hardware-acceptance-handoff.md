# Phase 5 — hardware acceptance handoff

## Goal

Prove cpuapp ASRC on nRF54L15 under Mode A and true Mode B 10-minute streams,
then run available nRF5340 regression. Record exact evidence and fix defects.

## Preconditions

- Software commits through `4ee37ee`.
- Use `nrf-probes`; never static probe mapping.
- Attach nRF5340DK HCI UART with `btattach`; run `bap_central.py` without sudo.
- Start serial capture before reset/flash-induced reboot.

## Required tests

1. Build/flash nRF54 production Mode A. Run 600 s autonomous central stream.
   Capture complete receiver and central logs. After stream, query `audio status`
   and `audio perf` before next gate opens.
2. Create smallest test-only Mode B build configuration: one sink ASE with
   stereo channel-count/allocation capability. Preserve product Mode A config.
   Verify central `--stereo` negotiates one ASE with two channels from logs; do
   not call two-mono-ASE Mode A “Mode B.” Run 600 s and collect same evidence.
3. For both: zero warning/error/fault/assertion, disconnect during active run,
   slab-full, I2S underrun/restart, repeat fallback, push failure, ASRC capacity
   error, decode error beyond expected invalid/PLC semantics. Callback max below
   10 ms, queue bounded, PCLK measurement active, output frames bounded.
4. Record ASRC avg/max cycles/us separately and complete callback timing.
5. Build nRF5340. If E83 receiver connected, flash and run at least 60 s Mode A
   regression with APLL/identity path. If absent, preserve raw `nrf-probes`
   evidence and defer only hardware run.
6. No sigrok unless needed to diagnose digital failure; redirect all binary
   output to files and print compact metrics only.

## Completion

- Fix any firmware/central/config defect found, rerun affected full gate.
- Remove SAMPLE_ADJUST production option only after both nRF54 gates pass;
  retaining a test-only historical comparison fixture is acceptable if useful.
- Update design/status/AGENTS/README and add Phase 5 result document.
- Commit test config, fixes, results, and this handoff. Do not push/release.
