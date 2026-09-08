# Withdrawn DevZone draft: nRF5340 SDC central ISO TX collapse

> [!CAUTION]
> **Do not post this question.** Passing controller-clock source
> fix-validation invalidated the proposed SDC-defect report. Preserve the text
> below only as a record of the superseded escalation draft.

Status: **WITHDRAWN on 2026-09-08; nothing was posted.** The old source
scheduler produced the recorded collapse, but HCI LE Read ISO TX Sync was
mistakenly described as an aired-SDU counter and the source fixture had not yet
been validated with its final mirrored controller-clock scheduler at sufficient
CPU throughput.

## Resolution

The corrected source fixture now:

- mirrors the CPUNET controller clock into application-core RTC0;
- encodes before waiting for a pinned event;
- schedules one semantic frame per event with a 3000 us target lead and 2000 us
  minimum;
- runs the nRF5340 application core at 128 MHz, matching Nordic Bluetooth ISO
  and audio examples.

The exact 10 ms Mode B row passed as
`rh3-modeb-sdc-controller-clock-128mhz-20260908`: all 12644 SDUs submitted,
12000 scored, zero send failures, zero skipped events, and receiver
`rx_valid=12644`. The exact 10 ms Mode A row then passed as
`rh3-modea-sdc-controller-clock-128mhz-20260908`: both streams submitted 12644
SDUs and scored 12000, zero send failures or skipped events, with receiver
`rx_valid=12645/12644`. Both rows used the same SDC CPUNET and receiver images.

The immediate 64 MHz baseline of the final scheduler failed its run deadline
with `sub=10907`, `sc=10763`, and `skip=10185`. Raising only the source CPUAPP
clock for that scheduler removed the throughput failure. This A/B comparison
does not assign every older ModeA12-18 failure to one sub-cause, but the passing
current source proves that the proposed SDC defect is not established.

