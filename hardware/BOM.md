# Bill of materials

Everything needed to build the battery-powered meter in its printed case.
See [README.md](README.md) for assembly and [wiring.svg](wiring.svg) for
the wiring.

## Electronics

| Qty | Part | Notes |
|---|---|---|
| 1 | Raspberry Pi Pico W, with headers ("Pico WH") | Or a Pico W with male headers soldered on, pins down |
| 1 | Pimoroni Pico Display Pack 2.0 | 320 × 240 IPS display, buttons A/B/X/Y, RGB LED; 55.2 × 34.6 mm |
| 1 | Pimoroni LiPo Amigo Pro | LiPo charger with power switch; USB-C in, JST-PH BAT and DEVICE sockets; 36 × 23 × 7 mm |
| 1 | LiPo battery, 3.7 V 350 mAh, JST-PH 2.0 mm plug, up to 32.0 × 20.5 × 7.3 mm | Pimoroni's 150 mAh pack (27 × 20.5 × 4.3 mm) also fits, with the caveats below |
| 1 | JST-PH 2.0 mm 2-pin lead, plug one end, bare wires the other, about 100 mm | From the Amigo Pro's DEVICE socket to the Pico |
| 1 | Schottky diode, 1 A, e.g. 1N5817 (or SS14, MBR120) | In the red (+) wire, cathode (stripe) towards the Pico |
| 3 cm | Heat-shrink tubing, 2–3 mm | Over the diode and the joints |

## Case

| Qty | Part | Notes |
|---|---|---|
| 1 | Front shell, `case/stl/front.stl` | PLA or PETG, 0.2 mm layers, 3 walls; print face down, no supports |
| 1 | Shelf, `case/stl/shelf.stl` | Print flat, posts up |
| 1 | Back cover, `case/stl/back.stl` | Print outside face down, no supports |
| 4 | Button caps, `case/stl/buttons.stl` (one print holds all four) | Print tab down; 0.1–0.15 mm layers print the small stems better |
| 4 | M2 × 8 mm self-tapping screws, pan head | Cover to shell |
| 1 | Double-sided foam tape, about 20 × 30 mm | Holds the charger in its pocket |

About 30 g of filament in all.

## Cables and tools

- A USB-C cable and charger: charges the battery, and powers the meter while
  plugged in.
- A micro-USB cable: flashing the firmware and the serial console (the
  Pico's own socket, reached with the case open).
- Soldering iron, solder, wire strippers, a heat gun or lighter for the
  heat-shrink.

## Battery notes

- **Charge rate.** The LiPo Amigo Pro charges at 200 mA as supplied: about
  0.6 C for a 350 mAh cell, well within what LiPo cells allow. Don't
  bridge the Amigo's FAST CHARGE pads (500 mA, 1.4 C). With the 150 mAh
  cell, 200 mA is 1.3 C, above the 1 C many small cells allow: check its
  datasheet first.
- **Running time.** The Pico W scanning Wi-Fi with the display lit draws
  roughly 80–100 mA, so 350 mAh lasts about 3½–4 hours (150 mAh about
  1½). The meter runs as long as you like on USB-C.
- **A different cell.** Set `BATTERY_L`, `BATTERY_W` and `BATTERY_T` at
  the top of `case/case.py` to its size and regenerate. The case grows to
  fit, and the script reports any clash.
- **Polarity.** JST-PH battery leads aren't all wired the same way. Check
  that the battery's red wire goes to the + pin of the Amigo's BAT socket
  before plugging it in.
