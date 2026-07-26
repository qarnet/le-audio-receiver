# Phase 4a Central-Reflash Handoff — nRF5340 DK as CIS Central for nRF54L15 Receiver

Status: continuation after commit `b68faae` (Phase 4a blocked: hci0 CIS-defective,
hci1 BD all-zeros). Resolves the hci1 blocker by reflashing the nRF5340 DK as a
USB HCI central with CIS + non-zero BD, then runs the Phase 4a stream test
**DK-central → nRF54L15 Xiao receiver**.

## Goal

1. Build + flash an nRF5340 DK USB HCI central image (cpuapp = USB-HCI transport,
   cpunet = SW-split LL controller with CIS-central, `bt-ll-sw-split` snippet).
   After reflash hci1 must report a **non-zero** static random BD_ADDR (sourced
   from FICR DEVICEADDR via `VS_READ_STATIC_ADDRS`).
2. With the nRF54L15 Xiao receiver already running current firmware, run the
   Phase 4a stream test DK-central → Xiao-receiver. Capture serial + btmon +
   sigrok evidence. Update `docs/development/phase4a-results.md`.
3. Commit. Do not push.

## Hardware layout (verified this session, see SESSION_USB_TABLE.md)

- **Receiver = nRF54L15 (Seeed Xiao)**. Debug probe CMSIS-DAP serial `8EE9B3FF`,
  `/dev/ttyACM0` @ 115200 8N1. Already connected, builds/flashes/boots clean.
  Do NOT reflash unless source changed — current image is the Phase 4a target.
- **Central target = nRF5340 DK**. Same physical board carries:
  - SEGGER J-Link OB-nRF5340 (`1366:1061`, serial `001050023938`) — debug probe
    for reflashing the DK itself. Verified working: `openocd -f interface/jlink.cfg`
    examines both cpuapp + cpunet (`J-Link OB-nRF5340-NordicSemi`, VTarget 3.3V,
    DPIDR `0x6ba02477`).
  - nRF USB BLE HCI (`2fe3:000b`) — currently `hci1` with BD `00:00:00:00:00:00`.
    This is what gets replaced by the reflash.
- **hci0 = nRF5340DK hci_uart** (`/dev/ttyACM2`, 1 000 000 baud, H4).
  CIS-proven (3000 ISO TX packets/15 s, STATUS.md).
- **E83 module is NOT in this session.** Do not look for or touch the Ebyte E83.
  The receiver is the Xiao only.

## Recipe (grounded in @ncs-source lookup against ~/ncs/v3.3.0/)

No prebuilt hex exists. Build from source.

### Source locations (verified)

- cpuapp USB-HCI transport sample: `~/ncs/v3.3.0/zephyr/samples/bluetooth/hci_usb/`
  (newer USBD stack; prj.conf has `CONFIG_USBD_BT_HCI=y`,
  `CONFIG_USB_DEVICE_STACK_NEXT=y`). No sysbuild dir — needs a wrapper.
- cpunet controller CIS-central conf: `~/ncs/v3.3.0/zephyr/samples/bluetooth/hci_ipc/nrf5340_cpunet_iso_central-bt_ll_sw_split.conf`
  (central-only, `CONFIG_BT_CTLR_CENTRAL_ISO=y`, `CONFIG_BT_ISO_CENTRAL=y`,
  `CONFIG_BT_PERIPHERAL=n`). Confirmed present.
- `bt-ll-sw-split` snippet: `~/ncs/v3.3.0/zephyr/snippets/bt-ll-sw-split/`
  (overlay disables `&bt_hci_sdc`, enables `&bt_hci_controller`; conf sets
  `CONFIG_BT=y`). Apply via `-DSNIPPET=bt-ll-sw-split` on cpunet.
- nRF sysbuild netcore machinery: `SB_CONFIG_NRF_DEFAULT_BLUETOOTH=y` on cpuapp
  → sysbuild auto-adds `hci_ipc` on cpunet (`~/ncs/v3.3.0/nrf/sysbuild/Kconfig.netcore:67`).

