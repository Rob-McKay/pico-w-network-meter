# Target: Seeed XIAO ESP32-C5 with a 2.2" ILI9341 TFT

The meter's second target. Build it with `make xiao`; the
[README](../../README.md) covers the build and what the meter shows, and
[USAGE.md](../USAGE.md) how to use it. **First runs on a breadboard**
(November 2026, list below): it scans, shows the screens and takes button
presses on all four buttons. What hasn't been checked yet is marked so; the rest comes from
the build, the board's documents and the [port study](../port-esp32c5.md).

## Hardware

- Seeed Studio XIAO ESP32-C5: Espressif's ESP32-C5 (a RISC-V core with
  dual-band 2.4 + 5 GHz Wi-Fi 6), 8 MB flash, a LiPo charger, and one
  U.FL antenna socket. **It needs an antenna** on that socket.
- A 2.2" ILI9341 SPI TFT module, 240x320, on 3.3 V.
- Four push buttons to ground, and a common-anode RGB LED (optional).

Wiring, power and parts: [hardware/xiao-esp32c5/](../../hardware/xiao-esp32c5/README.md).
In short:

| Function | XIAO pad (GPIO) |
|---|---|
| TFT SCK / MOSI / CS / DC / RESET / backlight | D8 (8) / D10 (10) / D3 (7) / D2 (25) / D6 (11) / D9 (9) |
| Buttons A / B / X / Y | D0 (1) / D1 (0) / D4 (23) / D5 (24) |
| LED red / green / blue | D7 (12) / MTCK (4) / MTDO (5), back pads for green and blue |

The devicetree overlay and the board's settings are
`app/boards/xiao_esp32c5_esp32c5_hpcore.overlay` and `.conf`. If the
picture is mirrored or upside down, change the panel's `rotation` or
`v-mirror` there.

## Build

```sh
make xiao                  # out/xiao/network-meter.bin, network-meter-merged.bin
make xiao WIFI_COUNTRY=US  # another regulatory domain
```

`WIFI_COUNTRY` is an ISO 3166 alpha-2 code (default `GB`; `00` is the
world-safe set). The meter asks the driver for it at start-up. The
firmware is built with the Zephyr SDK's GCC, the one exception to the
project's clang: Espressif's SoC support doesn't build with clang.

## Flashing

Plug the XIAO in by USB-C and, from the host (Docker on a Mac can't reach
USB; `pip install esptool`), run

```sh
esptool --chip esp32c5 write-flash 0x2000 out/xiao/network-meter.bin
```

esptool 5 needs Python 3.10 or later. With macOS's own Python 3.9, use
esptool 4 (4.9 or later knows the C5), which spells the command with an
underscore and has no `esptool` command of its own:

```sh
python3 -m pip install --upgrade "esptool<5"
python3 -m esptool --chip esp32c5 write_flash 0x2000 out/xiao/network-meter.bin
```

or flash `out/xiao/network-meter-merged.bin` at offset 0 with a browser
flasher. If esptool can't connect, hold **BOOT** while plugging the board
in. On Linux, `west flash` also works from inside Docker with
`--device /dev/ttyACM0`.

The event log is at the end of the flash (`storage_partition`, 64 KB),
which flashing leaves alone, so it survives updates. `esptool
erase-flash` wipes it.

## Serial console

The console is the C5's own USB Serial/JTAG port, on the same USB-C
socket: `/dev/cu.usbmodem<N>` on a Mac, `/dev/ttyACM<N>` on Linux. Open it
with `screen /dev/cu.usbmodem<N> 115200` (quit with Ctrl-A then K).

## Battery

