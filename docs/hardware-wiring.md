# Hardware wiring

How to wire an I2S DAC to either supported board. Both boards use the same
3-wire no-MCK topology (BCLK, LRCK, SDOUT; the DAC's internal PLL locks to
BCLK). For how the audio path works on each chip, see the
[technology notes](technology/).

## DAC choice — UDA1334A or PCM5102A

Either works. The project was developed on the **CJMCU-1334 (UDA1334A)** and
that is the default wiring documented below. The **PCM5102A** is the better
DAC on paper (112 dB SNR, 32-bit/384 kHz vs the UDA1334A's 100 dB / 16-bit),
but at the LE Audio sink floor of **48 kHz / 16-bit LC3**, both DACs exceed
the codec's dynamic range by a wide margin — the spec advantage is inaudible
here.

Practical differences when wiring to this project's 3-wire no-MCK topology:

- **UDA1334A (CJMCU-1334 / Adafruit #3678):** Format (SF0/SF1) and MUTE are
  pre-pulled to GND on the Adafruit breakout — no config wires needed on that
  specific board. Not all clones or assemblies are equivalent.
- **PCM5102A (GY-PCM5102 and clones):** the **SCK pad must be solder-bridged
  to GND** to enable internal-PLL 3-wire mode. If the pad is open you get
  silence or hiss — the single most-reported PCM5102A "no sound" cause.
  Adafruit's own PCM5102 breakout (#6250) has this handled; cheap clones often
  don't.

### Hardware validation before trusting a DAC breakout

Before treating a new DAC breakout/wiring assembly as working, validate the
I2S waveform with a standalone test (e.g. a tone loop that drives BCK/LRCK/
SDOUT without the full BLE audio stack). An old CJMCU-1334-compatible breakout
tested with this project **held LRCK high when unmuted** and is not suitable —
the breakout or wiring assembly was incompatible or defective.

**MUTE high mutes the analog output** (inverted logic — LOW = unmuted).
Raising MUTE is a silence/diagnostic control, not a fix for an I2S-line
anomaly.

Use whichever DAC you prefer. Pinout below is identical for both — 3 wires,
no MCK.

## I2S wiring — nRF5340 (Ebyte E83-2G4M03S)

Verified pin table (from the board's devicetree pinctrl):

| E83 pin | nRF5340 GPIO | I2S signal | → CJMCU-1334 |
|---------|--------------|-----------|---------------|
| — | P1.15 | SCK_M (BCK)  | **BCLK** |
| — | P1.13 | SDOUT (DIN)  | **DIN**  |
| — | P1.12 | LRCK_M (WSEL)| **WSEL** |
| 3V3 | — | — | **VIN** |
| GND | — | — | **GND** + **AGND** (tie both) |

No MCK — UDA1334A internal PLL locks to BCLK. (Board-specific Kconfig:
`CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`.)

## I2S wiring — nRF54L15 (Seeed Xiao)

Uses three free Xiao header pins, **D0 / D1 / D2**, all on GPIO port 1 (same
power domain as the working UART). This pin set deliberately avoids a
pin-conflict trap in an earlier wiring revision (P1.10/P1.11/P1.12 collided
with other peripherals — Zephyr does not detect pin overlaps, it silently
corrupts the loser).

| Xiao pin | nRF54L15 GPIO | I2S signal | → CJMCU-1334 |
|----------|---------------|-----------|---------------|
| **D0** | P1.4 | SCK_M (BCK)  | **BCLK** |
| **D1** | P1.5 | LRCK_M (WSEL)| **WSEL** |
| **D2** | P1.6 | SDOUT (DIN)  | **DIN**  |
| 3V3 | — | — | **VIN** |
| GND | — | — | **GND** + **AGND** (tie both) |

**MCK note (nRF54L15):** the I2S20 peripheral needs an MCK PSEL routed even
though the DAC doesn't consume it (3-wire no-MCK topology). The board overlay
routes MCK to **D3 (P1.7)** so the MCK generator can derive SCK/LRCK. D3 is
occupied by a peripheral-driven MCK — do not use it for other signals. The DAC
side stays 3-wire: BCK, LRCK, SDOUT only.

## CJMCU-1334 / UDA1334A config pins

On the Adafruit breakout (and faithful clones), these are pre-pulled to GND by
on-PCB resistors (verified against the Adafruit PCB schematic):

- **SF0** → GND via R10 (format bit 0 → I2S)
- **SF1** → GND via R2  (format bit 1 → I2S)
- **MUTE** → GND via R9 (LOW = unmuted; inverted vs most mute pins)

Leave them unconnected on the Adafruit board. On a bare clone without the
pulldowns, wire all three to GND explicitly.

## Leave unconnected

| CJMCU-1334 pin | Why |
|----------------|-----|
| **SCLK** | system-clock output in video mode; unused in audio mode |
| **PLL** | pulled low by default = audio PLL mode; don't tie high |
| **DEEM** | de-emphasis off; float or GND |
| **3V0** | regulated 3.3 V output from the board's own LDO — do not feed in |

## Audio out

CJMCU-1334 outputs **line level** (no headphone amp on the breakout). Connect:

| CJMCU-1334 | → |
|------------|---|
| **Lout** | left channel → line-in L / headphone L via amp |
| **Rout** | right channel → line-in R / headphone R via amp |
| **AGND** | sleeve / line ground (already tied to GND above) |

## PCM5102A note

Same D0/D1/D2 (nRF54L15) or P1.15/P1.13/P1.12 (nRF5340) → BCLK / DIN / LRCK
mapping. **Solder-bridge SCK to GND** on cheap GY-PCM5102 clones (see DAC
choice above). No MCK wire.

## Related documents

- [Technology: nRF5340](technology/nrf5340.md)
- [Technology: nRF54L15](technology/nrf54l15.md)
- [User guide](user-guide.md)
- [Known limitations](known-limitations.md)
