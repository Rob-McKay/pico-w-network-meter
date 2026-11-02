# Case, wiring and parts

A battery-powered build of the meter in a 3D-printed case:

- [BOM.md](BOM.md): the parts to buy and print, with notes on the battery.
- [wiring.svg](wiring.svg): how the battery, charger and Pico connect.
- [case/case.py](case/case.py): generates the case as STL files, in
  [case/stl/](case/stl/).

![Wiring](wiring.svg)

## The case

72.6 × 69.9 × 20.5 mm, in two halves screwed together, with four printed
button caps. The Display Pack and Pico fill the top. The charger and
battery sit in a compartment below the display.

![Sections through the assembled case](case/stl/sections.png)

- **Front shell** (`front.stl`): the bezel, with a window onto the
  display's active area, holes for the button caps and the RGB LED, and a
  frame that locates the Display Pack. The left wall has openings for the
  Pico's micro-USB socket and the charger's USB-C socket. There are four
  corner bosses for the screws, and two ribs that hold the battery down.
- **Back cover** (`back.stl`): pockets for the charger and battery. Four
  posts press the Pico into the Display Pack's sockets, with pins in its
  mounting holes. It also has a window onto the charger's power switch, a
  pin hole over the Pico's BOOTSEL button, and a lip that lines it up with
  the shell.
- **Button caps** (`buttons.stl`): each one has a stem through the bezel
  and a tab behind it, which rests on the button and stops the cap
  falling out.

### Check the measurements first

`case.py` takes the Display Pack's layout from Pimoroni's dimension
drawing: the board, the buttons, the LED, the glass and the active area.
The other parts' sizes are the published ones. A few things are on
neither, and are marked **MEASURE** in the script. Check these with
calipers, change the constants at the top of `case.py`, and regenerate
before printing:

| Constant | What | Default |
|---|---|---|
| `PACK_DISPLAY_H` | Front of the Display Pack's board to the front of the glass | 3.0 mm |
| `PACK_SOCKET_H` | Height of the header sockets on its back | 7.0 mm |
| `BUTTON_TOP_H` | Height of a button's top above the board | 1.5 mm |
| `PICO_USB_LEFT` | Whether the Pico's USB socket is at the A/B end (left, looking at the display) or the X/Y end. This decides which wall gets its opening | A/B end |
| `PICO_X`, `PICO_Y` | Where the Pico sits on the Display Pack's back | 2.5, 7.0 mm |
| `PICO_BOOTSEL` | The BOOTSEL button on the Pico | 12.0, 13.0 mm |
| `AMIGO_SWITCH` | The charger's switch and LEDs, from its USB-C end | 24.0, 11.5 mm |

Every run checks that the parts inside (modelled as blocks) clear the case,
and prints any clash.

### Generating the files

```sh
pip install numpy manifold3d matplotlib
python3 hardware/case/case.py --preview
```

This writes `front.stl`, `back.stl` and `buttons.stl` to
`hardware/case/stl/`. With `--preview` it also writes a picture of each
part and `sections.png`, which cuts through the assembled case with the
parts inside in colour. `--out DIR` writes them somewhere else.

### Printing

PLA or PETG, 0.2 mm layers, 3 perimeters, 15–20% infill. No supports are
needed:

- **Front shell:** face down. The window and holes print as bridges.
- **Back cover:** outside face down.
- **Button caps:** tab down. Finer layers (0.1–0.15 mm) give cleaner stems.
  Clean any stringing off the stems, so they slide freely in the bezel.

## Assembly

1. **Wire the power lead** (see [wiring.svg](wiring.svg)). Cut the JST-PH
   lead to about 80 mm.
   - Solder the Schottky diode into the red wire, its stripe (cathode)
     towards the Pico end.
   - Solder the red wire to the top of the Pico's **pin 39 (VSYS)**, where
     the pin comes through the board, and the black wire to **pin 38
     (GND)**.
   - Cover the diode and joints with heat-shrink.
   - Leave pin 40 (VBUS) alone.
2. **Plug the Pico into the Display Pack** the way the Display Pack's
   markings show. It works only one way round. Check that its USB socket
   is at the end `PICO_USB_LEFT` says before printing.
3. **Drop the button caps into their holes** in the front shell, from the
   inside, tabs along the edge.
4. **Lay the Display Pack face down in the shell's frame**, its glass
   against the bezel, with buttons A and B at the left-hand end (the end
   with the charger's USB-C opening).
5. **Fit the charger and battery.** Stick the charger into its pocket on
   the back cover with foam tape, USB-C to the open end, components up. Put
   the battery in its pocket.
   - Check the battery's polarity, then plug it into the charger's
     **BAT** socket.
   - Plug the Pico's lead into **DEVICE**.
6. **Close the case.** Turn the shell over onto the cover, so the lip goes
   inside the walls and the posts find the Pico's mounting holes. Fit the
   four M2 screws.
7. **Switch on** with the charger's switch, through the window in the back.

The charger's USB-C socket charges the battery, and powers the meter while
a cable is plugged in. The Pico's micro-USB socket is for flashing and the
serial console.

To flash new firmware, hold the Pico's BOOTSEL button through the pin hole
in the back while plugging in its micro-USB cable. A straightened paper
clip does the job.