### BD_ADDR source (verified)

No Kconfig hard-codes a BD. The controller reads FICR `DEVICEADDR` (net core
FICR @ `0x01FF02A4`, `DEVICEADDRTYPE` @ `0x01FF02A0` bit0=1=random) and returns
it via `VS_READ_STATIC_ADDRS`. The cpuapp host issues that vendor command in
`bt_setup_random_id_addr()` during `bt_enable()` when `CONFIG_BT_HCI_VS=y`
(defaults on with `BT_HCI_RAW`). Nordic DKs ship with non-zero FICR DEVICEADDR
from factory → reflash yields a real BD. If (rare) FICR reads `0xFFFFFFFF`,
fall back to `bt_id_create()` with a chosen static random addr before
`bt_enable()` in the cpuapp main — but try the reflash first.

### Build setup

Create a **scratch build dir outside the project tree** so it does not pollute
the repo or get committed:

```bash
mkdir -p /tmp/opencode/nrf53-hci-central
cd /tmp/opencode/nrf53-hci-central
```

Copy the hci_usb sample and add the sysbuild glue that points cpunet at the
CIS-central conf + snippet. Minimal wrapper:

```
nrf53-hci-central/
├── CMakeLists.txt          # from hci_usb sample (unchanged)
├── prj.conf                # from hci_usb sample (unchanged)
├── src/                    # from hci_usb sample (unchanged)
├── Kconfig                 # from hci_usb sample (unchanged)
├── sysbuild.cmake          # NEW — pull in hci_ipc cpunet with CIS conf + snippet
└── Kconfig.sysbuild        # NEW — enable SB_CONFIG_NRF_DEFAULT_BLUETOOTH
```

`sysbuild.cmake` (mirror the le-audio-receiver pattern at
`/home/thomas-workstation/repos/le-audio-receiver/sysbuild.cmake`, but central
not peripheral):

```cmake
if(SB_CONFIG_NETCORE_HCI_IPC)
  set(NET_APP_SRC_DIR ${ZEPHYR_BASE}/samples/bluetooth/hci_ipc)

  add_overlay_dts(
    hci_ipc
    ${ZEPHYR_BASE}/snippets/bt-ll-sw-split/bt-ll-sw-split.overlay
  )

  # Central role, CIS enabled (not peripheral/sink)
  add_overlay_config(
    hci_ipc
    ${NET_APP_SRC_DIR}/nrf5340_cpunet_iso_central-bt_ll_sw_split.conf
  )
endif()
```

`Kconfig.sysbuild`:

```kconfig
mainmenu "nRF53 HCI Central sysbuild"

config NRF_DEFAULT_BLUETOOTH
    bool "Use NCS default Bluetooth netcore (hci_ipc)"
    default y
```

**Snippet application note:** the existing le-audio-receiver `sysbuild.cmake`
(commit `b68faae`) applies the `bt-ll-sw-split` snippet to cpunet via
`add_overlay_dts(... bt-ll-sw-split.overlay)` + `add_overlay_config(... iso_peripheral.conf)`
— it does NOT use a `SNIPPET_FOR_<image>` variable. Follow that proven pattern
exactly. The overlay flips the DT nodes (`&bt_hci_sdc` disabled,
`&bt_hci_controller` enabled); the CIS conf already sets `CONFIG_BT=y`, so the
snippet's own conf fragment is redundant. Do not invent a `SNIPPET_FOR_`
mechanism — grep `~/ncs/v3.3.0/nrf/sysbuild/` if tempted; the working pattern
is overlay + conf only.

### Build commands

From the dev shell (`direnv allow` in the le-audio-receiver repo, or
`nix develop` — that shell has west + toolchain scoped):

```bash
cd /tmp/opencode/nrf53-hci-central
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine -d build
```

Scan full build output for warnings. Fix or suppress with a recorded reason —
see AGENTS.md "Policy — never ignore warnings". No warning is accepted as
expected without an explicit suppression.

