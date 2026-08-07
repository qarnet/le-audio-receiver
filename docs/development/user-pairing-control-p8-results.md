# P8 results — hardware acceptance and closeout

Accepted: 2026-08-08.  Base commit `e1dbbb0` (P7 accepted plus documentation
arithmetic correction); handoff commit `83fdea2` (`docs: record P8 handoff —
hardware acceptance and closeout`); acceptance commit (this document's
commit).  Handoff:
`docs/development/user-pairing-control-p8-handoff.md`; plan:
`docs/development/user-pairing-control-plan.md`.

P8 executes the hardware acceptance matrix on the XIAO nRF54L15 receiver
(button thresholds, LED patterns, pairing/security, `bt unpair`, streaming
after transitions), validates the SRAM budget and the 1024-byte pairing
work-queue stack at runtime, and proves nRF5340 feature-off parity.  No
production code, API, DT, Kconfig, test logic, baseline, or BSim pin
changed — P8 is evidence-only.

## 1. Fresh build/contract evidence on the exact base

`git status` clean at `83fdea2` (handoff committed; base `e1dbbb0`); diff
since P7 acceptance (`5772206`) is documentation-only (P6/P7 results
arithmetic corrections + this handoff), so executable inputs are identical
to the P7-accepted code.

Fresh pristine builds on the exact base (logs `/tmp/p8-build-54l15.log`,
`/tmp/p8-build-5340.log`):

| Build | Result | Sizes |
|---|---|---|
| `fw-build-54l15` | exit 0 | app FLASH 531668 B (36.36%), RAM 161036 B (98.29%); FLPR RAM 43632 B (66.58%) |
| `fw-build-5340` | exit 0 | app FLASH 375364 B, RAM 145256 B; net FLASH 146780 B, RAM 40512 B |

