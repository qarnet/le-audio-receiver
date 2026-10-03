---
id: PB-041
title: Identify lab DUT and verify logic-analyzer wiring per session
status: Backlog
assignee: []
created_date: '2026-09-25 20:45'
labels:
  - 'size:L'
  - 'area:hil'
  - 'area:hardware'
  - 'area:testing'
dependencies: []
references:
  - docs/testing/logic-analyzer-setup.md
  - docs/development/audio-validation-handoff-20260925.md
priority: p2
type: feature
ordinal: 38000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
### Problem
Two XIAO nRF54L15 boards can exchange physical and firmware roles between sessions. Probe family, tty ordering and old serial tables do not prove which board drives the breadboard DAC harness or which analyzer channel sees each intended pad. The user wants an onboard UART-commanded signature to verify this automatically, plus a command reporting whether an I2S device is attached. This fixture-identification work must remain separate from PCM/content and timing qualification.

### Desired outcome
A fresh, bounded discovery session binds probe, silicon, USB/UART, live firmware/boot identity and captured pin/channel evidence. The host identifies the board driving the user-declared DAC harness as the DUT candidate, detects channel swaps or ambiguity, and records an immutable mapping. An explicit presence query reports supported evidence for DAC attachment/function separately from wire identity and power observation. Unobservable facts are reported as unknown, never guessed. Fully automatic physical DAC-presence confirmation requires an agreed return-path/sense mechanism; unchanged three-wire I2S cannot acknowledge a DAC.

### Scope / Non-goals
Scope: reuse nix-nrf probes and existing HIL discovery/session ownership; add bounded firmware diagnostic protocol and host coordinator/decoder; verify a fresh nonce on actual captured waveform; validate intended output pads against observed analyzer channels; report firmware-role mismatches and unsupported images; retain raw capture/identity evidence. Prefer valid I2S marker emitted through exclusive output owner. Individual GPIO signatures are opt-in fallback only after safe peripheral release and verified analog mute/disconnection. Presence command must expose independent evidence states, including unknown for current no-feedback wiring.

Central physical declaration and safety contract: docs/testing/logic-analyzer-setup.md. Do not duplicate its channel table in task metadata. CH3 is passive rail observation, not a drive pin or guaranteed power-good signal. Confirm ground and analyzer voltage limits before active work.

Non-goals: PCM fidelity, LC3 conformance, ASRC control-loop acceptance, analog quality qualification, BLE clock synchronization, PB-019 HCI corruption repair, arbitrary GPIO scanning, automatic flashing to force role matches, or editing existing frozen HIL limits. No direct SDC/MPSL RADIO access. Do not send shell text to H4 firmware.

### Technical context
Active SDK NCS v3.4.1. Existing discovery/session seams: scripts/hil/discovery.py and scripts/hil/session.py; strict schema-v1 devices.json rejects added fields. Prefer separate versioned immutable harness evidence bound to session hash, or explicitly version the schema. tests/hil/fixture-xiao-source.json capture_capability concerns analog ALSA capture, not analyzer support.

Firmware seams: src/audio_shell.c routes audio stop through lifecycle; src/audio_i2s.c owns sink/pinctrl interaction; hil/source/app/src/hil_source_app.c and hil/source/core/hil_source_protocol.h own standalone-source protocol; scripts/hil/source_client.py owns source requests. Neither image currently supplies nonce-based pin identification. Live capability must be established before issuing proposed commands.

Helper source: ~/repos/nix-nrf-dev/bin/commands/nix-nrf-probes. Current public command is nix-nrf probes; nrf-probes remains JSON backend vocabulary. Family matching is ambiguous with two XIAOs. Full raw DP/AP/FICR plus USB ancestry is already available through project resolver. Firmware role declarations alone do not prove physical wiring.

Preferred design: host arms bounded capture, sends fresh per-attempt nonce to one verified candidate, firmware emits framed low-amplitude post-ASRC diagnostic words in valid I2S, host checks nonce/integrity and tries allowed channel assignments. Only unique observed mapping binds DUT candidate. Retain expected versus observed map and reject stale, truncated, duplicated or flat traces. Pin-specific GPIO fallback needs explicit allowlist, fresh nonce/pad identifier, exclusive ownership and automatic cleanup. Firmware timeout must work even if host disappears.

Three-wire BCLK/WSEL/DIN has no DAC identity/ACK. MCU output readback, input loading and rail-high cannot distinguish functional DAC from open connector or powered harness. A new sense/ID connection or safe analog return is a design decision, not an implemented capability. Software PCM mute does not protect arbitrary pin patterns; no current MCU-driven analog DAC mute was found.

Related, not automatic dependencies: PB-033 session foundation; PB-035/PB-036 HIL integration; PB-023/PB-025 analog qualification. Separate audio testing handoff: docs/development/audio-validation-handoff-20260925.md.

### Open questions
Which exact DAC breakout and analyzer model/input limits are connected, and is analyzer ground confirmed? Which analog mute or downstream-disconnection procedure is safe? Does the user require an added hardware presence sensor/ID or analog return for automatic DAC attachment proof, or accept explicit unknown plus physical attestation with unchanged wiring? Which diagnostics must be available in each firmware role, especially HCI mode? Fix command schema, waveform amplitude/duration, acquisition limits and evidence-schema version during refinement. Keep Backlog until these consequential safety/product choices are resolved.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Fresh discovery binds distinct candidates using raw probe/silicon and USB/UART evidence plus live firmware/boot identity; role assignment never relies on cached tty, HCI index, enumeration order or SoC family alone.
- [ ] #2 A bounded UART-commanded diagnostic emits a fresh request nonce on allowlisted I2S outputs; host accepts pin/channel identity only from captured waveform with matching nonce and unique valid mapping, not UART acknowledgment alone.
- [ ] #3 Wire-level verification detects swapped channels and fails closed on missing, flat, duplicated, ambiguous, stale/replayed or truncated captures; failure to observe a candidate never automatically assigns it source role.
- [ ] #4 Diagnostic refuses active/conflicting use safely, has local timeout and cancellation, restores peripheral/pinctrl ownership after errors or host loss, and never drives supply/CH3, UART/SWD/RF-control or unapproved pins. Electrical and analog safety preconditions are checked.
- [ ] #5 Presence query and host report distinguish harness binding, rail observation, DAC presence and DAC function with named evidence and explicit unknown states. Absent DAC with unchanged driven I2S wires cannot produce a false confirmed-presence result.
- [ ] #6 Physical presence-detection mechanism and scope are explicitly agreed before claiming automatic DAC detection; any required added sense/ID or analog return is documented and tested against present, absent and misleading powered/open-harness cases. Unsupported unchanged wiring remains unknown.
- [ ] #7 Each session retains versioned immutable harness evidence bound to device-session and image/boot identity, analyzer selector/settings, expected and observed mapping, nonce, raw capture hash, command outcomes and capture-integrity verdict; existing strict session schema and analog capture capability are not silently repurposed.
- [ ] #8 Firmware-role mismatch or unsupported diagnostic capability fails with an actionable reason, without automatic flashing or shell traffic on HCI H4. Both supported diagnostic-capable board roles are covered by public protocol tests.
- [ ] #9 Regression tests exercise encoded command/capture boundaries, stale nonce, channel permutation, invalid input, busy, timeout, cancellation and restart. Physical acceptance challenges both freshly discovered boards and confirms actual wire mapping; simulator-only or mock-call evidence cannot substitute.
<!-- AC:END -->