Canonical result:
[system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

## Historical validation checklist

The following checks were completed before the draft was withdrawn. They remain
source-research history, not support for posting the defect claim.

1. Supported feature: nRF5340 Isochronous Channels = "Supported" in the
   official software maturity table (nRF53 tab, no caveat; the
   ISO-encryption footnote is nRF52-only). CIS-central is a listed SDC
   library feature.
2. API usage: our chain is the documented preferred flow (timestamps
   mode; first SDU untimestamped; HCI VS ISO Read TX Timestamp
   readback; subsequent SDUs pinned to assigned + k x interval,
   submitted at least the arrival margin before the pinned event) and
   mirrors the nrf iso_time_sync sample (central TX role) and the
   nrf5340_audio production host.
3. Known issues: SDC limitations.rst (v3.3.0) has no matching entry
   (SDU > 1255 B, framed CIG params, nRF54H RAM only). DRGN-23776
   (CIS central, nRF53, encrypted ISO, invalid MIC) is fixed in the
   v3.3.0 changelog block we run. DRGN-21293/21605 document readback
   semantics; our measurements match them.
4. Errata: nRF5340 silicon errata (Rev 1 / Eng A / Eng D, via
   docs.nordicsemi.com) contain no ISO/CIS transmission anomaly.
5. Newer SDK: SDC changelogs for v3.3.1, v3.3.3, v3.4.0, and main
   contain no fix matching this signature (CIS-central TX airing under
   timestamp provisioning).

## Withdrawn question text (do not post)

Subject: [WITHDRAWN] nRF5340 SDC central CIS TX stops airing after first event under
timestamp-mode ISO provisioning (NCS v3.3.0) - supported-feature defect
report

Body:

Setup:

- Hardware: nRF5340-DK (central/source, app core Zephyr host + network
  core hci_ipc with SoftDevice Controller, multirole) -> nRF54L15-DK
  (peripheral/sink, SDC, LE Audio BAP unicast server sink).
- NCS v3.3.0; SDC changelog block "nRF Connect SDK v3.3.0" (the one that
  includes the DRGN-23776 fix).
- Stream: BAP unicast, 48_4_1 preset - 48 kHz, 10 ms frame, Mode B
  (single ASE, 2 channels, SDU 240 bytes), unframed, 2M PHY, RTN 5,
  Max_Transport_Latency 20 ms. Controller-selected CIG: ISO interval
  10 ms, BN 1, NSE 3, FT 16 subevents, cig_sync 4146 us. ACL uses
  BT_BAP_CONN_PARAM_RELAXED (50-70 ms). Link encrypted (L2, bonded) -
  per the nRF53 software maturity table, encrypted ISO is supported on
  nRF5340 (the no-ISO-encryption footnote applies to nRF52 only).
- Host provisioning: SDC timestamps mode, exactly the documented chain
  (isochronous_channels.rst) and the iso_time_sync sample pattern:
  first SDU sent untimestamped (sn=0), HCI VS ISO Read TX Timestamp
  after its completion, every later SDU sent with
  bt_bap_stream_send_ts() pinned to assigned + k x 10000 us, one pin
  per event, submitted 2-3 ms before the pinned event
  (HCI_ISO_TX_SDU_ARRIVAL_MARGIN_US + nRF53 IPC allowance).

Observed (receiver-side counters are the on-air evidence):

- The central accepts every SDU: HCI ISO TX completes for all 12644
  submitted SDUs (no send failures), and the VS readback grid advances
  exactly one 10 ms interval per submitted SDU for the whole 2-minute
  stream (12643 readbacks spanning 126.41 s; DRGN-21293 schedule
  semantics).
- The receiver receives almost nothing: 3 valid SDUs of 12644
  (rx_lost=19308, controller rx_unreceived near-total, crc_error=0,
  duplicate=2). Under a looser host pacing the same receiver gets
  12643/12644 from the same central and the same stack (see table); it
  also passes this exact stream shape from Linux SDC centrals (Intel
  AX210, and a separate nRF SDC dongle history at 120 s/300 s).

Variation table (same central firmware, same receiver, same stream
shape; only the host submission discipline differs):

| Host submission discipline | Receiver valid SDUs (of 12644) |
| --- | --- |
| Host-clock-paced (not grid-disciplined; pins often ran ahead, guard fired ~2040x) | 12643 |
| Free-run, pins on the readback grid | 167 |
| Free-run + completion-driven readback, pins on grid | 167 |
| Bounded 4 events ahead of the readback grid | 168 |
| Documented lead-window (3 ms before pin, offset resynced per readback, pins exactly 1 interval ahead) | 3 |
| Same + TX_Sync poll in status | 3 |

Question: is there a known reason the SDC central would accept and
schedule (HCI-complete + readback grid) timestamp-provisioned ISO SDUs
but not air them past the first event, when the identical stream from
the same central airs fine under host-clock-paced (time-of-arrival
like) submission? Is this a known issue, a configuration constraint we
are missing (CIG structure FT=16/NSE=3/BN=1? CIG reserved time?
central ISO buffers?), or a defect we should file?

Additional data available on request: per-run HIL1 status records
carrying the assigned-timestamp envelope, pin values, and skip counts;
receiver ISO link-quality counters; full console logs; the exact
source/receiver image hashes.

## Historical sources reviewed

1. Software maturity, nRF5340 ISO "Supported": nRF Connect SDK
   documentation, Software maturity levels, Bluetooth features
   support, nRF53 Series table (nrf/doc/nrf/releases_and_maturity/
   software_maturity.rst in the installed v3.3.0 tree). The footnote
   "Do not support encrypting and decrypting the Isochronous Channels
   packets" is attached to nRF52-series entries only.
2. SDC feature table listing "Connected Isochronous Stream - Central":
   nrfxlib softdevice_controller README (installed v3.3.0 tree).
3. Timestamps mode as "the preferred way of providing data to the
   SoftDevice Controller", the arrival margin requirement, and the
   first-SDU-plain/readback/pin chain: nrfxlib
   softdevice_controller/doc/isochronous_channels.rst, "Providing data"
   (installed v3.3.0 tree).
4. Reference implementation: nrf/samples/bluetooth/iso_time_sync/
   src/iso_tx.c (central TX: bt_iso_chan_send_ts + VS readback; the
   sample's own output logs one pinned SDU per 10 ms event
   successfully).
5. HCI_ISO_TX_SDU_ARRIVAL_MARGIN_US = 1000 us:
   nrfxlib/softdevice_controller/include/sdc_hci.h.
6. DRGN-21293 (LE Read ISO TX Sync returns the schedule reference of
   the previously scheduled SDU): SDC known issues (nrfxlib docs).
7. DRGN-23776 fixed in the v3.3.0 changelog block: nrfxlib
   softdevice_controller/CHANGELOG.rst (installed v3.3.0 tree).
8. SDC limitations (no matching entry): nrfxlib
   softdevice_controller/limitations.rst (installed v3.3.0 tree).
9. No matching newer-SDK fix: SDC CHANGELOG.rst v3.3.1, v3.3.3,
   v3.4.0, and main (via Nordic docs).
10. nRF5340 silicon errata (Rev 1 / Eng A / Eng D tables; RADIO
    anomalies are parameter/current/spurious-emission class):
    docs.nordicsemi.com nRF5340 errata pages (retrieved via Nordic
    documentation MCP, 2026-09-08).

## Disposition

- [x] Cancel posting.
- [x] Retain historical evidence roots without mutation.
- [x] Replace defect escalation with the fixed 14-child RH3 matrix validation
  of the corrected source fixture.
