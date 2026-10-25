# Using the network meter

The meter listens for 2.4 GHz Wi-Fi networks and shows how crowded each
channel is, so you can pick the best channel for your own router. A
printable one-page version of this guide is in
[quick-reference.pdf](quick-reference.pdf).

## Starting up

Plug the Pico W into any USB power supply. The screen shows
**Scanning...** and the LED turns blue. The first results appear a few
seconds later. After that the meter scans again every 10 seconds on its
own.

## Buttons

The four buttons are beside the screen, two on each side.

| Button | What it does |
|---|---|
| **A** | Next screen |
| **B** | Previous screen |
| **X** | Scan now, without waiting for the next scan |
| **Y** | Not used |

## Screens

### Channels

![Channels screen](screens/channels.png)

- **One bar per channel**, 1 to 13. The taller the bar, the more Wi-Fi
  signal reaches that channel. That includes networks on the channels
  either side, because a 2.4 GHz network spills over its neighbours up to
  3 channels away.
- **Bar colour** shows how busy the channel is:
  green = clear, amber = moderate, red = busy.
- **The number above a bar** is how many networks use that channel.
- **The highlighted channel number** is the recommended channel: the
  quietest of 1, 6 and 11. Those three are the only channels that don't
  overlap each other, so routers should use one of them.
- **Footer:** "Use ch 6: clear" names the recommended channel and how busy
  it is. "Nothing heard" means no networks are showing. The scan count is
  on the right.
- **Header:** how many networks are showing.

### Networks

![Networks screen](screens/networks.png)

The strongest networks, one per row:

- **Name.** Shown in amber if the network is open (no password), white
  if it is secured.
- **Channel.**
- **Signal strength** in dBm. Closer to 0 is stronger: −40 is very strong,
  −90 is barely there.
- **Signal bar:** green = good (−60 dBm or better), amber = fair (down to
  −75 dBm), red = weak.

The footer gives how many networks are showing, and how many results were
ignored ("other"), such as hidden networks.

## Fading

![Faded screen](screens/faded.png)

A network stays on screen after it was last heard, so one scan that misses
it doesn't make it vanish:

- For **15 seconds** after it was last heard it is shown at full
  brightness.
- It then **fades**, and has faded to **black at 30 seconds**, when it is
  removed.
- If it is heard again, it is back at full brightness straight away.

So dim bars and rows are old data. If the whole screen fades, the meter has
heard nothing for a while.

## LED

The RGB LED is on the right of the screen.

| LED | Meaning |
|---|---|
| **Blue** | Scanning |
| **Green** | The recommended channel is clear |
| **Amber** | The recommended channel is moderately busy |
| **Red** | Even the recommended channel is busy |
| **Magenta** | The last scan failed; it is retried after 2 seconds |
| Off | Starting up |

## Tips

- **Choosing a channel:** set your router to the channel the meter
  recommends. Check it from where you actually use Wi-Fi, not right next
  to the router.
- **Walk around:** the readings change from room to room. Press **X** for
  an immediate scan after moving.
- **Only 2.4 GHz:** the Pico W's radio cannot hear 5 GHz networks.
- **Keep away from USB 3 hubs:** they give off noise across the 2.4 GHz
  band that drowns out all but the strongest networks. Use a long cable or
  a USB charger, away from computers and hubs. If only your own router
  shows up, this is the likely cause.
- **Serial console:** the Pico's USB socket is also a serial port. On a Mac
  it appears as `/dev/cu.usbmodem<N>` (`ls /dev/cu.usbmodem*` finds it):
  open it with `screen /dev/cu.usbmodem<N> 115200`, and quit with Ctrl-A
  then K. On Linux it is `/dev/ttyACM<N>`. Each scan's per-channel figures
  are printed there. Logging starts when the port is opened, so the first
  moments after power-up are not shown.

## Settings

Change these when building the firmware: `make pico` with
`WIFI_COUNTRY=...`, or `-DCONFIG_...=` options passed to
`scripts/build.sh` (see the README).

| Setting | Default | |
|---|---|---|
| `WIFI_COUNTRY` | `UNITED_KINGDOM` | Which channels may be scanned (the US stops at 11) |
| `CONFIG_METER_SCAN_INTERVAL_S` | 10 | Seconds between scans |
| `CONFIG_METER_FADE_START_S` | 15 | Seconds at full brightness |
| `CONFIG_METER_FADE_END_S` | 30 | Seconds until faded out |
| `CONFIG_METER_LED_BRIGHTNESS` | 5 | LED brightness, percent |