### Flash commands

The DK's onboard J-Link is the probe. `west flash` uses the Zephyr OpenOCD
runner for nrf5340dk which handles APPROTECT UICR programming automatically.

```bash
west flash -d build --sysbuild
```

If `west flash` cannot find the J-Link, run OpenOCD directly:

```bash
openocd -f interface/jlink.cfg -c "adapter serial 001050023938" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf53.cfg \
  -c init -c "reset run" -c shutdown
```

(Use for reset only; prefer `west flash` for the actual program so UICR
APPROTECT stays correct. Do NOT run `nrf53_recover` — that mass-erases and
re-locks the chip per the AGENTS.md APPROTECT gotcha.)

### Verify BD after reflash

Unplug/replug the DK USB (or `btmgmt -i hci1 power off; btmgmt -i hci1 power on`)
so btusb re-enumerates, then:

```bash
btmgmt -i hci1 info
```

**Acceptance: `addr` must be non-zero** (e.g. `xx:xx:xx:xx:xx:xx` with the top
two bits set, indicating static random). If still `00:00:00:00:00:00`, FICR
DEVICEADDR is unprogrammed — implement the `bt_id_create()` fallback in the
cpuapp `src/main.c` (call before `bt_enable()` with a chosen static random
addr; needs `CONFIG_BT_HCI_SET_PUBLIC_ADDR=y`-adjacent plumbing — grep the
host id.c path from the @ncs-source report). Rebuild + reflash. One attempt.

Also verify CIS settings present:

```bash
btmgmt -i hci1 info | grep -i cis
```

Expect `cis-central` in current settings (the `iso_central` conf enables it).

## Phase 4a stream test — run after central reflash verified

Pre-conditions:
- nRF54L15 Xiao receiver running current firmware (commit `b68faae` build).
  If unsure of image, reflash: `fw-flash-54l15` from the le-audio-receiver
  dev shell. Start serial-mcp on `/dev/ttyACM0` BEFORE reset/flash.
- hci1 BD non-zero, CIS settings present.
- hci0 (nRF5340DK hci_uart) attached via btattach.

### Steps

1. Open serial-mcp on `/dev/ttyACM0` @ 115200 8N1. Confirm clean boot:
   `BLE ready`, `settings_load() OK`, `I2S ready (48 kHz, 16-bit, stereo, 12 blocks)`,
   `Advertising as "LE Audio Receiver"`. Zero warnings. If any warning, fix
   at source — do not proceed over warnings (AGENTS.md policy).

2. Through serial-mcp send:
   ```
   audio reset-stats
   audio status
   ```
   Capture baseline counters.

3. Start btmon capture on hci1:
   ```bash
   sudo btmon -i hci1 -w /tmp/phase4a-dkcentral-hci1.btsnoop
   ```

4. Run the corrected mono test (30 s):
   ```bash
   cd /home/thomas-workstation/repos/le-audio-receiver
   nix develop --command python3 scripts/bap_central.py \
     --adapter hci1 --duration 30 --freq 1000 \
     2>&1 | tee /tmp/phase4a-dkcentral-bap-mono.log
   ```

   `bap_central.py` already supports `--adapter hci1` (commit `6c6ffdf`).

5. While streaming, capture 0.5–1.0 s with sigrok fx2lafw at 24 MHz on
   Xiao D0/D1/D2 + 3V3. Measure BCK (~3.072 MHz), LRCK (48 kHz, ratio 64),
   DIN toggling, 3V3 stable high. Channel map in SESSION_USB_TABLE.md.

6. Query post-stream `audio status` via serial-mcp. Preserve Config, QoS,
   Enable, Start, Stream started, decode counters, warnings/errors.

7. If mono passes (frames decoded climbing, decode errors 0, no steady-state
   underrun, BCK/LRCK/DIN confirmed on LA), run `--stereo` Mode B 30 s and
   repeat serial + LA + stats evidence.

## Anti-recursion rules — MANDATORY

