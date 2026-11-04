# XIAO ESP32-C5: bill of materials

The parts for the XIAO ESP32-C5 meter. See [README.md](README.md) for the
wiring. From the [port study](../../docs/port-esp32c5.md), section 7.

| Qty | Part | Notes |
|---|---|---|
| 1 | Seeed Studio XIAO ESP32-C5 | 21 × 17.8 mm. Seeed lists an FPC antenna as included, but it isn't always in the box: check, or buy the antenna below |
| 1 | **Antenna**: dual-band 2.4 GHz + 5 GHz (2.4–2.5 and 5.15–5.85 GHz), 50 Ω, **U.FL / IPEX-1 / MHF1** plug (not MHF4), lead about 50–150 mm | **Required**: the board's one U.FL socket carries both bands. A stick-on FPC antenna fits inside a case (mount it flat, away from the TFT's metal frame and the battery). Or Seeed's 2.4G/5G RP-SMA antenna with a U.FL-to-RP-SMA bulkhead lead, outside the case (both RP-SMA, or both SMA). A 2.4 GHz-only antenna would cripple the 5 GHz results |
| 1 | 2.2" SPI TFT module, ILI9341, 240 × 320, 3.3 V supply | The module pi-zephyr-inet-radio used. Measure its LED pin's current |
| 4 | Tactile push switches, 6 × 6 mm (or 12 × 12 mm with caps) | Press to make |
| 1 | RGB LED, 5 mm, common anode (optional) | Or a 3.3 V-friendly SMD RGB LED on a carrier |
| 3 | Resistors for the LED: 330 Ω (red), 680 Ω (green, blue) | Dimmed further in software (2%) |
| 1 | 10 kΩ resistor | Pull-up on the TFT's CS (GPIO7, a strapping pin) |
| 1 | N-MOSFET 2N7000 / BSS138, or an NPN and a 1 kΩ base resistor (optional) | Backlight switch, only if the module's LED pin draws tens of mA (most switch it themselves) |
| 1 | LiPo cell, 3.7 V, protected, 350–500 mAh | Soldered to the XIAO's BAT+ / BAT− pads |
| 1 | Slide switch (optional) | In the battery lead, for a hard off |
| — | Hook-up wire, perfboard for the buttons, headers | |