The XIAO charges a LiPo soldered to its BAT pads, at 200 mA, and runs from
it when USB is unplugged: no separate charger. Expect roughly 3–5 hours
from 500 mAh (an estimate from the datasheet; not measured). Details in
[hardware/xiao-esp32c5/](../../hardware/xiao-esp32c5/README.md#power-and-battery).
There's no case for it yet.

## Known issues and limits

- **Only partly checked on hardware**: see the list below.
- **5 GHz.** The radio scans both bands, and the meter keeps the 5 GHz
  networks (`CONFIG_METER_BAND_5G`, on in the board's `.conf`): a
  Channels 5 GHz screen after the 2.4 GHz one, and the Networks list and
  the LED follow the band of the last Channels screen viewed (USAGE). The
  console lists each 5 GHz channel a network reaches and the quietest
  80 MHz block ("5 GHz: use 52-64 (80 MHz): clear, nothing heard").
  Channels 165-177 aren't charted, though their networks are listed.
- **5 GHz widths are assumed.** Zephyr's scan results don't give a
  network's channel width, so every 5 GHz network is taken to use its
  whole 80 MHz block, as nearly every home router does. A 20 or 40 MHz
  network is counted on channels it doesn't use. The recommendation
  considers 36-48, 52-64, 100-112 and 116-128; 132-144 and 149-161 only
  with `CONFIG_METER_RECOMMEND_5G_132` and `_UPPER` (allowed in the US,
  not at 80 MHz or full power in the UK).
- **Driver patch.** The ESP32 Wi-Fi driver needs a carried Zephyr patch
  (`esp32-wifi-scan-params.patch`) to report hidden networks and to honour
  the scan's 130 ms dwell time per channel; without it a sweep of both
  bands takes about 14 s.
- **Reset causes.** The meter records the chip's reset cause, so the
  events screen can say "HW watchdog", "brown-out" or "CPU fault" (see
  USAGE). The XIAO's own reset button resets the whole chip, which shows
  as a power-on, not "reset button" (checked on hardware).
- **The restart note.** Why the meter restarted itself is kept in RAM
  that start-up doesn't clear. A reset through the chip's enable pin (the
  reset button) loses it, as a power cut does. Whether it survives the
  meter's own software restarts is still to be checked: if it doesn't,
  those will show as power-ons too.
- **SPI without DMA.** The TFT's SPI bus runs without DMA: on the C5,
  `dma-enabled` also needs `dmas` channels and the GDMA controller
  enabled, and without them the panel never started (the first run's
  black screen). CPU-driven transfers are fast enough at 10 MHz.
- **Strapping pins.** The TFT's CS and DC are on strapping pins (GPIO7,
  GPIO25). With the chip's factory eFuses neither affects booting; the
  10 kΩ pull-up on CS keeps the panel deselected while it boots.

## Checked on hardware

On the breadboard ([hardware/xiao-esp32c5/](../../hardware/xiao-esp32c5/README.md#on-a-breadboard)),
with esptool 4 on macOS:

| Check | Result |
|---|---|
| Flashing over USB-C | Works (`write_flash 0x2000`) |
| Console with a host attached | Works: the scans' figures, no errors |
| The TFT | Shows the screens as expected, once SPI DMA was dropped (above) |
| Scans | Every scan succeeds; one every 15–16 s, so a sweep of both bands takes 5–6 s with the patched 130 ms dwell. Channel 13 is heard (country GB), and 4–10 networks a scan count as "other" (5 GHz) |
| Hidden networks | Shown |
| Buttons | All four work (tried with a wire link to ground): A and B change screens, X scans now on the Channels screen, X and Y page up and down the Networks list |
| 5 GHz on the console | Works: "21 results from the driver, 21 networks kept", "13 networks on 2.4 GHz, 8 on 5 GHz, 0 other" (before, the 8 counted as "other"), the 5 GHz channels reached and the recommended 80 MHz block, with the 4 KB log buffer dropping nothing |
| Channels 5 GHz screen | Shown after Channels (A). The six blocks are labelled 36, 52, 100, 116, 132, 149, by their first channel, and the network count sits over the column of its channel ("2" over 40, the block's second column) |
| Networks 5 GHz | A from Channels 5 GHz lists the 5 GHz networks |
| Event log | Works: the first start shows as "#1 power on", so the log in flash (`storage_partition`, 64 KB) opens and records |
| Power cut, reset button | Both show as "power on". The reset button resets the whole chip through its enable pin: the chip reports a power-on, and the restart note in RAM doesn't survive it, so the meter can't tell it from a power cut |

Not checked yet:

1. The RGB LED.
2. The console with no host attached (it must not block on a charger or a
   battery).
3. The meter's own restarts: whether the restart note survives a
   software restart (a Wi-Fi restart, a crash, a hung thread), so the
   events screen says why, and that the log keeps its starts across a
   reflash.
4. Running for hours: no watchdog restarts or driver failures.
5. A faster SPI clock (20 MHz with short wires).
6. Current draw and battery life.