These exist because a prior executor session recursed. Obey literally.

- **Max 2 stream attempts per session.** Attempt = one full
  `bap_central.py --duration 30` run against hci1. If the same failure mode
  recurs twice with identical evidence, **STOP**: report Phase 4a blocked,
  state the exact repeated evidence, do not retry a third time.
- **No fix-loop on `bap_central.py` past 2 iterations** unless a concrete
  new defect is identified with evidence (a traceback, an HCI error code, a
  receiver log line). "Maybe the script is wrong" is not evidence.
- **Build/flash failures**: 1 fix attempt, re-verify. No retry storm.
- **Stop conditions** (any one):
  - Phase 4a acceptance criteria all PASS → done, commit, report.
  - Same failure twice with identical evidence → blocked, commit results
    update, report exact blocker.
  - New defect identified → one fix attempt, re-test once. If still failing
    with the new defect fixed, count toward the 2-attempt cap.
  - A step requires unavailable hardware, credentials, or a destructive
    action not pre-approved → stop and ask.
- **Never** erase ZMS/settings on the Xiao. Never hardcode probe serials
  (use `nrf-probes --find nrf54l` for Xiao, J-Link serial `001050023938`
  only in explicit openocd commands). Never run `nrf53_recover`.

## Phase 4a acceptance criteria (unchanged from prior handoffs)

1. Latest nRF54L15 boots cleanly, no warnings/errors.
2. BlueZ source (hci1) establishes BAP transport + writes LC3 SDUs for the
   requested duration.
3. Receiver logs ASE Configure/QoS/Enable/Start and `Stream started`.
4. Frames decoded climb; decode errors = 0.
5. Logic capture proves BCK ~3.072 MHz, LRCK 48 kHz, DIN toggling.
6. No steady-state I2S underrun/slab-full/reset storm.
7. Results document has exact commands + evidence, no unsupported conclusions.
8. Working tree clean; no build artifacts tracked.

## Results update

Rewrite `docs/development/phase4a-results.md`:
- Replace the "BLOCKED — no working CIS-capable central" status with the
  DK-central outcome (PASS or BLOCKED with new evidence).
- Keep the hci0 CIS-failure evidence (still valid, documented in STATUS.md).
- Add a section "nRF5340 DK as hci1 central" with: FICR-derived BD shown by
  `btmgmt -i hci1 info`, CIS settings, btmon `LE CIS Established` events (or
  the exact failure if CIS still fails), receiver serial, LA measurements,
  `audio status` counters.
- State each acceptance criterion PASS/FAIL with exact evidence. Do not infer
  controller incapability from an application-level error alone.

If Phase 4a passes: mark accepted, state Phase 4b GRTC+DPPI is next and
mandatory. If still blocked after the 2-attempt cap: mark blocked, identify
the exact user decision/resource needed (e.g. a third CIS-capable dongle,
or pivot to Phase 4b GRTC which is stream-independent).

## Commit

- New commit in the le-audio-receiver repo. Do not amend prior commits.
- Stage only: `docs/development/phase4a-results.md`,
  `docs/development/phase4a-central-handoff.md` (this file — copy it into
  `docs/development/`), and any `bap_central.py` fix (if a new defect was
  found and fixed).
- Do NOT stage the `/tmp/opencode/nrf53-hci-central/` build tree — it is
  scratch, outside the repo.
- Do not push. No attribution footer (AGENTS.md global rule).

## Report back

Return:
- Commit hash + message.
- `btmgmt -i hci1 info` output after reflash (proving non-zero BD).
- btmon text decode of the CIS establishment sequence (or failure).
- Receiver serial excerpts: boot, ASE Config/QoS/Enable/Start, Stream started,
  decode counters, any warnings.
- sigrok measurement summary (BCK/LRCK/DIN/3V3).
- `audio status` post-stream counters.
- Each acceptance criterion PASS/FAIL with evidence pointer.
- If blocked: the exact repeated evidence + recommended next step
  (Phase 4b GRTC, or a third central, or other).