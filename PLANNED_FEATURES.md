# Planned features

Structured, prioritized backlog of known gaps and future work. Items are
ordered roughly by priority within the backlog; the advertisement-differentiation
research is kept as its own section because it predates the backlog and is
referenced by `docs/development/user-pairing-control-plan.md`.

---

## Backlog

### A. Multi-frequency LC3 support

**Priority:** High
**Status:** Backlog

#### Problem

The firmware currently hardcodes **48 kHz** everywhere: PACS capability and
codec validation (`src/bt_bap.c`), the LC3 decoder setup
(`src/audio_decode.c`), and the I2S/sink path (`src/audio_i2s.c`). A source
that negotiates any other sample rate is rejected.

#### Scope

Candidate practical LC3 rates at or below 48 kHz: **8, 16, 24, 32, 44.1, and
48 kHz**. Note that **44.1 kHz combined with 7.5 ms frame duration produces a
non-integer number of samples per frame** (330.75) and needs an explicit
compatibility decision before it can be supported.

#### Plan

- PACS capability advertisement for the supported rates.
- Codec-config validation and rejection messages per rate.
- Decoder configuration (rate, frame duration, octets per frame) per stream.
- Sink / rate-conversion path for each rate.
- FLPR offload contract and CPU fallback behavior for each rate.
- Test fixtures (LC3 golden PCM at each rate).
- BSim scenarios and hardware acceptance.

Do not promise all rates blindly: land rates one at a time with fixtures and
acceptance evidence.

#### Acceptance

- A source can negotiate and stream at each supported rate.
- Unsupported rates are rejected with the existing codec-config error
  discipline.
- Fixtures, BSim, and hardware acceptance exist per rate.

---

### B. Perceptual / logarithmic volume mapping

**Priority:** Medium
**Status:** Backlog

#### Problem

Current volume implementation (`src/audio_volume.c`) multiplies each PCM
sample by `volume / 255`, a **linear** scale. Reported behavior: the first
roughly **third of the slider** causes most of the perceived loudness change,
and adjustments near the top of the range are barely audible. Keep the
linear-scaling explanation as the mechanism; do not assert an exact
psychoacoustic cause (e.g. a specific loudness curve) until the VCP mapping
investigation below has been done.

#### Plan

- Investigate Volume Control Profile (VCP) semantics before choosing a curve
  (how the spec expects volume values to map to attenuation/gain).
- Define a **monotonic fixed-point gain table/curve** (no floating point on
  the audio path).
- Pin exact endpoints: **mute** and **full-scale** behavior, and the value at
  which full scale is reached.
- Define saturation behavior (no overflow on scaled samples).
- Unit tests for the table (monotonicity, endpoints, saturation).
- Listening acceptance: perceive a natural volume sweep across the range.

#### Acceptance

- Volume changes are perceptually even across the range on both DAC types and
  both boards.
- Mute is exact, full scale is exact, and no sample overflow occurs at any
  volume.
- Curve choice is justified against VCP semantics in the acceptance record.

---

### C. Long-idle resume pop

**Priority:** Medium
**Status:** Backlog

#### Problem

A startup/audible pop has been observed after long idle periods. The root
cause is **not yet confirmed**. An energy-saving cause (e.g. clock or DAC
power-down during idle) is a **hypothesis only**: do not treat it as fact
until instrumented.

#### Plan

- Instrument timing around stream start/stop and idle intervals.
- Inspect I2S/DAC stop and start sequencing, startup prefill, and
  silence/ramp/mute sequencing (including the MUTE control on the DAC).
- Determine whether the pop is digital (PCM discontinuity), electrical
  (DAC/power state change), or both.

#### Acceptance

- No audible transient after defined idle intervals (list the intervals in
  the acceptance record).
- Verified on both DAC types and both boards.

---

### D. Duplicate BONDING advertisements

**Priority:** High (observed bug)
**Status:** Under investigation

#### Problem

During BONDING, **two scanner entries / advertisements have been observed**
and only one of them pairs. Root cause is **unconfirmed**. This is a distinct,
observed bug and must not be conflated with the intentional NORMAL/BONDING
advertisement payload distinction (see the dedicated research section below).

#### Plan

