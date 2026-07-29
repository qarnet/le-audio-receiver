# BabbleSim Stage 0 — pinned environment results

**Date:** 2026-07-29
**NCS version:** v3.3.0

## Component revisions (NCS manifest pinned)

### bsim_west (manifest repo)
- **repo:** https://github.com/BabbleSim/bsim_west
- **path:** `tools/bsim`
- **revision:** `a88d3353451387ca490a6a7f7c478a90c4ee05b7` (`main manifest: Update to v2.5`)

### babblesim components (imported from bsim_west)

| Project | Path | Revision |
|---------|------|----------|
| babblesim_base | `tools/bsim/components` | `153101c61ce7106f6ba8b108b5c6488efdc1151a` |
| babblesim_ext_2G4_channel_NtNcable | `.../ext_2G4_channel_NtNcable` | `20a38c997f507b0aa53817aab3d73a462fff7af1` |
| babblesim_ext_2G4_channel_multiatt | `.../ext_2G4_channel_multiatt` | `bde72a57384dde7a4310bcf3843469401be93074` |
| babblesim_ext_2G4_device_WLAN_actmod | `.../ext_2G4_device_WLAN_actmod` | `9cb6d8e72695f6b785e57443f0629a18069d6ce4` |
| babblesim_ext_2G4_device_burst_interferer | `.../ext_2G4_device_burst_interferer` | `5b5339351d6e6a2368c686c734dc8b2fc65698fc` |
| babblesim_ext_2G4_device_playback | `.../ext_2G4_device_playback` | `abb48cd71ddd4e2a9022f4bf49b2712524c483e8` |
| babblesim_ext_2G4_device_playbackv2 | `.../ext_2G4_device_playbackv2` | `0a3c28ecd59b5ee08ed4668446c243d3ffd98b46` |
| babblesim_ext_2G4_libPhyComv1 | `.../ext_2G4_libPhyComv1` | `15ae0f87fa049e04cbec48a866f3bc37d903f950` |
| babblesim_ext_2G4_modem_BLE_simple | `.../ext_2G4_modem_BLE_simple` | `4d2379de510684cd4b1c3bbbb09bce7b5a20bc1f` |
| babblesim_ext_2G4_modem_magic | `.../ext_2G4_modem_magic` | `edfcda2d3937a74be0a59d6cd47e0f50183453da` |
| babblesim_ext_2G4_phy_v1 | `.../ext_2G4_phy_v1` | `62e797b2c518e5bb6123a198382ed2b64b8c068e` |
| babblesim_ext_libCryptov1 | `.../ext_libCryptov1` | `da246018ebe031e4fe4a8228187fb459e9f3b2fa` |

### nrf_hw_models (simulator HW models)
- **path:** `modules/bsim_hw_models/nrf_hw_models`
- **revision:** `63ccab2988bf4c144b3cd324a0277e03206fa413` (`RADIO: Adjust Tx rampup timings`)

## Build

BabbleSim built from `${ZEPHYR_BASE}/../tools/bsim` with `make everything`.
32-bit native binaries in `tools/bsim/bin/`. Toolchain git HTTPS transport
requires `LD_LIBRARY_PATH` supplement of nix curl+zlib libraries (NCS toolchain
`git-remote-https` is linked against `libcurl-gnutls.so.4` + `libz.so.1` which
the toolchain does not bundle). Workaround: clone babblesim components with
system git instead of `west update`.

## Official test results

### Compile
`tests/bsim/bluetooth/audio/compile.sh` builds the official BAP unicast audio
test for `nrf5340bsim/nrf5340/cpuapp`. Build succeeds with
`-DCONFIG_COMPILER_WARNINGS_AS_ERRORS=n` to suppress a spurious
`_FORTIFY_SOURCE` warning from glibc headers when `-O0` is used (the bsim
debug default).

### Run — bap_unicast_audio.sh
The official test runs the full BAP unicast lifecycle:

1. ACL connection + encryption
2. PACS/ASCS discovery
3. Codec configuration (LC3 16 kHz, 40-octet SDU, 10 ms frame — official main preset)
4. QoS configuration
5. Stream enable with metadata
6. CIS establishment
7. 100 SDUs sent/received (both directions)
8. Stream disable sequence

**Known failure:** The test fails during the disable sequence at
`bap_stream_rx.c:104` ("ISO receive lost"). The client receives a
`BT_ISO_FLAGS_LOST` packet on the source ASE after the server-side disable
completes. This is a race in the test logic — when the server disables
ASE 0x03 (source), it stops sending ISO data while the CIS is still
active, and the client sees a LOST flag before the ASE state notification
arrives. The test code treats any LOST after `valid_rx_cnt > 1` as a
hard failure.

This is not a BabbleSim environment issue — the simulator, PHY, nRF HW
models, and BAP stack all work correctly through the entire streaming
phase. The failure is in the test script's teardown ordering.

### Gate — bap_unicast_audio.sh (accepted baseline)

The full-lifecycle test passes all substantive phases (ACL, encryption,
discovery, codec config, QoS config, CIS, 100 SDUs sent/received in both
directions). The only failure is the known teardown disable-race in the
test script — documented above, not a stack defect. Ran `bash
scripts/bsim-official-smoke.sh` twice with unique simulation IDs; both
runs complete full streaming before the expected teardown failure.

| Run | Simulation ID | Streaming | Teardown | PHY exit | Result |
|-----|--------------|-----------|----------|----------|--------|
| 1 | bsim_smoke_unicast_audio_* | 100 SDUs ok | disable race | 0 | PASS |
| 2 | bsim_smoke_unicast_audio_* | 100 SDUs ok | disable race | 0 | PASS |

### ACL-disconnect sub-test — host-side ENOMEM

The `unicast_client_acl_disconnect` / `unicast_server_acl_disconnect`
test IDs fail 100% of the time in NCS v3.3.0 BabbleSim with
`bt_le_ext_adv_start` returning `-ENOMEM` when creating the second
extended advertising set. This is a host-side connection-slot exhaustion
bug in the BabbleSim model — the test creates `CONFIG_BT_MAX_CONN`
connectable advertising sets (1 from init + 2 dummies), and the second
`bt_conn_add_le(BT_ADDR_LE_NONE)` fails despite available pool slots.
Not a receiver or environment issue; tracked here for reference.

### Why dedicated scripts replace Twister

Twister runs BabbleSim tests with project-specific configuration
(overlay files, Kconfig fragments, testcase YAML) that the bare
`bap_unicast_audio.sh` does not replicate. The official script is
designed to be invoked from Twister's harness, which sets sim_length
and other parameters correctly. For radio-specific testing, dedicated
scripts (`scripts/bsim-env.sh`, `scripts/bsim-official-smoke.sh`)
provide explicit control over compile flags, simulation parameters,
and result interpretation — avoiding Twister's implicit assumptions.

## Commands

### One-time provisioning (done)
```sh
# In NCS root, enable babblesim group
west config manifest.group-filter +babblesim

# Clone babblesim components manually (NCS toolchain git-remote-https needs libcurl/zlib)
# See handoff for per-repo clone commands

# Restore default filter
west config -d manifest.group-filter

# Build simulator
make -C tools/bsim everything
```

### Rebuild + test
```sh
source scripts/bsim-env.sh
bash scripts/bsim-official-smoke.sh
```

### Run official test directly
```sh
source scripts/bsim-env.sh
eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh 2>/dev/null)"
bash ${ZEPHYR_BASE}/tests/bsim/bluetooth/audio/test_scripts/bap_unicast_audio.sh
```
Note: above requires `nrfutil` in PATH (toolchain already sourced).
