# 0001: Private BlueZ guest isolation for host-stack regression

- Date: 2026-10-08
- Status: Proposed
- Approval: Pending human product-owner review on PR 16

## Context

PB-053 established a Linux/BlueZ host-stack regression lane that must exercise
real encoded public D-Bus and ISO transport behavior against a real Bluetooth
daemon. The workstation boundary forbids mutating host adapters, drivers,
kernel or the host system bus, and `/dev/vhci` is a root-only host resource.
A pure results accountant (PB-052) cannot by itself prove encoded transport
behavior; it validates reports, not the stack producing them. A real encoded
regression therefore needs an environment where a BlueZ daemon, emulated
controllers and ISO transport run without touching the host.

## Options considered

- Run BlueZ and emulated controllers directly on the host workstation. This
  would require host adapter, driver, kernel or bus mutation and access to the
  root-owned `/dev/vhci`, which conflicts with the owner's host-isolation
  boundary. Rejected.
- Keep the PB-052 accountant alone. It cannot prove real encoded D-Bus or ISO
  behavior. Rejected as insufficient.

## Decision and rationale

The lane runs inside a private QEMU guest with a deliberately provisioned,
machine-specific pinned profile, and keeps all ownership boundaries on the
host side:

- The guest boots a pinned existing kernel image with matching modules and
  config, an installed BlueZ daemon and independently built `btvirt` from a
  pinned clean BlueZ source revision. No network, host shares, device
  passthrough or privileged RPC exists; controllers, D-Bus, daemon and
  `/var/lib/bluetooth` state are guest-only.
- Host children are constrained by owned process groups and per-process
  subreaper adoption for detached descendants. A descendant scope requires
  the sole main thread and no existing direct children; it sets (and later
  restores) the child-subreaper flag, so an operator-preexisting flag value
  such as 1 is permitted, saved and restored unchanged.
- The caller supplies a literal manifest digest that anchors the manifest and
  the exact copied source bytes for trusted local execution; check and lane
  steps fail on any pin mismatch.
- These tradeoffs are current facts explained at lane cleanup time. They are
  not a record of an older undocumented debate.

## Consequences

The profile is machine-specific by design: another machine must deliberately
reprovision and review equivalent pinned capabilities; there is no automatic
installer, download or host mutation. Guest root does not confer host root.
The host CPU profile with `ssbd=off` remains a reviewed property of this local
guest configuration, not a general hardening recommendation. The lane does not
attest against a hostile same-user process, and it does not establish RF
delivery, codec conformance, analog presentation or release acceptance. Late
acquire, progress reporting and cancellation paths have bounded-ownership
needs that still require real tests as the lane grows.

## References

- Guide and boundary: [Isolated BlueZ host lane](../testing/isolated-bluez-host-lane.md)
- Completed item: [PB-053 in backlog completed](../product/backlog/completed/pb-053%20-%20Establish-isolated-Linux-BlueZ-host-regression-lane.md)
- Lane results: `docs/development/pb-053-host-lane-results-20261005.md` (nine
  mandatory cases 9/9; separate R32 outer-cancellation control)
- Test directories: `tests/host_bluez/`, `tests/unit/bluez_host_process/`,
  `tests/unit/bluez_host_descendants/`, `tests/unit/bluez_guest_acquire/`