- Capture scanner evidence: advertisement timing, identity/address fields,
  and advertising-set attribution for both entries.
- Determine whether the duplicate is two advertising sets, an address/identity
  artifact, or a central-side discovery artifact.
- Establish the invariant: **exactly one connectable receiver advertisement**
  is visible at any time.
- Verify pairing succeeds from the visible entry and that the stale/duplicate
  entry cannot steal the connection.

#### Acceptance

- One connectable advertisement invariant holds in NORMAL and BONDING.
- Pairing succeeds from the visible entry on a fresh central.
- Evidence (scanner captures, identity/address/adv-set analysis) recorded in
  the resolution document.

After this bug is understood and fixed, refine the advertisement-marker
research below as a follow-up.

---

### E. nRF5340 user pairing input

**Priority:** Medium
**Status:** Backlog

#### Problem

The nRF54L15 build has user pairing control (button + LED, see
`docs/development/user-pairing-control-plan.md`); the **nRF5340 build does
not**: no physical pairing button is wired yet (see
[Known limitations](docs/known-limitations.md)). Pairing reset on nRF5340 is
currently only possible through the developer shell (`bt unpair`).

#### Plan

- Board wiring + devicetree alias + Kconfig for the nRF5340, matching the
  nRF54L15 feature stack (`CONFIG_USER_PAIRING_CONTROL` /
  `CONFIG_USER_PAIRING_INPUT`).
- Same user experience as nRF54L15: 3 s hold = BONDING, 8 s hold = RESET.
- **Candidate pin defined only after hardware review**: do not invent a pin
  assignment now.
- Feature-parity tests and hardware acceptance on the nRF5340.

#### Acceptance

- nRF5340 button/LED behavior matches the nRF54L15 contract (NORMAL /
  BONDING / RESET).
- Feature-parity tests and hardware acceptance recorded.

---

### F. Public GitHub release binaries

**Priority:** High (blocking public use)
**Status:** Planned; nothing published yet

#### Problem

Today the firmware must be built from source with the developer toolchain.
Public users need ready-made firmware binaries.

#### Plan

- Factory image releases are first, per
  [the firmware release plan](docs/development/firmware-release-plan.md).
  Release artifacts are factory-flash ZIPs, one per receiver target:
  nRF5340 dual-core merged hexes, and nRF54L15 cpuapp plus FLPR images.
- Checksums for every artifact.
- Versioning scheme and release notes.
- Flashing documentation for each published artifact.
- MCUboot and signed DFU are a separate future track (item I) and do not
  block factory image releases.

#### Acceptance

- A public user can download, verify, and flash a release without building
  from source, following the user guide.

---

### G. (Research) nRF54L15 as a native Linux LE Audio USB dongle

**Priority:** Low (research only)
**Status:** Research; feasibility **not** claimed

#### Problem / idea

The nRF54L15 (Seeed Xiao) is cheap and has an onboard USB bridge. Could it
serve as a native Linux LE Audio source dongle (plug-and-play USB LE Audio
source)? This is an **open research question**: enumerate the architecture
before claiming anything.

#### Plan

- USB transport / profile architecture (USB Audio Class vs vendor HCI-like
  transport; what the SAMD11 bridge can carry).
- Linux integration: BlueZ and PipeWire support for LE Audio over USB.
- Upstream compatibility of the chosen USB descriptor/transport.
- Source-role feasibility (the firmware is currently sink-only).
- Plug-and-play acceptance criteria (device appears in PipeWire, streams
  without manual setup).

#### Acceptance

- Architecture and feasibility analysis documented with evidence.
- If feasible: prototype acceptance per the criteria above. If not: record
  the blockers.

---

### H. Public-friendly nRF54L15 flashing method

**Priority:** High (blocking nRF54L15 public use)
**Status:** Planned

#### Problem

The nRF54L15 currently flashes through a developer workflow. A public-friendly
method (easy tooling, no specialized developer toolchain) needs evaluation.

#### Plan

- Evaluate candidate methods: Nordic's official tools where applicable,
  plain OpenOCD builds, and any platform-neutral flasher that avoids the
  specialized developer toolchain.
