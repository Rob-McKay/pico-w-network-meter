# Seeed Studio XIAO ESP32-C5: vendor documents

Reference documents for the XIAO ESP32-C5 port
([docs/port-esp32c5.md](../../../docs/port-esp32c5.md)). They are kept
here so the study's references stay checkable when Seeed's or
Espressif's files change. They are third-party works, under their own
licences, not this repository's Apache 2.0 licence.

| File | What | Version | From | Licence |
|---|---|---|---|---|
| `Seeed_Studio_XIAO_ESP32C5_V1.1_schematic.pdf` | Schematic: block diagram, power (charger, power mux, buck, load switches, battery divider, header), main (chip, flash, crystal, LEDs, buttons, RF diplexer, U.FL) | V1.1, 2025-12-25 | [Seeed_Studio_XIAO_ESP32C5.pdf][sch] | CC BY-SA 4.0 (marked on every sheet) |
| `Seeed_Studio_XIAO_ESP32C5_V1.1_KiCad.zip` | KiCad 9 project: schematic sheets and PCB (footprint positions, board outline) | V1.1, 2025-12-25 (`Seeed Studio XIAO ESP32C5 V1.1_SCH&PCB_251225`) | [Seeed_Studio_XIAO_ESP32C5.zip][kicad] | CC BY-SA 4.0 (marked on every sheet) |
| `XIAO_ESP32C5_Pinout.xlsx` | Pinout sheet: the chip's pin table with the XIAO's pad functions (mostly in Chinese) | 2025-12-02 (file date) | [XIAO_ESP32C5_Pinout.xlsx][pinout] | None stated in the file. Seeed publishes it with the schematic and PCB above as part of the board's open-hardware resources |

All three are linked from Seeed's [XIAO ESP32-C5 getting started][gs]
page ("Resources"). They are copied here unchanged; file names are
Seeed's with the version added.

**Attribution**: © Seeed Studio. The schematic and KiCad project are
licensed under the [Creative Commons Attribution-ShareAlike 4.0
International licence][ccbysa]. Anything derived from them (for example, a
case designed around the PCB file's footprint positions) must be shared
under the same licence.

**Known slip in the pinout sheet**: its last column ("XIAO扩展IO引脚")
swaps D8 and D9. Its own XIAO-function column and the schematic agree
with Zephyr's `seeed_xiao_connector.dtsi`: D8 is GPIO8 (SCK), D9 is GPIO9
(MISO).

## Not included: the Espressif ESP32-C5 datasheet

The study also uses Espressif's *ESP32-C5 Series Datasheet*, pre-release
v0.5 (2025-05-19). It is not copied here. Its notice reads "Copyright ©
2025 Espressif Systems (Shanghai) Co., Ltd. All rights reserved" and
grants no licence, and this repository is public. Get it from Espressif:
[esp32-c5_datasheet_en.pdf][ds] (that address always serves the latest
version). The copy the study was checked against has SHA-256
`0ff4186c19eb9ea69e92f12db9c604cc6fce2a3c2a7492e48e1f13720772928c`.

## Checksums

```
d1e69e911d006a315a462e13347b2f7a821c067a003e8ee7fb522eff5f6bb01b  Seeed_Studio_XIAO_ESP32C5_V1.1_schematic.pdf
be192710dbc1cfcd26760feb4c02179a007b81623754e0e07a01cd7d55106872  Seeed_Studio_XIAO_ESP32C5_V1.1_KiCad.zip
3f1e0cbefc70dea85fb274074597a9bd2bc40aca6fc4e7ddcd79fc7ec2e04354  XIAO_ESP32C5_Pinout.xlsx
```

[sch]: https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.pdf
[kicad]: https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip
[pinout]: https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/XIAO_ESP32C5_Pinout.xlsx
[gs]: https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/
[ccbysa]: https://creativecommons.org/licenses/by-sa/4.0/
[ds]: https://www.espressif.com/documentation/esp32-c5_datasheet_en.pdf