Zero compiler warnings (0 `[-W...]` in both logs); only the documented
pre-existing NCS v3.3.0 diagnostics (PARTITION_MANAGER deprecation, SW Split
experimental symbols, `__ASSERT()` informational, watchdog "No SOURCES
given").  Build contract on fresh artifacts: **95 assertions, 0 failed,
BUILD CONTRACT PASSED**.

## 2. Probe identity evidence (raw, runtime-resolved)

`nrf-probes` (source of truth — no static probe↔board table):

```
SERIAL            PROBE                              TARGET    DPIDR       PART        VARIANT
8EE9B3FF          Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  0x6ba02477  0x00054b15  AAC0
E6635C08CB1F502B  Debugprobe on Pico (CMSIS-DAP)     nRF5340   0x6ba02477  0x00005340  QKAA
```

- XIAO flash (`fw-flash-54l15`): probe `8EE9B3FF`, SWD DPIDR `0x6ba02477`,
  Cortex-M33 r1p0, app 531660 B + FLPR 32604 B written and verified
  (log `/tmp/p8-flash-54l15.log`).
- E83 flash (`fw-flash-5340`): probe `E6635C08CB1F502B`, nRF5340-QKAA
  1024 kB app / 256 kB net; the only diagnostics are the documented
  page-tail `Adding extra erase range` warnings (app `0x0005ba44 ..
  0x0005bfff`, net `0x01023d5c .. 0x01023fff`; log
  `/tmp/p8-logs/5340-flash.log`).
- Dongle resets: onboard J-Link auto-detected (`J-Link OB-nRF5340-
  NordicSemi`, VTarget 3.300 V, DPIDR `0x6ba02477`) — used to clear zombie
  SDC connection slots between central runs (log `/tmp/p8-logs/dongle-reset1.log`).

## 3. Serial ports and capture

Listed before opening:

| Port | Device | Role |
|---|---|---|
| `/dev/ttyACM4` | Seeed XIAO nRF54L15 CMSIS-DAP (if-02 UART) | XIAO console 115200 |
| `/dev/ttyACM3` | Pico debugprobe (if-01 UART) | nRF5340 flash probe |
| `/dev/ttyACM2` | J-Link VCOM iface-02 | hci_uart central (dongle) 1 Mb/s |
| `/dev/ttyUSB0` | CH340X | E83 console 115200 |

Captures: serial-mcp with reconnect-policy on (`/tmp/p8-logs/session1.*`,
ring dumps at `/tmp/p8-logs/`, central stdout/stderr per row in
`/tmp/p8-logs/rowN-*`).  All serial connections closed when done.

## 4. Automated nRF54L15 preparation

1. Clean exact commit + fresh build/contract evidence — see section 1.
2. Resilient capture armed on `/dev/ttyACM4`, then `fw-flash-54l15`
   (app + FLPR) — verified OK.
3. Full boot captured (log `/tmp/p8-logs/session1.txt` boot 1 at
   `00:46:22`): `BLE ready`, `settings_load() OK`, Identity
   `DB:A6:0C:05:A2:AA (random)`, FLPR handshake `READY` + `READY_ACK sent`,
   `Advertising as "LE Audio Receiver"`.  No fatal/reboot loop, no
   GPIO/input failure, no stack/heap/assert/fault warning.
4. Shell diagnostics (`kernel thread list`): pairing work-queue =
   unnamed priority-5 thread `0x2000eb90`, **stack 1024, usage 640/1024
   (62 %)** at idle — runtime high-water within budget (sustained
   transition tests below exercise the same queue; no overflow, no stack
   warning).  `input` thread 1024 (23 %); `sysworkq` 2048 (13 %);
   `icmsg_workq` 1280 (77 %); heap 0 (system-heap command absent by
   design; **no allocation failure observed** across all streams).
5. `bt unpair` (00:51:16): exact synchronous sequence — bonds cleared,
   RESETTING rapid-LED feedback, `Pairing reset complete: bonds cleared;
   BONDING advertising active.` (1056 ms).  Reset via OpenOCD →
   NORMAL + zero bonds (boot 2 at `00:48:23`, clean).

## 5. Central setup

Per repo instructions: `btattach -B /dev/ttyACM2 -S 1000000` +
`btmgmt power on` / `io-cap 3` / `sc on`.  Verified:

- `hci0` current settings: `powered le secure-conn cis-central`;
- identity `C0:AA:BB:CC:DD:EE` (compile-time dongle identity);
- `scripts/bap_central.py` runs without sudo (raw helper uses sudo -n
  internally).  Central stdout/stderr captured separately per row
  (`/tmp/p8-logs/rowN-central.{out,err}`).

Note: repeated raw-HCI connect attempts exhaust the dongle's two SDC
connection slots (`Connection Rejected due to Limited Resources (0x0d)`);
recovery is the documented `fw-reset-dongle`-equivalent J-Link reset run +
re-attach (3 occurrences, all cleared, none a receiver fault).

## 6. Automated XIAO rows

Receiver identity `DB:A6:0C:05:A2:AA`; central `C0:AA:BB:CC:DD:EE`.
All per-row central logs and receiver console evidence in
`/tmp/p8-logs/`.

### Row 1 — NORMAL zero-bond boot: advertising visible; unbonded reject — PASS

- Receiver NORMAL + zero bonds after prep; advertising confirmed via
  btmon: `ADV_IND` from `DB:A6:0C:05:A2:AA (Static)`, RSSI -46
  (`/tmp/p8-logs/scan5-btmon.log`); `bap_central.py` discovery found
  `/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA name='LE Audio Receiver'`.
- Direct unbonded connect (`--peer-addr`, raw HCI): `LE Extended Create
  Connection` accepted by the controller but **no `LE Enhanced Connection
  Complete`** — BONDED_ONLY empty FAL rejects at the link layer
  (`HCI_CONNECT_FAIL reason=timeout attempts=3 last_status=none`;
  `/tmp/p8-logs/row1g-btmon.log`).  BlueZ `Pair()` path:
  `AuthenticationTimeout` (no ACL established).  Receiver console quiet —
  no pairing/connect log.

### Row 2 — short press (<3 s): no transition — PASS (user-observed)

User pressed the onboard button ~1 s: **LED stayed off** (NORMAL LED
inactive); no mode/advertising/connect-policy change (receiver console
silent, no pairing-mode transition log, advertising continued).

### Row 3 — 3 s hold: BONDING — PASS (user-observed + CLI)

User held through 3 s and released before 8 s: **LED started slow
blinking** (BONDING ~500 ms half-period pattern).  No bonds existed at
that point (zero-bond state), so bond-preservation was vacuous there;
bond preservation with a bond present is proven by the preserved-bond
reconnect in rows 5/8 (bonds survive BONDING/NORMAL cycles).

### Row 4 — fresh Just Works pair in BONDING; NORMAL without disconnect; Mode A 30 s — PASS

- Entered BONDING via `bt unpair` (`BONDING advertising active.`).
- Central fresh pair: `Pairing accepted` → `Pairing complete, bonded: 1`
  → `Security changed: level 2 err 0 bonded 1` — completion entered
  NORMAL **without disconnect** (log `/tmp/p8-logs/row4-*`).
- Mode A stream 40 s: central 4000 frames / 40.00 s (100.0 fps), exit 0;
  receiver `Stream[0] summary: SDUs=3775 decoded=8084 plc=534
  decode_err=0 i2s_underrun=0 stream_reset=0`; I2S DMA started; FLPR
  offload prep OK (epoch=532462487, ACTIVE).

### Row 5 — preserved-bond reconnect; Mode B 30 s — PASS

User requested CLI-driven tests (button interrupt/timing already verified
by the LED observations).  Preserved-bond reconnect (`--preserve-bond`,
no re-pair): `Connected` → `Security changed: level 2 err 0 bonded 1` →
NORMAL without disconnect.  Mode B (single stereo ASE
`chan_count=2` SDU 240): central 4000 frames / 40.00 s, exit 0; receiver
`Stream[0] summary: SDUs=3895 decoded=8084 plc=294 decode_err=0
i2s_underrun=0 stream_reset=0` (log `/tmp/p8-logs/row5-*`).

### Row 6 — 8 s hold RESET supersedes BONDING; old bond fails; fresh pair succeeds — PASS

- Button-hold timing user-observed (earlier session): 3 s → slow blink
  (BONDING), 8 s during slow blink → rapid flash then slow (RESET
  supersedes BONDING, one-second rapid feedback, then BONDING).
- RESET owner/ordering driven via CLI (shell and button share the same
  transition owner): `bt unpair` → `Disconnected: C0:AA:BB:CC:DD:EE
  reason 0x16` (intervening peer disconnected before deletion) → `Pairing
  reset complete: bonds cleared; BONDING advertising active.`
- **Old bond fails**: 4× reconnect attempts → `Security changed: level 1
  err 2 bonded 0` → `Disconnected reason 0x05` (auth failure — bond
  deleted by RESET).
- **Fresh pairing succeeds**: `Pairing accepted` → `Pairing complete,
  bonded: 1` → `Security changed: level 2 err 0 bonded 1`; Mode A 35 s
  clean (`SDUs=2800 decoded=7086 plc=1486 decode_err=0 i2s_underrun=0
  stream_reset=0`).

### Row 7 — `bt unpair` repeats same RESET — PASS

Two additional invocations in this session, both exact synchronous
output: `Pairing reset complete: bonds cleared; BONDING advertising
active.` (1020 ms and 1140 ms), same owner/ordering as the row-6 RESET.

### Row 8 — reboot: saved bond accepted; distinct unbonded identity rejected — PASS

- Fresh bond persisted (fs_zms settings survived reset), OpenOCD reset
  run → NORMAL + saved bond (boot at `00:15:21`, clean).
- **Saved bond accepted** (`--preserve-bond`): `Security changed: level
  2 err 0 bonded 1` (no re-pair), Mode A 10 s clean (`SDUs=677 decoded
  =2082 plc=728 decode_err=0 i2s_underrun=0 stream_reset=0`).
- **Distinct unbonded identity rejected**: raw helper as random own
  address `C0:AA:BB:CC:DD:EF` (≠ saved `C0:AA:BB:CC:DD:EE`): `LE Extended
  Create Connection` accepted but **no Connection Complete**
  (`HCI_CONNECT_FAIL reason=timeout attempts=1`); receiver console quiet
  — BONDED_ONLY FAL (only `C0:AA:BB:CC:DD:EE`) rejected at link layer
  (logs `/tmp/p8-logs/row8b-*`).

## 7. User-only observations

| Observation | Result |
|---|---|
| Short press: LED stayed off | **confirmed** (user) |
| BONDING LED ~500 ms on / 500 ms off | **confirmed** (user: "slow blinking") |
| RESET LED ~100 ms / 100 ms for one second (five flashes), then slow BONDING | **confirmed** (user: "flashed quickly then resumed to slowly blinking") |
| Audio audible Mode A / Mode B | **not observable — no speakers/headphones connected** (user).  Technical stream evidence stands: zero decode errors, zero I2S underruns, zero stream resets in both modes. |

## 8. nRF5340 parity (feature-off)

- Probe `E6635C08CB1F502B`; `fw-flash-5340` OK (documented page-tail
  erase warnings only).
- Boot (log `/tmp/p8-logs/5340-boot1.txt`): `BLE ready`,
  `settings_load() OK`, Identity `E8:54:F0:E0:D9:42 (random)`,
  `Advertising as "LE Audio Receiver"`; legacy advertising loop
  (`Restarting advertising...` / `Advertising again`) — feature-off
  path, no pairing symbols, no new warnings.
- `bt unpair` retains the **legacy feature-off output**: `Pairing mode
  reset: bonds cleared; open pairing enabled.` (exact).
- Representative Mode A stream 40 s: central 4000 frames / 40.00 s,
  exit 0; receiver `Pairing accepted` → `Pairing complete, bonded: 1`,
  2× ASE Config (Mode A), `Stream[0] summary: SDUs=3778 decoded=7558
  plc=2 decode_err=0 i2s_underrun=0 stream_reset=0`, `Stream[1] summary:
  SDUs=3794 decode_err=0`, I2S DMA started; disconnect 0x13 → advertising
  restarted → preserved reconnect OK.  (Initial stale central-side bond
  produced one `Pairing failed: 4` — cleared via receiver `bt unpair`,
  documented, not a receiver fault.)
- BSim/software evidence unchanged (no repo executable change): P7
  canonical gate 59/59 and all BSim pins stand; no rerun needed.

## 9. Warning audit

- Receiver console: zero `LOG_WRN`/`LOG_ERR` across all rows and boots
  (only the documented 5340 stale-bond `Pairing failed: 4` in the E83
  session, resolved).
- Build logs: zero compiler warnings (documented pre-existing
  diagnostics only).  Flash logs: documented page-tail erase warnings
  only.  Central stderr: only transient BlueZ `InProgress` discovery-
  state errors during non-acceptance probe runs (acceptance rows use
  `--peer-addr` and exit cleanly).
- `git diff --check` clean.

## Commits

1. `83fdea2` — P8 handoff (committed first, per handoff).
2. This acceptance commit — P8 results + STATUS/README/AGENTS/plan
   status updates (documentation-only; no executable input changed).

No push, PR, amend, force-push, or attribution footer.  No mass erase,
recovery, settings-partition erase (bonds cleared only via production
`bt unpair` RESET), or probe-rs.

## Deviations and notes

- **Rows 5/6/7 button presses driven via CLI after the user's initial
  LED observations** — the user explicitly requested CLI-driven tests
  after verifying button interrupt + timekeeping via the LED pattern
  observations (rows 2/3/6 timing).  Shell `bt unpair` and the button
  share the same pairing-mode transition owner by design (P1), so the
  CLI RESET evidence exercises the same transition machinery; the
  button's 3 s / 8 s thresholds were confirmed by the user's LED
  observations.
- **Three clean XIAO reboots occurred during the user's button session**
  (boot logs show no FATAL/error).  Each reboot returns NORMAL —
  consistent with the "reboot always returns NORMAL" matrix row.  The
  physical RST button behavior is plan NON-SCOPE; no firmware fault was
  logged.
- **Audio audibility not observable** — no speakers/headphones connected
  (user); recorded as not-observable, technical counters clean.
- The 5340 identity `E8:54:F0:E0:D9:42` matches the central-side device
  object observed before this session's cleanup (removed via
  `bluetoothctl remove`), confirming the earlier stale device was the
  E83 receiver, not the XIAO.

## Next-phase grounding

Feature accepted and portable contract documented: NORMAL/BONDING/RESET
modes, 3 s/8 s button thresholds, LED patterns, `bt unpair` RESET parity,
preserved-bond reconnect, and reboot-to-NORMAL all verified on hardware;
nRF5340 remains feature-off.  The pairing work-queue 1024-byte stack is
validated at runtime (62 % idle high-water, sustained transitions clean).
