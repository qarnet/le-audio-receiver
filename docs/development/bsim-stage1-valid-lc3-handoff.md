# BabbleSim Stage 1 — receiver-specific valid-LC3 radio gate

## Scope

One nRF5340bsim receiver peripheral + one nRF5340bsim custom BAP client + PHY.
One mono sink ASE, 48 kHz, 10 ms, 120 octets, valid runtime-encoded LC3 sine.
No Mode A/B duplication yet.

## First fix Stage 0 truth

- `bsim-official-smoke.sh` must exit nonzero when any official child fails.
- Mark official baseline PARTIAL: environment/streaming proven, official teardown
  and ACL-disconnect scenarios fail in pinned NCS. Never label them PASS.
- Commit previously omitted Stage0 review handoff; keep tree clean.

## Receiver binary

- Add proper nRF5340bsim dual-core sysbuild files based exactly on
  `zephyr/tests/bsim/bluetooth/audio/{Kconfig.sysbuild,sysbuild.cmake}` using
  `hci_ipc` and `nrf5340_cpunet_iso-bt_ll_sw_split.conf` plus controller overlay.
- Build current receiver `main.c`, `bt_bap.c`, `audio_decode.c`, volume/lifecycle/
  offload dependencies; substitute only `audio_sink_stub.c` for physical I2S.
- Correct stale prj.conf comment claiming no separate LL; enable settings calls
  as required for dynamic PACS/ASCS without persistent storage.
- Sink stub requires exactly 960 samples per push, checks at least one nonzero
  sample and nonzero bounded energy over stream, tracks rolling CRC, fails on
  malformed count or pushes after stop, passes after 100 valid decoded pushes.
  Export counters for result log. Do not claim exact PCM/audio quality.

## Client binary without NCS source edits

Create `tests/bsim/client/` that reuses upstream
`samples/bluetooth/bap_unicast_client/src/{main.c,stream_tx.c,stream_lc3.c}`.

- Forced-include repo header first includes preset header, then maps sample's
  `BT_BAP_LC3_UNICAST_PRESET_16_2_1` use to
  `BT_BAP_LC3_UNICAST_PRESET_48_4_1`.
- Client config: sink ASE count=1, source ASE count=0, group stream count=1,
  ISO max chan=1, LIBLC3/FPU, 48 kHz encoder support, SW Split ISO TX SDU/buffer
  limits >=120 bytes.
- Wrap `bt_bap_stream_send` through forced macro to call real API and atomically
  count successful TX submissions. BSim installer passes client at 100 TX.
- Use same dual-core sysbuild pattern. Do not copy/modify upstream source files.

## Runner

Add strict repo build/run script using `compile.source`:

- compile receiver and client as sysbuild executables from repo app root;
- unique simulation ID;
- receiver device 0/testid receiver, client device 1/testid valid_lc3_client,
  both `-RealEncryption=1`, same `-D=2`;
- PHY sim length 40e6;
- propagate each child exit. Preserve per-process logs in `/tmp` and print paths.

## Acceptance

- Two consecutive runs, all three processes exit zero.
- Advertising, encryption, PACS/ASCS, codec/QoS/enable/start, CIS all observed.
- Client >=100 successful sends; receiver exactly reaches >=100 decoded pushes.
- Receiver decode errors zero; no malformed sample count; nonzero PCM/energy/CRC;
  no late push after stop.
- Build has SW Split cpunet child for both applications.

Document limitations: no I2S/FLPR/PCLK/SDC/CPU/audio-quality validation. Update
design/status only after pass. Commit code/tests/scripts/docs. No component
revision changes/downloads/packages/push/hardware/security changes.
