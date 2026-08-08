# Planned features

## Distinguish NORMAL and BONDING advertisements

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
   filter accept list is allow-only and cannot express “allow every unknown
   peer except known bonds.”
6. Avoid application-level connect/disconnect loops that consume the connection
   slot and radio time.
7. Define compatibility when a central does not understand the marker.

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
