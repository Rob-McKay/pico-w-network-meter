# Using the network meter

The meter listens for 2.4 GHz Wi-Fi networks and shows how crowded each
channel is, so you can pick the best channel for your own router. A
printable one-page version of this guide is in
[quick-reference.pdf](quick-reference.pdf).

It works the same on both of the meter's targets, the Raspberry Pi Pico W
with a Pico Display Pack 2.0 and the Seeed XIAO ESP32-C5 with a 2.2" TFT.
What differs between them, such as flashing and the battery, is in
[Pico W and XIAO ESP32-C5](#pico-w-and-xiao-esp32-c5) below.

## Starting up

Plug the meter into any USB power supply, or switch on its battery. The
screen shows **Scanning...** and the LED turns blue. The first results
appear a few seconds later. After that the meter scans again every 10
seconds on its own.

## Buttons

There are four buttons, A, B, X and Y. On the Pico W's Display Pack they
are beside the screen, A and B on the left, X and Y on the right.

| Button | What it does |
|---|---|
| **A** | Next screen |
| **B** | Previous screen |
| **X** | Channels screen: scan now, without waiting for the next scan. Networks and Events screens: previous page |
| **Y** | Networks and Events screens: next page |
| **X** and **Y** together | Events screen: print the whole log on the serial console |

## Screens

### Channels

![Channels screen](screens/channels.png)

- **Two bars per channel**, 1 to 13, one inside the other:
  - **Bright, narrow bar: signal.** The strongest network on that
    channel. No bright bar means no network uses the channel.
  - **Dim, wide bar: interference.** Every other network the channel
    hears: other networks on the same channel, and those on nearby
    channels spilling over (a 2.4 GHz network overlaps its neighbours up
    to 3 channels away). A channel with one network and nothing nearby
    has no dim bar.
- **The scale** on the left is in dBm, the signal reaching the channel,
  with a grid line every 10 dB; the labelled lines are brighter.
- **Bar colour** shows how busy the channel is, counting both:
  green = clear, amber = moderate, red = busy.
- **The number above a bar** is how many networks use that channel.
- **Bold channel numbers** are the standard channels 1, 6 and 11. Those
  three are the only channels that don't overlap each other, so routers
  should use one of them.
- **The highlighted channel number** is the recommended channel: the
  quietest of 1, 6 and 11.
- **Footer:** "Use ch 6: clear" names the recommended channel and how busy
  it is. "Nothing heard" means no networks are showing. The scan count is
  on the right.
- **Header:** how many networks are showing.

### Networks

![Networks screen](screens/networks.png)

The strongest networks, one per row:

- **Name.** Shown in amber if the network is open (no password), white
  if it is secured. A hidden network, which doesn't broadcast its name, is
  shown in grey as "hidden" and its MAC address (BSSID), such as
  `hidden 02:1f:3c:a2:7f:06`, which tells its access points apart. The
  first three bytes identify the maker: look them up in the IEEE's
  register of MAC prefixes (OUI).
  Names up to 24 characters, such as `JurassicFibre_UID35630`, are shown
  whole; longer ones are shortened in the middle with `..`, keeping the
  start and the end, which often tells similar names apart.
- **Channel.**
- **Signal strength** in dBm. Closer to 0 is stronger: −40 is very strong,
  −90 is barely there. Its colour: green = good (−60 dBm or better),
  amber = fair (down to −75 dBm), red = weak.

The screen has room for 9 networks. When more are heard, **Y** pages down
the list and **X** back up, and the footer shows which are on screen,
such as "10-13 of 13".

![Second page of the networks screen](screens/networks-page2.png)

The footer gives how many networks are showing ("heard"), and how many
results were ignored ("other"): those not on a 2.4 GHz channel (on the
XIAO, its 5 GHz networks, for now), or past the 64 the meter keeps. Each access point counts once, so a router with a
guest network counts twice.

### Events

![Events screen](screens/events.png)

Each time the meter started, newest first, numbered, and why. Where it
restarted itself, how long it had been running before:

| Event | Colour | Meaning |
|---|---|---|
| **power on** | white | Power was applied, or came back after a cut |
| **Wi-Fi restart after …** | amber | The Wi-Fi driver stopped working, so the meter restarted itself (after 5 failed scans in a row) |
| **crash N after …** | red | A fatal error (number N): the meter restarted itself |
| **scan hung / main hung after …** | red | A part of the meter stopped responding, and the watchdog restarted it |
| **restart after …** | red | Restarted with no reason recorded: the reset button, a debugger, or the hardware watchdog |

On the XIAO ESP32-C5, which records the chip's own reset cause, a restart
with no reason recorded says what the chip saw instead, where it tells
more than "restart":

| Event | Colour | Meaning |
|---|---|---|
| **brown-out** | white | The supply dropped too low (a flat battery or a weak USB supply), then came back: shown in place of "power on" |
| **HW watchdog after …** | red | The chip's hardware watchdog restarted it: the whole system stopped, not just one part of the meter |
| **CPU fault after …** | red | The processor faulted in a way the meter couldn't record as a crash |
| **reset button after …** | red | The chip's reset pin. The XIAO's own reset button resets the whole chip, which it reports as a power-on instead |

On the console's full listing (below), such a start ends with `hw` and the
cause's number: 1 power-on, 2 reset pin, 3 software, 4 watchdog,
5 brown-out, 6 CPU fault, 7 wake-up, 8 other.

The log is kept in flash, so it survives power cuts and firmware updates.
The screen shows the newest 64 (9 to a page: **Y** next page, **X**
previous). The serial console prints the newest 10 when the meter starts.

To see the whole log with every detail, open the serial console (see
[Tips](#tips)) and press **X** and **Y** together on the Events screen. It
prints every start kept in flash, oldest first, such as:

```
#13 Wi-Fi restart after 2h31m: cause 2, ran 9060 s, detail -11, arg 0x00000005
```

where *detail* is the error (Wi-Fi), the reason (crash) or which part hung
(watchdog: 0 main, 1 scan), and *arg* is how many scans failed in a row
(Wi-Fi) or the program address of the crash. The footer says when it has
been sent. Press X first and it pages up a moment before; that does no
harm.

## Fading

![Faded screen](screens/faded.png)

A network stays on screen after it was last heard, so one scan that misses
it doesn't make it vanish:

- For **60 seconds** after it was last heard it is shown at full
  brightness. Weak and hidden networks are often missed by a scan or two,
  so this keeps them on screen.
- It then **fades**, down to 40% brightness so it stays readable, and at
  **2 minutes** it is removed.
- If it is heard again, it is back at full brightness straight away.

So dim bars and rows are old data. If the whole screen fades, the meter has
heard nothing for a while.

## LED

The RGB LED is to the right of the screen on the Pico W's Display Pack.

| LED | Meaning |
|---|---|
| **Blue** | Scanning |
| **Green** | The recommended channel is clear |
| **Amber** | The recommended channel is moderately busy |
| **Red** | Even the recommended channel is busy |
| **Magenta** | The last scan failed; it is retried after 2 seconds. After 5 failures in a row the meter restarts itself |
| Off | Starting up |

## Tips

- **Choosing a channel:** set your router to the channel the meter
  recommends. Check it from where you actually use Wi-Fi, not right next
  to the router.
- **Walk around:** the readings change from room to room. Press **X** on
  the Channels screen for an immediate scan after moving.
- **Only 2.4 GHz:** the Pico W's radio cannot hear 5 GHz networks. The
  XIAO's can, but the meter shows only 2.4 GHz for now.
- **Keep away from USB 3 hubs:** they give off noise across the 2.4 GHz
  band that drowns out all but the strongest networks. Use a long cable or
  a USB charger, away from computers and hubs. If only your own router
  shows up, this is the likely cause.
- **Serial console:** the board's USB socket is also a serial port. On a
  Mac it appears as `/dev/cu.usbmodem<N>` (`ls /dev/cu.usbmodem*` finds
  it): open it with `screen /dev/cu.usbmodem<N> 115200`, and quit with
  Ctrl-A then K. On Linux it is `/dev/ttyACM<N>`. Each scan's per-channel
  figures are printed there. On the Pico W, logging starts when the port
  is opened, so the first moments after power-up are not shown.

## Settings

Change these when building the firmware: `make pico` or `make xiao` with
`WIFI_COUNTRY=...`, or `-DCONFIG_...=` options passed to
`scripts/build.sh` (see the README).

| Setting | Default | |
|---|---|---|
| `WIFI_COUNTRY` | `UNITED_KINGDOM` (Pico W), `GB` (XIAO) | Which channels may be scanned (the US stops at 11): a WHD country name on the Pico W, an ISO 3166 code on the XIAO |
| `CONFIG_METER_SCAN_INTERVAL_S` | 10 | Seconds between scans |
| `CONFIG_METER_RECOVER_AFTER` | 5 | Failed scans in a row before the meter restarts itself (0: never) |
| `CONFIG_METER_FADE_START_S` | 60 | Seconds at full brightness |
| `CONFIG_METER_FADE_END_S` | 120 | Seconds until a network is removed |
| `CONFIG_METER_FADE_MIN_PERCENT` | 40 | Brightness a network fades to before it is removed (0: fade to black) |
| `CONFIG_METER_LED_BRIGHTNESS` | 2 | LED brightness, percent (1 is the dimmest) |

## Pico W and XIAO ESP32-C5

The screens, buttons and LED are the same on both. These differ; each
target's guide ([Pico W](targets/pico.md),
[XIAO ESP32-C5](targets/xiao-esp32c5.md)) has the details.

| | Pico W with Display Pack 2.0 | XIAO ESP32-C5 with 2.2" TFT |
|---|---|---|
| **Flashing** | Hold BOOTSEL while plugging it in, copy `network-meter.uf2` to the `RPI-RP2` drive | `esptool --chip esp32c5 write-flash 0x2000 network-meter.bin` over USB-C (hold BOOT while plugging in if it won't connect), or the merged image with a browser flasher |
| **Battery** | A separate charger (LiPo Amigo Pro) in the printed case; its USB-C socket charges and powers the meter | The XIAO charges a LiPo on its BAT pads from its own USB-C socket, and runs from it when unplugged |
| **Console** | The Pico's USB socket (inside the case) | The XIAO's USB-C socket |
| **Bands** | 2.4 GHz only | Hears 2.4 and 5 GHz; shows 2.4 GHz for now |
| **Unexplained restarts** | All shown as "restart after …" | Named where the chip says why: "HW watchdog", "brown-out", "CPU fault" |
| **Tested on hardware** | Yes | On a breadboard: scans, screens, buttons A and B ([details](targets/xiao-esp32c5.md#checked-on-hardware)) |