- Document hardware requirements (onboard SAMD11 CMSIS-DAP) and exact steps
  for a non-developer.
- Keep the nRF54L15 flashing section of the user guide intentionally vague
  until this is decided.

#### Acceptance

- A non-developer can flash the nRF54L15 build following documented steps.

---

### I. MCUboot and signed firmware update

**Priority:** Medium
**Status:** Research

#### Problem

Factory image releases (item F) cover fresh flashing only. Over-the-air or
in-field firmware update needs MCUboot and signed images, which is a separate
track from factory releases.

#### Plan

- Require an ADR before implementation, resolving companion-image
  compatibility, signing-key custody, rollback, power-loss behavior, settings
  preservation, downgrade policy, and transport.
- BLE SMP in a physically gated DFU mode is the preferred transport
  hypothesis, not an accepted implementation.
- Build target-specific prototypes before any acceptance claim.

#### Acceptance

- Signature rejection, rollback, interrupted transfer, power-loss recovery,
  companion-image compatibility, settings/bond preservation, and post-update
  audio all pass acceptance.

#### NCS constraints

- nRF54L15 stock MCUboot updates cpuapp only, not the custom FLPR image.
- nRF5340 full app/net update is supported by NCS but the internal-flash
  budget for this firmware is currently unresolved.
- No DFU support claim exists yet.

---

## Advertisement differentiation: NORMAL vs BONDING (existing research)

Status: deferred follow-up to
`docs/development/user-pairing-control-plan.md`.

### Problem

Initial user pairing control uses the same Bluetooth advertising payload in
NORMAL and BONDING. Access policy differs at the controller:

- NORMAL uses BONDED_ONLY connection filtering.
- BONDING uses OPEN advertising so an unknown central may connect and pair.

A previously bonded central may auto-connect when it sees the BONDING
advertisement. Because the receiver has one ACL/BAP connection slot, that can
prevent a new central from pairing. Interim accepted behavior counts a secure
reconnection from an already bonded central as successful BONDING completion
and returns to NORMAL without disconnecting it.

### Desired future behavior

- Centrals can distinguish NORMAL from BONDING on-air.
- A custom central auto-connects only to NORMAL.
- A custom central requires explicit user action before connecting to BONDING.
- Stock BlueZ/WirePlumber behavior remains understood and documented.
- Existing bonds and privacy remain correct.
- No reconnect-reject loop repeatedly occupies the sole connection slot.

### Research items

1. Add an explicit mode marker through manufacturer data or service data while
   preserving the mandatory ASCS unicast announcement.
2. Update `bap_central_device.py` discovery policy to parse the marker and
   suppress automatic bonded reconnect to BONDING.
3. Determine whether stock BlueZ/WirePlumber exposes a supported policy hook
   that can suppress profile auto-connect based on advertisement data.
4. Evaluate separate advertising sets or Bluetooth identities. Document bond,
   privacy, GATT, PACS/ASCS, and identity-resolution consequences before use.
5. Evaluate rejection of known bonded peers during BONDING. The controller
   filter accept list is allow-only and cannot express "allow every unknown
   peer except known bonds."
6. Avoid application-level connect/disconnect loops that consume the connection
   slot and radio time.
7. Define compatibility when a central does not understand the marker.

### Reconciliation with the duplicate-advertisement bug

- The observed duplicate BONDING advertisements (backlog item D) are a bug,
  not this feature. Fix item D first and establish the one-connectable-
  advertisement invariant **before** implementing payload differentiation.
- Do **not** adopt **two simultaneous advertisements** as the default fix for
  either issue: running a second advertisement (set or identity) has bond,
  privacy, and connection-slot consequences documented in research item 4, and
  the duplicate-entry symptom itself needs a root cause first.
- After item D is resolved, revisit the marker approach above.

### Acceptance questions

- Which payload field is standards-compatible and available in extended
  advertising reports on all supported centrals?
- Can BlueZ suppress reconnect before ACL creation, rather than disconnecting
  afterward?
- Can one device identity safely expose both advertisements while retaining
  existing bonds?
- If separate identities are required, how are bond ownership and settings
  migration handled?
- What fallback applies to an old central that does not understand the marker?

Do not implement this item as part of initial button/LED pairing control.
