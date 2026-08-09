# Flashing the nRF54L15 Xiao release candidate

## Target and images

This package is for the **nRF54L15 receiver** (Seeed Xiao nRF54L15 with
the SAMD11 USB CDC bridge). It contains two image files that must be
flashed together as one versioned tuple:

| File | Role | Flash order |
|------|------|-------------|
| `cpuapp.hex` | Application core (cpuapp) | 1 |
| `flpr.hex` | FLPR (RISC-V VPR audio offload core) | 2 |

Do not mix these files with images from a different version or target.
The pair is one release tuple; flashing one half without the other leaves
the device in an unsupported state.

## Flash order

1. `cpuapp.hex` (cpuapp)
2. `flpr.hex` (FLPR)

Normal flashing through the repository helper (`fw-flash-54l15`) programs
firmware address ranges and preserves settings and bonds. A destructive
clean-state or recovery procedure is a separate, target-specific step; it
is never implied by this package.

## Reference documentation

- Repository flashing guide:
  https://github.com/qarnet/le-audio-receiver/blob/main/docs/flashing.md
- User guide:
  https://github.com/qarnet/le-audio-receiver/blob/main/docs/user-guide.md

## Status

FR1 packages are release-candidate inputs. Final non-developer flashing
commands must be accepted and documented before public publication.
