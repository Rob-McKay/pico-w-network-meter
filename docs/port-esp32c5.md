# Porting the meter to the Seeed XIAO ESP32-C5: a study

What it would take to run the network meter on new hardware:

- a **Seeed Studio XIAO ESP32-C5** (Espressif ESP32-C5: RISC-V, dual-band
  2.4 GHz + 5 GHz Wi-Fi 6) in place of the Raspberry Pi Pico W;
- a **2.2" SPI TFT module** with an **ILI9341** controller, 240×320 pixels,
  in place of the Pimoroni Pico Display Pack 2.0's ST7789V panel;
- **four external push buttons** (press to make) in place of A/B/X/Y, and
  optionally an **RGB LED**;
- ideally scanning **both 2.4 GHz and 5 GHz**.

This is a research and planning document. No application code was changed.
The branch (`esp32c5-port-study`) adds only this file. Section 9 lists what
was **verified** here (by building in Docker) and what is **inferred** from
documentation. Nothing could be run on real hardware.

**Summary.**

- **Upstream support.** Zephyr already supports the board:
  `xiao_esp32c5/esp32c5/hpcore` is in Zephyr v4.5.0 and in the commit
  `west.yml` pins. No out-of-tree board is needed.
- **The meter builds for it, nearly unchanged.** With a devicetree overlay
  (appendix A) and a copy of `prj.conf` without the one WHD line, the
  unchanged `app/` source compiles and links for the board with no
  warnings under `-Werror`.
- **Two things block "unchanged".** clang-tidy can't run on the RISC-V
  build yet, and the ESP32-C5 needs the Zephyr SDK's **GCC**: the project
  is clang-only today.
- **Wi-Fi works through the same Zephyr API.** The scan results carry band,
  channel, RSSI and BSSID for 5 GHz networks.
- **The driver needs a small patch**, like the one already carried for
  AIROC. Without it, hidden networks are never reported and the scan
  parameters are ignored.
- **The 5 GHz work is mostly in the application.** The data model, the
  overlap rules, a second chart and the recommendation all need changes.
  Estimate: about 2–3 weeks of focused work for a dual-band meter, of
  which the board, display, buttons and a 2.4 GHz scan are 3–5 days.

## Contents

1. [Zephyr support for the board and SoC](#1-zephyr-support-for-the-board-and-soc)
2. [Wi-Fi](#2-wi-fi)
3. [Display](#3-display)
4. [Buttons and LED](#4-buttons-and-led)
5. [Application changes](#5-application-changes)
6. [Build system](#6-build-system)
7. [Hardware: wiring, BOM, power, case](#7-hardware-wiring-bom-power-case)
8. [Risks, unknowns, effort and plan](#8-risks-unknowns-effort-and-plan)
9. [What was verified, and how](#9-what-was-verified-and-how)
10. [Sources](#10-sources)
- [Appendix A: trial devicetree overlay](#appendix-a-trial-devicetree-overlay)
- [Appendix B: draft Wi-Fi driver patch](#appendix-b-draft-wi-fi-driver-patch)

---

## 1. Zephyr support for the board and SoC

### The board is upstream

Zephyr has a board for this exact module: `boards/seeed/xiao_esp32c5`
([source][z-board], [docs][z-board-doc]).

| Zephyr | `xiao_esp32c5` | ESP32-C5 SoC |
|---|---|---|
| v4.4.0 | no | yes (`soc/espressif/esp32c5`, `esp32c5_devkitc` board) |
| `a915b2f` (v4.5.0-rc2, this project's pin) | **yes** | yes |
| v4.5.0 | yes (identical to the pin) | yes |
| main (`e7b400b`, 3 Nov 2026) | yes (devicetree property order only, since v4.5.0) | yes |

The Wi-Fi driver (`drivers/wifi/esp32/src/esp_wifi_drv.c`) is identical at
the pin and on main. So the project's current Zephyr pin is good enough,
and the port needs **no out-of-tree board definition**: only an
application overlay for the TFT, buttons and LED, as the Pico build has
today (`app/boards/rpi_pico_rp2040_w.overlay`).

The board has two targets: `xiao_esp32c5/esp32c5/hpcore` (the main RISC-V
core, which runs the meter) and `xiao_esp32c5/esp32c5/lpcore` (the
low-power core, not needed). Its devicetree
(`xiao_esp32c5_hpcore.dts`):

- 8 MB flash and 8 MB PSRAM (Zephyr's board docs say an ESP32-C5-WROOM-1
  module; Seeed's wiki says only "8 MB PSRAM & 8 MB Flash");
- console and shell on `usb_serial`, the SoC's built-in **USB Serial/JTAG**
  controller on the USB-C socket (no USB stack in Zephyr, no CDC-ACM);
- user LED on GPIO27, active low, as `led0` (`gpio-leds`);
- the BOOT button on GPIO28 as a `gpio-keys` node (`INPUT_KEY_0`);
- `wdt0` enabled, `wifi` enabled, `spi2`, `i2c0`, `uart0`, `adc0` enabled
  on the XIAO's standard pins;
- the flash partition table `partitions_0x2000_default_8M.dtsi`, which
  already has a 192 KB `storage_partition` at 0x7b0000 (section 5.7).

The SoC support uses **hal_espressif**, which the pinned Zephyr manifest
lists at `b16851b`. It carries the ESP32-C5 HAL (its `zephyr/esp32c5`
directory and `components/*/esp32c5`) and lists the C5's Wi-Fi, PHY,
coexistence and BLE libraries as west blobs (`lib/esp32c5/libcore.a`,
`libnet80211.a`, `libpp.a`, `libphy.a`, `libcoexist.a`, `libmesh.a`,
`libespnow.a`, `libble_app.a`, `libbtbb.a`).

**Verified:** `samples/hello_world` and the meter itself build for
`xiao_esp32c5/esp32c5/hpcore` at the project's pin in Docker (section 9).

### Pin-out

The XIAO ESP32-C5's pads, from Seeed's wiki ([getting started][seeed-gs],
source [in Seeed's wiki repository][seeed-gs-src]) and Zephyr's
`seeed_xiao_connector.dtsi`, which agree:

| Pad | GPIO | Board default | Notes |
|---|---|---|---|
| D0 | 1 | ADC1_CH0 | LP GPIO |
| D1 | 0 | | LP GPIO |
| D2 | 25 | | **strapping pin** |
| D3 | 7 | | **strapping pin**; SPI2 IO_MUX MOSI (FSPID) |
| D4 | 23 | I2C0 SDA | |
| D5 | 24 | I2C0 SCL | |
| D6 | 11 | UART0 TX | |
| D7 | 12 | UART0 RX | |
| D8 | 8 | SPI2 SCK | |
| D9 | 9 | SPI2 MISO | |
| D10 | 10 | SPI2 MOSI | SPI2 IO_MUX CS0 (FSPICS0) |
| MTMS (back pad) | 2 | JTAG | **strapping pin** |
| MTDI (back pad) | 3 | JTAG | **strapping pin** |
| MTCK (back pad) | 4 | JTAG | usable as GPIO (USB-JTAG is on GPIO13/14) |
| MTDO (back pad) | 5 | JTAG | usable as GPIO |
| (internal) | 6 | battery voltage (ADC_BAT) | SPI2 IO_MUX SCK (FSPICLK): not on a pad |
| (internal) | 26 | enables the battery divider (ADC_CTRL) | strapping pin |
| (internal) | 27 | user LED, yellow, active low | strapping pin |
| (internal) | 28 | BOOT button | strapping pin (download mode) |
| (internal) | 13, 14 | USB Serial/JTAG D−/D+ | |
| (internal) | 16–22 | flash and PSRAM | |

**Strapping pins.** From the ESP32-C5 datasheet ([datasheet][esp-ds],
pre-release v0.5, chapter 3 *Boot Configurations*, Tables 3-1 to 3-8). The
chip latches these at reset and holds them until power-down. Each must be
stable for 3 ms after CHIP_PU rises (Table 3-2). After that they are
ordinary GPIOs.

| Strapping pin | Internal pull at reset | Selects | Matters for the meter? |
|---|---|---|---|
| GPIO28 (BOOT button) | pull-up | boot mode: 1 = SPI boot (normal), 0 = download | Only the board's BOOT button should touch it |
| GPIO27 (user LED) | pull-up | boot mode with GPIO28 (Joint Download Boot 0/1); UART0 ROM printing, only if `EFUSE_UART_PRINT_CONTROL` is burnt | No: board-internal |
| GPIO26 (battery divider enable) | none (floating) | download mode variant (Joint Download Boot 1), only with GPIO28 = 0 | No: board-internal |
| GPIO25 (**D2**) | none | SDIO slave sampling/driving edge, with MTDI | **No**: SDIO isn't used. Safe for the TFT's DC |
| MTDI (GPIO3, back pad) | none | SDIO edge, with GPIO25 | No |
| GPIO7 (**D3**) | **none, and must not float** | JTAG source (USB Serial/JTAG or the JTAG pads), but only if `EFUSE_JTAG_SEL_ENABLE` is burnt | With the default eFuses (all 0), GPIO7 is ignored and JTAG stays on USB Serial/JTAG (Table 3-7). Safe for the TFT's CS. A 10 kΩ pull-up to 3V3 on CS also keeps the panel deselected during boot and removes any doubt |
| MTMS (GPIO2, back pad) | none | crystal frequency (40/48 MHz), only in download boot | No |

So the only strapping pin that can stop the meter booting is GPIO28, which
is the board's BOOT button and not wired to anything here. Of the XIAO's
front pads only **D2 (GPIO25)** and **D3 (GPIO7)** are strapping pins, and
with factory eFuses neither affects a normal SPI boot. The port still puts
only **outputs to high-impedance inputs** on them (the TFT's DC and CS),
not buttons. The datasheet's list matches the DevKitC-1 guide's
([DevKitC-1 user guide][devkitc]) and esptool's note on GPIO28
([esptool boot modes][esptool-boot]).

**Free pins for the port.** The meter needs SCK, MOSI, CS, DC, backlight
(and optionally RESET) for the TFT, plus four buttons: 9–10 pins of the 11
front pads. Freeing them means disabling the board's `uart0` (D6/D7: the
console is on USB Serial/JTAG anyway) and `i2c0` (D4/D5), and dropping
SPI2's MISO (D9: the panel is write-only). An RGB LED needs three more, so
two of its pins come from the back pads MTCK (GPIO4) and MTDO (GPIO5),
which are not strapping pins. The allocation is in section 7 and
appendix A.

**SPI speed.** SPI2 runs at up to **80 MHz** as master (datasheet
§4.2.1.2). Its IO_MUX pins are GPIO6 (FSPICLK), GPIO7 (FSPID), GPIO2
(FSPIQ), GPIO4 (FSPIHD), GPIO5 (FSPIWP) and GPIO10 (FSPICS0) (datasheet
§4.2.1.2 and Appendix A Table 7-1; the same in hal_espressif's
`io_mux_reg.h`). The clock's IO_MUX pin, GPIO6, is wired to the
battery-voltage divider and has no pad, so the panel's SPI clock must go
through the **GPIO matrix**. The datasheet gives no GPIO-matrix speed limit;
Zephyr's `espressif,esp32-spi` binding says IO_MUX is what allows
"frequencies higher than 20 MHz". Plan on **20 MHz**
(a full 320×240 RGB565 frame is 1.23 Mbit, about 62 ms); try 26–40 MHz on
hardware. The Pico drives its panel at 62.5 MHz. The meter redraws only
what changes, a band at a time, so 20 MHz should not be noticeable.

**USB.** The USB-C socket goes to the C5's USB Serial/JTAG controller:
`west flash` (esptool) programs it, the console appears as
`/dev/ttyACM<N>` (Linux) or `/dev/cu.usbmodem<N>` (macOS), and OpenOCD can
debug over the same cable. The first flash may need BOOT held while
plugging in (Seeed's Zephyr page does this); after that esptool resets the
chip into download mode over USB.

---

## 2. Wi-Fi

### The driver supports the C5, 5 GHz included

Zephyr's Espressif Wi-Fi driver (`drivers/wifi/esp32`, `CONFIG_WIFI_ESP32`)
is enabled by the board's `&wifi` node. It wraps Espressif's binary Wi-Fi
library (hal_espressif blobs) and implements Zephyr's `wifi_mgmt_ops`, so
`NET_REQUEST_WIFI_SCAN`, `NET_EVENT_WIFI_SCAN_RESULT` and
`NET_EVENT_WIFI_SCAN_DONE` work as they do with the AIROC driver.
`scanner.c` needs no structural change. The C5 has
`CONFIG_SOC_ESP32_WIFI_SUPPORT_5G=y` (`soc/espressif/esp32c5/Kconfig.caps`),
and the driver has 5 GHz code: the regulatory query lists the 28 5 GHz
channels from 36 to 177.

What a scan result carries, from `scan_done_handler()` in
[`esp_wifi_drv.c`][z-esp-drv]:

| `wifi_scan_result` field | From Espressif's `wifi_ap_record_t` | 5 GHz? |
|---|---|---|
| `channel` | `primary` | yes: 36–177 |
| `band` | `wifi_utils_chan_to_band(channel)` | yes: `WIFI_FREQ_BAND_5_GHZ` for 5 GHz channel numbers |
| `rssi` | `rssi` | yes |
| `mac` (BSSID) | `bssid` | yes, for hidden networks too |
| `ssid` | `ssid` (empty if hidden) | yes |
| `security` | `authmode` | yes |
| (none) | `bandwidth`, `second`, `vht_ch_freq1/2` | **dropped**: Zephyr's `wifi_scan_result` has no bandwidth field |

So `scanner.c`'s `to_chan_ap()` gets 5 GHz results today. It counts them
as "ignored" because it keeps only `WIFI_FREQ_BAND_2_4_GHZ`.

### What the driver does not do (yet)

`esp32_wifi_scan()` starts with `wifi_scan_config_t scan_config = { 0 }`
and copies **only `scan_type`** from Zephyr's `wifi_scan_params`:

1. **Hidden networks are never reported.** `show_hidden` stays `false`
   (Espressif's default, `esp_wifi.h`). The AIROC driver had the
   same blind spot in another form, which is why this project carries
   `airoc-hidden-network-results.patch`. The ESP32 equivalent is a
   one-line patch: set `show_hidden = true`.
2. **Dwell times are ignored** (`dwell_time_active`, `dwell_time_passive`):
   the library's defaults apply. Those are 120 ms a channel active and
   **360 ms a channel passive** (`esp_wifi_scan_start()` docs).
3. **Bands are ignored** (`bands`, `band_chan`). Espressif's
   `wifi_scan_config_t.channel_bitmap` can skip a band (bit 0 of
   `ghz_2_channels` / `ghz_5_channels`) or pick channels, but the driver
   never sets it.
4. **The band mode is never set.** `esp_wifi_set_band_mode()` isn't called
   anywhere in the driver or hal_espressif's Zephyr glue, so the library's
   default applies. Espressif's C5 workshop describes `WIFI_BAND_MODE_AUTO`
   (both bands) as the default ([workshop, lecture 1][esp-workshop]), so a
   plain scan should cover both bands. *Inferred, to confirm on hardware.*

Appendix B is a draft patch for 1–3 (17 lines). **Verified:** it compiles
into the meter's C5 build. Whether a zero bitmap with only bit 0 set in
the other band gives "all channels of this band" is my reading of the
header comment, and needs checking on hardware. It is upstreamable, and
would sit in `zephyr/patches/` like the AIROC one.

**Bandwidth.** Overlap on 5 GHz depends on each network's channel width
(section 5.2), which the scan result doesn't carry. Two options:

- **Patch Zephyr's API** to add a `bandwidth` (Zephyr already has
  `enum wifi_frequency_bandwidths`: 20/40/80 MHz) and a centre channel to
  `struct wifi_scan_result`, filled by the ESP32 driver from `bandwidth`,
  `second` and `vht_ch_freq1`. This is a bigger upstream change (a public
  struct), but the right one; drivers that don't fill it leave 0, unknown.
- **Assume a width** in the application: 80 MHz on 5 GHz (the default of
  nearly every consumer router since 802.11ac) and 20 MHz on 2.4 GHz.
  Simple, but wrong for 20/40 MHz enterprise deployments.

Start with the assumption; carry the API patch if the figures prove
misleading.

### Regulatory domain and DFS

- **Country.** The AIROC build sets the country at build time
  (`CONFIG_WHD_WIFI_COUNTRY`, `make pico WIFI_COUNTRY=...`), which doesn't
  exist for the ESP32. The ESP32 driver implements Zephyr's standard
  `NET_REQUEST_WIFI_REG_DOMAIN` (`esp32_wifi_reg_domain()`, calling
  `esp_wifi_set_country_code()`). It maps Zephyr's "00" (world) to
  Espressif's "01"; `force` turns off 802.11d. Espressif's library starts
  in its world-safe domain "01", so the meter should set the country at
  start-up from a new Kconfig option, e.g. `CONFIG_METER_WIFI_COUNTRY="GB"`
  (ISO 3166 alpha-2), in `scanner_init()`. The AIROC driver doesn't
  implement `reg_domain`, so the Pico keeps its WHD option.
- **Channels for the UK.** Espressif's regulatory table
  ([`esp_wifi_regulatory.txt`][esp-reg], `country GB`) allows 2.4 GHz
  channels 1–13 and, at 5 GHz:
  - 5150–5250 MHz (ch 36–48);
  - 5250–5350 MHz, DFS (ch 52–64);
  - 5470–5730 MHz, DFS (ch 100–144);
  - 5725–5850 MHz (ch 149–165, short-range devices, indoor only).

  That's **25 channels at 5 GHz** and 13 at 2.4 GHz: 38 in all. The US
  table (`country US`) differs (no 12–13, 5 GHz 36–165 with its own DFS
  ranges).
- **DFS.** Channels 52–144 are DFS channels. The C5 supports only passive
  radar detection: in station mode it may **listen** on DFS channels and
  so hears their beacons, which is all the meter needs. It never transmits
  there before it has heard an AP. With the default country policy
  (`WIFI_COUNTRY_POLICY_AUTO`), an "active" scan is active on 2.4 GHz
  1–11 and non-DFS 5 GHz, and **passive** on 2.4 GHz 12–14 and all DFS
  channels ([ESP-IDF Wi-Fi driver guide][esp-wifi-guide], "Wi-Fi Country
  Code" and "Dynamic Frequency Selection"). The meter already scans
  passively by default (`CONFIG_METER_SCAN_PASSIVE`), so this changes
  nothing for it. Hidden networks on passive-only channels can only be
  found by their beacons, which still carry the BSSID: the meter names
  hidden networks by BSSID, so that is enough.

### Scan timing for both bands

At the library's default dwell times, for the UK's 38 channels:

| Scan | 2.4 GHz (13 ch) | 5 GHz (25 ch) | Sweep |
|---|---|---|---|
| Passive, 360 ms default | 4.7 s | 9.0 s | **≈ 14 s** |
| Passive, 130 ms (patched driver, `dwell_time_passive`) | 1.7 s | 3.3 s | ≈ 5 s |
| Active, defaults (DFS and 12–13 forced passive) | 11×120 + 2×360 ms ≈ 2.0 s | 9×120 + 16×360 ms ≈ 6.8 s | ≈ 9 s |

Plus channel-switch overhead. Beacons come every 102.4 ms, so a passive
dwell of 120–150 ms hears every AP at least once a sweep. Today's
`CONFIG_METER_SCAN_TIMEOUT_S` default is 15 s and the scan thread's
watchdog allowance is built from it (`scan_task.c`). A dual-band passive
sweep at default dwell would scrape that timeout. So either:

- carry the driver patch and set `dwell_time_passive` to about 130 ms
  (sweep about 5 s, close to today's 2.4 GHz-only sweep), or
- raise the timeout to 30 s on the C5 and accept a slower refresh, or
- alternate bands, one per scan (needs the patch's `bands` support), so
  each band refreshes every other scan.

Recommended: the first, with the option of the third if 5 GHz turns out
noisy to sweep. The C5 can't listen on both bands at once
(`WIFI_BAND_MODE_AUTO` "does not mean simultaneous dual-band support",
ESP-IDF guide), so a sweep always visits the bands in turn.

### Memory

The Wi-Fi driver needs about 51 KB of system heap
(`HEAP_MEM_POOL_ADD_SIZE_ESP_WIFI`). The trial build uses 209 KB of the
378 KB of HP SRAM, Wi-Fi heap included, and the 8 MB PSRAM is unused. A
larger `CHAN_MAX_APS` for dual band (section 5.1) fits easily.

---

## 3. Display

### The ILI9341 in Zephyr

Zephyr's `ilitek,ili9341` binding ([binding][z-ili9341],
[common][z-ili9xxx-common]) uses the same MIPI DBI model as the
`sitronix,st7789v` node the Pico build uses: a `zephyr,mipi-dbi-spi` bus
node on an SPI controller, with the panel as its child. Differences from
the current setup:

| | Pico (`st7789v`) | XIAO (`ili9341`) |
|---|---|---|
| Driver | `display_st7789v.c` | `display_ili9xxx.c` (ILI9340/9341/9342C/9488) |
| Bus | `spi0`, 62.5 MHz | `spi2`, 20 MHz (GPIO matrix, section 1) |
| Size in devicetree | `width = <320>; height = <240>;` | `width = <240>; height = <320>;` (portrait, native) |
| Landscape | `mdac = <0x60>` (raw MADCTL) | `rotation = <90>;` (driver sets MV/MX); `h-mirror`/`v-mirror` to correct, or 270 |
| Byte order | `PANEL_PIXEL_FORMAT_RGB_565X` | `PANEL_PIXEL_FORMAT_RGB_565X` (same) |
| Panel tuning | gamma, VCOM, porch from Pimoroni's driver | driver defaults; `pgamctrl`/`ngamctrl`/`vmctrl1` etc. exist if needed |
| Reset | none (wired to RUN); software reset | `reset-gpios` on the bus node (optional); the driver also sends SWRESET |
| Colour order | | BGR by default; `red-blue-swap` if red and blue are swapped |
| Inversion | `inversion-off` if inverted | `display-inversion` if inverted |
| Backlight | `gpio-leds` on GPIO20 | `gpio-leds` (or LEDC PWM) on the module's LED pin |

**Rotation and size.** `ili9xxx_get_capabilities()` swaps width and height
for 90° and 270°, so with `width = <240>; height = <320>; rotation = <90>;`
the application sees **x_resolution 320, y_resolution 240**, as the UI is
laid out for. `gfx.c` checks only `x_resolution <= MAX_WIDTH` (320) and
reads the size at run time, so it needs no change. **Verified** in the
trial build's devicetree (appendix A); the picture's orientation and
mirroring can only be checked on the panel.

**Byte order.** `gfx.c` has
`BUILD_ASSERT(DT_PROP(DISPLAY_NODE, pixel_format) == PANEL_PIXEL_FORMAT_RGB_565X)`
and a run-time check of `PIXEL_FORMAT_RGB_565X`. The ILI9xxx driver
accepts both RGB565 forms and sets the panel to 16 bits for either.
`mipi_dbi_spi.c`'s `write_display()` ignores the pixel format and sends the
buffer's bytes as they are. So RGB_565X (high byte first in memory, as
`text_raster.c` writes it) arrives high byte first, as the ILI9341 expects
on SPI. **No change to `gfx.c`, `rgb565.h` or `text_raster.c`.** The
build's `BUILD_ASSERT` passed with the trial overlay.

**DMA.** Zephyr's ESP32 SPI driver can use GDMA (`dma-enabled;` in the
trial overlay). Without it, transfers are limited to the 64-byte SPI FIFO
a chunk. The meter's band buffer is one text line (320×24×2 = 15 KB).

**ESP32 LCD peripheral.** Zephyr also has `mipi_dbi_esp32.c`, for the
LCD_CAM parallel (i80) interface of chips that have one. It is not for an
SPI panel; `zephyr,mipi-dbi-spi` is the right model here.

### The module

"2.2 inch SPI TFT ILI9341 240x320" modules (often a red PCB with an SD
card socket; pins VCC, GND, CS, RESET, DC/RS, SDI/MOSI, SCK, LED,
SDO/MISO) vary by maker. Before wiring, check on yours:

- **VCC**: most take 3.3–5 V through an on-board regulator. Power it from
  the XIAO's 3V3 if it is a 3.3 V-only part, or from 5V/VBUS if it has a
  regulator. Logic is 3.3 V either way.
- **LED (backlight)**: either straight to the LEDs through a resistor
  (tens of mA) or through a transistor on the module. Measure it. A C5 pin
  sources 40 mA typical at full drive with the output still above 2.64 V,
  and sinks 28 mA (datasheet Table 5-4, `PAD_DRIVER` = 3). That's a
  typical figure, not a limit, and it's well above what to load a pin with
  continuously. The wiring in section 7 therefore assumes a small
  transistor or MOSFET switch, which costs nothing and keeps the LED
  current off the GPIO. The
  backlight could instead be dimmed with LEDC PWM, which the Pico can't do
  on the Display Pack.
- **RESET**: wire it to a GPIO (as the trial overlay does) or to 3V3
  through 10 kΩ with 100 nF to GND. The driver sends a software reset
  either way.

---

## 4. Buttons and LED

### Buttons

The app already reads buttons through Zephyr's input subsystem:
`controls.c` takes `INPUT_EV_KEY` events from any `gpio-keys` device
(`INPUT_CALLBACK_DEFINE(NULL, ...)`), and `keys.c` turns X+Y into a chord.
None of it is board-specific. The port needs only a `gpio-keys` node with
the same `zephyr,code` values (`INPUT_KEY_A`, `_B`, `_X`, `_Y`), as in
appendix A.

- **Wiring**: each button from its GPIO to GND (press to make), as the
  Display Pack's are.
- **Pull-ups**: `GPIO_PULL_UP | GPIO_ACTIVE_LOW`. The C5's internal
  pull-ups (45 kΩ typical, datasheet Table 5-4) are enough for short leads inside a case. For
  leads over 10–20 cm, add 10 kΩ external pull-ups.
- **Debounce**: `gpio-keys` debounces in software
  (`debounce-interval-ms`, default 30 ms). The trial overlay sets it
  explicitly. No capacitors needed.
- **Pins**: D0, D1, D4, D5 (GPIO1, 0, 23, 24). None is a strapping pin, so
  a button held at power-up can't change the boot mode.
- **The board's own BOOT button** (GPIO28) is already a `gpio-keys` key
  (`INPUT_KEY_0`). `keys.c` ignores codes it doesn't know, so it does
  nothing; it could become a fifth button.

### LED

The Display Pack's RGB LED is driven by `pwm-leds` and dimmed to 2%
(`status_led.c`). The XIAO ESP32-C5 has:

- **a single-colour user LED** (yellow, GPIO27, active low; Zephyr's
  `led0`). It is too limited for the meter's six states: blue scanning,
  green, amber, red, magenta, off.
- **a charge LED** (red), driven by the charger, not software.
- **no RGB LED.**

So for the same status colours, add an RGB LED:

- **Discrete RGB LED (recommended).** A 5 mm common-anode LED with three
  resistors on three LEDC (`espressif,esp32-ledc`) PWM channels, through
  `pwm-leds`, exactly as now. `status_led.c` is unchanged, since it uses
  only the node labels `pack_rgb_led`, `pack_led_red/green/blue`,
  `pack_leds` and `pack_backlight`. The LEDC has 6 channels; the trial
  overlay uses channels 0–2 on GPIO12 (D7), GPIO4 (MTCK) and GPIO5 (MTDO).
  **Verified:** builds, with `CONFIG_PWM_LED_ESP32=y` and `CONFIG_LED_PWM=y`.
- **A WS2812 ("NeoPixel")** would need one pin, but Zephyr's WS2812 drivers
  (`ws2812_spi.c`, `_i2s.c`, `_gpio.c`, `_uart.c`, ...) all need a
  peripheral this layout has already used or haven't been tried on the C5,
  and the LED-strip API differs from the LED API. Not worth it.
- **Fallback**: the on-board yellow LED, blinking patterns instead of
  colours. That's a `status_led.c` variant, and poorer.

---

## 5. Application changes

The trial build shows that the board-specific parts (display, buttons,
LED, watchdog, flash log, reset note) compile unchanged. The work is in
making the **2.4 GHz assumptions** band-aware, plus some Pico-specific
corners.

### 5.1 Data model (`chan_stats.h/.c`, `ap_history.c`, `scanner.c`)

Today: `CHAN_FIRST 1`, `CHAN_LAST 14`, `CHAN_COUNT 14`;
`struct chan_ap.channel` is "CHAN_FIRST..CHAN_LAST";
`chan_stats_add()` rejects anything else as "ignored";
`chan_stats_channel()` and `overlap_mhz()` use `centre_mhz()` for 2.4 GHz;
`chan_stats_recommend()` picks from `{1, 6, 11}`; `scanner.c` zeroes the
channel of non-2.4 GHz results.

Proposed:

- **`struct chan_ap`** gains `uint8_t band` (an enum of the meter's own:
  `CHAN_BAND_2G4`, `CHAN_BAND_5G`, so `chan_stats.c` stays free of Zephyr
  headers) and `uint8_t width_mhz` (20/40/80; 0 = unknown, treated as the
  band's assumed width), plus optionally the centre channel. `channel`
  stays `uint8_t` (36–177 fit).
- **A channel table per band**, not a range:
  `static const uint8_t chans_5g[] = {36, 40, ..., 165, 169, 173, 177};`
  with a lookup from channel number to index. Country-specific channels
  come from the scan itself: show a channel if the regulatory domain
  allows it (query `NET_REQUEST_WIFI_REG_DOMAIN` once at start-up for the
  list) or only those with networks.
- **`CHAN_MAX_APS`** 64 → 128 for dual band: busy places have 40–60 BSSIDs
  per band. A `struct chan_ap` is about 44 bytes, and the app keeps about
  four copies (scan, history, snapshot, UI), so about 22 KB: fine on the C5,
  about 11 KB more than today on the Pico (264 KB SRAM, check headroom).
  Make it a Kconfig option with a per-board default.
- **`chan_stats_add()`** accepts either band; "ignored" becomes "no band
  the meter shows" (6 GHz, sub-GHz, unknown).
- **`chan_info`** and the functions that work out a channel take
  `(band, channel)`.
- **`ap_history`** merges by BSSID as now; the band is part of the record.
  A dual-band AP has different BSSIDs per band, so they stay separate rows.
- **`scanner.c`**'s `to_chan_ap()` maps `r->band` to the meter's band
  instead of discarding non-2.4 GHz results. The width comes from a
  patched `wifi_scan_result` or the assumption (section 2).
- **Unit tests** (`tests/meter_units`): every new rule is pure arithmetic
  and belongs there: channel tables, 5 GHz occupancy, recommendation, ties.

### 5.2 Overlap rules

**2.4 GHz (today)**: channels 5 MHz apart, each BSS 20 MHz wide, so a BSS
on *n* overlaps *n*±1 by 15 MHz, ±2 by 10 and ±3 by 5. Interference is
power × overlap ÷ 20. That stays, with one refinement worth making while
there: a 40 MHz 2.4 GHz BSS (HT40, `second` above or below) spans 40 MHz.

**5 GHz**: channel numbers are still 5 MHz apart (f = 5000 + 5·n MHz), but
usable 20 MHz channels are every 4th number (36, 40, 44, ...) and **don't
overlap each other**. Overlap comes only from **channel bonding**: a BSS
occupies a fixed, aligned block of 20 MHz channels set by its width:

| Width | Blocks (channels occupied) |
|---|---|
| 20 MHz | its primary only |
| 40 MHz | 36+40, 44+48, 52+56, 60+64, 100+104, ..., 149+153, 157+161 |
| 80 MHz | 36–48, 52–64, 100–112, 116–128, 132–144, 149–161 |
| 160 MHz | 36–64, 100–128 |

So the rule is simpler than at 2.4 GHz: **a BSS interferes fully with every
20 MHz channel in its block and not at all outside it**. The primary
channel carries beacons and most management traffic; data uses the whole
block. RSSI is measured on the 20 MHz primary, which approximates the
per-20 MHz power of the whole block, so weight each occupied channel by 1
(not 1/4). This is integer bookkeeping with no new arithmetic in
`chan_dbm_to_power()`/`chan_power_to_dbm()`, and it keeps "accurate to
1 dB".

### 5.3 Channel recommendation

- **2.4 GHz**: unchanged: the quietest of 1, 6, 11.
- **5 GHz**: recommend an **80 MHz block** (what a home router would be
  set to), by the summed interference over its four channels, then fewer
  networks, then non-DFS before DFS (DFS channels make the router wait 60 s
  or more for radar before using them, and can force a channel change),
  then the lower block. Footer: "Use 36-48: clear" or "Use ch 36 (80 MHz):
  clear". Optionally a 20 MHz recommendation for 20 MHz-only setups, as a
  Kconfig choice.
- In the UK, 149–165 is short-range-device spectrum (200 mW, indoor only)
  and not every router offers it. Leave it off by default
  (`CONFIG_METER_RECOMMEND_5G_UPPER=n`) but still show networks on it.

### 5.4 UI: a 5 GHz chart on 320 pixels

The 2.4 GHz chart has 13 columns of (320 − 34) ÷ 13 = 22 px, with bars
3 + 6 + 3 px and the channel number and network count above and below
each. For the UK's 25 5 GHz channels:

- **A second Channels screen, "5 GHz"**, after the 2.4 GHz one: A/B cycle
  2.4 GHz → 5 GHz → Networks → Events. This adds a screen, not a mode, and
  fits `ui.c`'s existing screen list. X still "scans now" on both.
- **Columns of 10 px** (signal bar 4, interference 8, 1 px gaps), with
  **6 px gaps between the 80 MHz blocks** so the blocks read as groups:
  25 × 10 + 5 × 6 = 280 px of the 286 beside the dBm axis. Fits.
- **Labels**: a three-digit number is 30 px in the 10×16 font, so label
  only **the first channel of each block** (36, 52, 100, 116, 132, 149;
  blocks are ≥ 40 px). Draw DFS blocks' labels or a thin underline in a
  different colour. Highlight the recommended block's underline.
- **Network counts** above bars don't fit in 10 px (two digits are 20 px).
  Show one count per block, centred over it, or drop counts on this screen.
  Zephyr's CFB fonts are 10×16, 15×24 and 20×32, so there's no smaller one
  to borrow; `text_raster.c` could gain a 6×8 font, which is a bigger
  change.
- **A shorter range of channels** if the regulatory query says so: in the
  US, 36–165 has 25 channels (no 169–177); "only channels the country
  allows" keeps it at about 25 everywhere.
- **Screenshots**: `tests/ui_render` draws the 2.4 GHz screen on a fake
  320×240 panel; add sample 5 GHz data and a `channels-5g.png` screenshot.

### 5.5 Networks screen

Today: `SSID_CHARS 24`, `CHAN_CHARS 3` (two digits and a space),
`RSSI_CHARS 4`; (24 + 3 + 4) × 10 + 8 = 318 of 320 px. A three-digit 5 GHz
channel breaks the layout: `snprintf("%*u", CHAN_CHARS - 1, ...)` writes
"100" into a two-character column and pushes the RSSI over. Options:

- `CHAN_CHARS 4` and `SSID_CHARS 23`: costs one character of every name.
  The hidden-network name "hidden 02:1f:3c:a2:7f:06" is exactly 24
  characters, so it would need shortening (e.g. "hidden 021f3c:a27f06", 20
  characters, still unique).
- Keep 24 characters and show the band by **colour of the channel number**
  (e.g. cyan for 5 GHz), and narrow the RSSI column ("-92" fits in 3 + the
  name gap). Fiddlier.
- **A band filter**: the Networks screen follows the last Channels screen
  shown (2.4 or 5 GHz). Combined with either of the above, it halves the
  list and keeps paging sensible.

Recommended: `CHAN_CHARS 4`, `SSID_CHARS 23`, the shorter hidden name,
5 GHz channels in a second colour, and a band filter. The footer's "other"
count means 6 GHz and unknown results only.

### 5.6 Status LED

It shows how busy the recommended channel is. With two bands, show the
band of the Channels screen last viewed, or the worse of the two. A small
`main.c` change; documented in USAGE.md.

### 5.7 Pico-specific code to make conditional or replace

| Today | On the ESP32-C5 |
|---|---|
| `zephyr/patches/zephyr/airoc-hidden-network-results.patch` | Not needed (AIROC driver only); harmless, as both targets share one patched Zephyr tree. Add the ESP32 scan-parameters patch (appendix B) beside it. |
| `zephyr/patches/zephyr/rpi-pico-boot2-clang-target.patch` | Not needed; harmless (only the RP2040's boot2 CMake). |
| `CONFIG_WHD_WIFI_COUNTRY` in `app/prj.conf` | Move to `app/boards/rpi_pico_rp2040_w.conf` (the WHD symbol is undefined on the C5, and Zephyr fails the build on an undefined symbol). On the C5: `CONFIG_METER_WIFI_COUNTRY` and `NET_REQUEST_WIFI_REG_DOMAIN` at start-up (section 2). |
| Restart after `CONFIG_METER_RECOVER_AFTER` failed scans (`scan_health.c`, `scan_task.c`) | Keep: it's generic and costs nothing. Whether the ESP32 driver ever needs it is unknown. Reword the README's AIROC buffer-leak explanation as Pico-specific. |
| `reset_note.c`: `__noinit` in SRAM | Builds (`.dram0.noinit`). The datasheet (§4.1.3.2) says the C5's CPU, Core and System resets "preserve the data stored in internal memory"; only a Chip Reset (power-up, CHIP_PU) clears it. A software restart (`sys_reboot()`), a panic restart and a watchdog reset keep the note, as on the RP2040, and a power cut loses it, which is what `reset_note.c` relies on. **Remaining risk**: the ROM loader and simple-boot code run in SRAM before the app and could overwrite the part the note lands in. Espressif's own code keeps such data in RTC/LP memory. If testing shows the note lost, the board's devicetree already has `retainedmem0` (`zephyr,retained-ram`, 512 bytes of the 16 KB LP SRAM): use Zephyr's **retention** API for the note on the C5, behind the existing `reset_note.h` interface. |
| "The RP2040 can't tell reset button, debugger and hardware watchdog apart" | The ESP32 can: `hwinfo_get_reset_cause()` (`hwinfo_esp32.c`, `esp_reset_reason()`) reports power-on, reset pin, software, watchdog (interrupt, task or RTC WDT), panic and brown-out. `boot_record` could record it, and the Events screen show "reset button", "hardware watchdog", "brown-out". New behaviour, optional; add `CONFIG_HWINFO`. |
| `watchdog.c`: `wdt0`, RP2040 notes (8.3 s limit, not stopped at start-up), `CONFIG_TASK_WDT_*` comments in `prj.conf` | `wdt0` exists (Timer Group 0 MWDT, `CONFIG_WDT_ESP32`) and the build links. The C5 has three digital watchdogs: an MWDT in each of the two timer groups and an RTC watchdog (RWDT), each with four stages and a 32-bit counter. It also has an analog "super watchdog", and flash-boot protection that resets the chip if booting from flash stalls (datasheet §4.1.3.9). Move the RP2040 comments to the board's `.conf`; check that `wdt_disable()` behaves as `watchdog.c` expects. |
| `event_log.c`: FCB on `storage_partition`, created by the Pico overlay (last 64 KB) | The C5 board's 8 MB table already has `storage_partition` at 0x7b0000, 192 KB, with 4 KB erase sectors: no overlay change. `west flash` writes only the bootloader and app regions, so the log survives updates, as with UF2; `esptool erase_flash` wipes it. Check `MAX_SECTORS` (the log uses at most that many of the 48 sectors). |
| `usb_console.c`, `app/usb-console.conf`, `app/usb-console.overlay` (CDC-ACM with DTR-based flow control; Raspberry Pi VID/PID) | Not used: the console is the hardware USB Serial/JTAG (`zephyr,console = &usb_serial`, board default). `usb_console.c` is compiled only with `CONFIG_USBD_CDC_ACM_CLASS`, so it drops out. Check on hardware that the console doesn't block with no host attached (the problem `usb_console.c` solves on the Pico), and how the X+Y log dump behaves (it waits up to 1 s for the log backend). |
| `README.md`, `docs/USAGE.md`, `docs/quick-reference.html`/`.pdf`, `hardware/` | Per-target sections (hardware, pins, flashing, battery), the 5 GHz screen and band filter, the LED meaning. |

---

## 6. Build system

### Toolchain: GCC is unavoidable for now

The project builds everything with clang: ATfE for the Pico, host clang for
native_sim ("There is no GCC anywhere", README). ATfE is Arm-only. For
RISC-V the candidates are the Zephyr SDK's GCC and the Zephyr SDK 1.0's
LLVM (clang 19). **Tried both** on `samples/hello_world` for
`xiao_esp32c5/esp32c5/hpcore`:

| Toolchain | Result |
|---|---|
| Zephyr SDK 1.0.1, GNU `riscv64-zephyr-elf` (GCC 14.3.0) | **Builds and links** (`zephyr.bin`, 135 KB). |
| Zephyr SDK 1.0.1, LLVM (`-DZEPHYR_TOOLCHAIN_VARIANT=zephyr/llvm`) | **Fails**: hal_espressif adds GCC-only `-fstrict-volatile-bitfields` (`zephyr/esp32c5/CMakeLists.txt`); with that removed, clang's assembler rejects Zephyr's `soc/espressif/esp32c5/soc_irq.S:73` ("immediate must be an integer in the range ..."). Espressif SoC support in Zephyr is GCC-only today. |

So the C5 needs **GCC from the Zephyr SDK** (1.0.1 is what the pinned
Zephyr's `SDK_VERSION` asks for). The coding rules' "no GCC" would become
"clang for the Pico and the tests; the Zephyr SDK's GCC for the ESP32-C5".
The strict warnings (`-Wall -Wextra -Wpedantic -Werror`) work with GCC:
**verified**, no warnings from the app.

**clang-tidy** must still run on every file (CODING.md rule 0). The trial
build with clang-tidy on fails before checking anything: the compile
commands carry GCC's RISC-V flags (`-march=rv32imac_zicsr_zifencei`,
`-mabi=ilp32`, `-mcmodel=medlow`), and `scripts/clang-tidy-cross.py`, which
gives clang-tidy ATfE's headers, doesn't add a target, so clang takes them
as x86 options. Teach it the GCC case: add `--target=riscv32-unknown-elf`,
point `-isystem` at the SDK's picolibc and GCC headers (as it does for
ATfE), and drop flags clang doesn't know (`-fstrict-volatile-bitfields`,
`-fno-reorder-functions`, ...). An hour or two of work, not a blocker.
Until then the C5 build would need `-DMETER_CLANG_TIDY=OFF`, which the rules
don't allow for a merge.

### Dockerfile

Additions, which keep the Pico target as it is:

```dockerfile
# Zephyr SDK (minimal) + the RISC-V GNU toolchain, for the ESP32-C5.
ARG ZEPHYR_SDK_VERSION=1.0.1
RUN base=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION} \
    && cd /opt && wget -q $base/sha256.sum \
    && for f in zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-$(uname -m)_minimal.tar.xz \
                toolchain_gnu_linux-$(uname -m)_riscv64-zephyr-elf.tar.xz; do \
         wget -q $base/$f && grep " $f\$" sha256.sum | sha256sum -c - || exit 1; done \
    && tar xf zephyr-sdk-*_minimal.tar.xz && mkdir -p zephyr-sdk-${ZEPHYR_SDK_VERSION}/gnu \
    && tar xf toolchain_gnu_*_riscv64-zephyr-elf.tar.xz -C zephyr-sdk-${ZEPHYR_SDK_VERSION}/gnu \
    && rm -f *.tar.xz sha256.sum
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}

# esptool (image conversion, flashing) and hal_espressif's Python needs.
RUN west packages pip --install \
    && pip install --no-cache-dir -r modules/hal/espressif/zephyr/requirements.txt

# The ESP32-C5's Wi-Fi, PHY and coexistence libraries.
RUN west blobs fetch hal_espressif --auto-accept --allow-regex '.*/lib/esp32c5/lib(core|net80211|pp|phy|coexist|mesh|espnow)\.a$'
```

(The arm64 SDK asset is `linux-aarch64`, so `$(uname -m)` covers both
image architectures. Check whether the Wi-Fi driver links `libmesh` and
`libespnow` without mesh enabled before trimming the list further.)

Cost: the riscv64 toolchain is a 152 MB download and 1.1 GB unpacked (all
its multilibs), hal_espressif 173 MB, mbedtls + tf-psa-crypto 37 MB. The
study image was 9.7 GB with the SDK's LLVM (1.9 GB, not needed) included;
without it, roughly 6.5–7 GB against 5.5 GB today.

### west.yml

Add to the `name-allowlist`:

```yaml
          # ESP32-C5 SoC, Wi-Fi driver and its binary libraries
          - hal_espressif
          # The ESP32 Wi-Fi driver selects MBEDTLS and PSA_CRYPTO
          - mbedtls
          - tf-psa-crypto
```

Imported projects can only be fetched with a plain `west update`, which the
Dockerfile already runs. **Verified** that these three are all the C5 build
needs from the pinned Zephyr manifest.

### cmake/toolchain.cmake

Already does the right thing: for a board other than `rpi_pico*` and
`native_sim*` it returns early and leaves Zephyr's default toolchain (the
Zephyr SDK) in place. **Verified**: the C5 build picked
`riscv64-zephyr-elf-gcc` with no change. Worth making explicit in a comment
and a `message(STATUS ...)`.

### scripts/build.sh and Makefile

- A new target, e.g. `make xiao` → `scripts/build.sh xiao` →
  `board=xiao_esp32c5/esp32c5/hpcore`, without the USB CDC-ACM
  `EXTRA_CONF_FILE`/`EXTRA_DTC_OVERLAY_FILE` (Pico only), with
  `-DCONFIG_METER_WIFI_COUNTRY="$WIFI_COUNTRY"` (an ISO code, not a WHD
  name, so `WIFI_COUNTRY` needs a per-target meaning or a second variable).
- Outputs: `zephyr.bin` (the app image; simple boot, app at 0x2000) and
  `zephyr.elf`. To flash without a west workspace on the host:
  `esptool --chip esp32c5 write-flash 0x2000 network-meter.bin`, or a
  merged image (`esptool merge-bin`) for a browser flasher such as ESP Web
  Tools. On Linux, `west flash` can run inside Docker with
  `--device /dev/ttyACM0`; on macOS, Docker can't reach USB, so flash from
  the host.
- **Bootloader**: the board's `Kconfig.sysbuild` defaults to MCUboot
  when building with sysbuild. Plain `west build` (as `build.sh` does) gives
  Espressif's **simple boot** (`CONFIG_ESP_SIMPLE_BOOT=y`, verified), which
  is right for the meter: no OTA, one image.
- **Flash size**: the build shows `CONFIG_ESPTOOLPY_FLASHSIZE="2MB"`
  while the flash runner passes `--esp-flash-size=8MB` from the devicetree.
  Set `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` in the board's `.conf` so the image
  header matches the module.
- `make pico` stays as it is. Both targets in one image.

### Application files

- `app/boards/xiao_esp32c5_esp32c5_hpcore.overlay` (appendix A, picked up
  automatically by board name) and `app/boards/xiao_esp32c5_esp32c5_hpcore.conf`
  (flash size, `CONFIG_METER_WIFI_COUNTRY`, scan timeout or dwell, retention).
- `app/boards/rpi_pico_rp2040_w.conf` for the WHD country and the RP2040
  watchdog comments now in `prj.conf`.

### Check scripts

- `scripts/check-docs.py`, `scripts/check-style.sh`: unaffected (they check
  sources, not targets).
- `scripts/clang-tidy-cross.py`: the RISC-V/GCC case (above).
- `scripts/test.sh`: unchanged; the native_sim tests cover the new
  band-aware logic. Optionally a "does it build" step for both firmware
  targets, which would have caught, for example, the WHD symbol.

---

## 7. Hardware: wiring, BOM, power, case

### Wiring

| XIAO pad (GPIO) | To | Why this pin |
|---|---|---|
| 3V3 | TFT VCC (if 3.3 V-only), RGB LED common anode | |
| 5V (VBUS) | TFT VCC (if it has a regulator; only when on USB, so see power) | |
| GND | TFT GND, all button commons, LED switch | |
| D8 (8) | TFT SCK | board's SPI2 SCK |
| D10 (10) | TFT SDI (MOSI) | board's SPI2 MOSI |
| D3 (7) | TFT CS, plus 10 kΩ to 3V3 | strapping pin with no internal pull: the pull-up fixes its level at reset and keeps the panel deselected while the chip boots |
| D2 (25) | TFT DC / RS | strapping pin; also an input on the TFT |
| D6 (11) | TFT RESET | UART0 TX by default: disabled |
| D9 (9) | TFT LED, through an N-MOSFET (e.g. 2N7000/BSS138) or NPN + 1 kΩ, unless the module switches it | SPI2 MISO by default: not needed |
| D0 (1) | Button A, other side to GND | |
| D1 (0) | Button B, other side to GND | |
| D4 (23) | Button X, other side to GND | I2C0 SDA by default: disabled |
| D5 (24) | Button Y, other side to GND | I2C0 SCL by default: disabled |
| D7 (12) | RGB LED red cathode, via 220–470 Ω | LEDC channel 0 |
| MTCK back pad (4) | RGB LED green cathode, via 470 Ω–1 kΩ | LEDC channel 1 |
| MTDO back pad (5) | RGB LED blue cathode, via 470 Ω–1 kΩ | LEDC channel 2 |
| — | TFT SDO (MISO), SD card pins | not connected |

Without the RGB LED, D7 stays free and the back pads stay unsoldered; the
status shows on the yellow user LED only. To keep the panel's backlight on
permanently instead, tie LED to 3V3 (through the module's resistor) and
free D9.

### Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | Seeed Studio XIAO ESP32-C5 | 21 × 17.8 mm; "External RF antenna included", on a U.FL socket ([Seeed][seeed-gs]). Seeed's pin map lists no other antenna, so treat the U.FL antenna as required |
| 1 | 2.2" SPI TFT module, ILI9341, 240×320 | Check VCC range and LED wiring (section 3) |
| 4 | Tactile push switches, 6×6 mm (or 12×12 with caps) | Press to make |
| 1 | RGB LED, 5 mm, common anode (optional) | Or a 3.3 V-friendly SMD RGB on a carrier |
| 3 | Resistors for the LED: 330 Ω (red), 680 Ω (green, blue) | Dimmed further in software (2%) |
| 1 | N-MOSFET 2N7000 / BSS138 (or NPN + 1 kΩ base resistor) | Backlight switch, if the module has none |
| 1 | 10 kΩ | Pull-up on TFT CS (GPIO7, a strapping pin) |
| 1 | 10 kΩ, 100 nF (optional) | TFT reset RC, if not on a GPIO |
| 1 | LiPo cell, 3.7 V, with protection, e.g. 350–500 mAh | Soldered to the XIAO's BAT+/BAT− pads (no JST socket on the XIAO) |
| 1 | Slide switch (optional) | In the battery lead, for a hard off |
| — | Hook-up wire, perfboard for the buttons, headers | |

### Power and battery

The XIAO ESP32-C5 has **battery charging on board**: an SGM40567 charger
from USB-C to BAT+/BAT− pads on the back, a red charge LED, and a battery
voltage monitor (a TPS22916 switch and a 2 × 100 kΩ divider on GPIO6,
enabled by GPIO26) ([Seeed getting started][seeed-gs], "Battery Usage").
That replaces the **LiPo Amigo Pro and the Schottky diode** of the current
build: the XIAO runs from USB when plugged in and from the cell otherwise.
Seeed's page doesn't state the charge current. Check the schematic
([PDF linked from the wiki][seeed-gs]) or measure, and pick a cell that
takes it at ≤ 1 C.

**Battery level**, new and cheap: enable GPIO26, read GPIO6 through the
ADC (`adc0`, Zephyr's ADC API), × 2. It could be a header item on the
screens. Not in the current meter; optional.

**Current**: not measured on the board. The datasheet's figures (Tables
5-7 and 5-8, preliminary, at 3.3 V) for receiving, with the CPU idle,
are **99–107 mA on 2.4 GHz** and **127–135 mA on 5 GHz**. A scan is
almost all listening, especially a passive one. Transmit peaks are 246–339
mA on 2.4 GHz and 377–381 mA on 5 GHz; an active scan transmits a
probe request on each active channel, and the passive default doesn't.
Add the CPU (modem-sleep figures, Table 5-11, put a busy 240 MHz CPU at
26–43 mA, partly already in the RX figures), the TFT backlight (20–60 mA
depending on module) and the XIAO's regulator. That gives roughly
**150–200 mA** while scanning, against 80–100 mA for the Pico W build.
With 10 s between scans the radio still listens for ≈ 5 s of every 15,
so the average is lower: estimate 100–150 mA. A 500 mAh cell would last
about 3–5 hours. The supply must also handle ~400 mA transmit peaks.
Measure on hardware.

**5 V for the TFT**: the XIAO's 5V pad is VBUS. On battery it carries
nothing (or the battery through a diode, depending on the board: check the
schematic), so a module that needs its regulator's input above 3.3 V would
go dark on battery. Prefer a module that runs from 3.3 V (most have a
jumper, often J1, to bypass the regulator for 3.3 V).

**Antenna**: the 5 GHz band is lossier through cases and hands. Mount the
FPC antenna flat against the inside of the case's back or top, away from
the TFT's metal frame and the battery. The README's USB 3 noise warning
applies even more: USB 3 noise covers 2.4 GHz harmonics, and 5 GHz links
are weaker to start with.

### The case generator

`hardware/case/case.py` is parametric (every dimension is a constant), but
its layout is built around the Display Pack: the Pico plugs into it, the
buttons are the pack's, the shelf's posts hold the Pico, and the charger
hangs at the back. For the new parts:

- **Reusable**: the shell, lip, corner radius, bolt and nut-trap blocks,
  printing orientation, button caps and stems (`BUTTON_*`), LED hole, the
  STL export and preview code. That's most of the 969 lines.
- **New**: the TFT module's outline (measure: a typical 2.2" module is about
  67 × 40 mm with a 45 × 34 mm active area, off-centre because of the pin
  header and SD socket); a mounting for a button board (four switches on
  perfboard at chosen positions) instead of the pack's buttons; the XIAO's
  mount with its USB-C opening (on the XIAO now, not a charger); a pocket
  for the cell; and the antenna. No charger, no PWR plunger.

Estimate: 1–2 days to adapt with calipers in hand, as a second layout in
the same script (`--layout xiao`) rather than a fork.

---

## 8. Risks, unknowns, effort and plan

### Risks and unknowns

| # | Risk or unknown | Impact | How to retire it |
|---|---|---|---|
| 1 | Default band mode: does a plain scan cover 5 GHz? (inferred AUTO) | No 5 GHz results without an extra call | Step 4: log `band` of every result; if needed, call `esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO)` in the driver patch |
| 2 | Driver patch semantics (`channel_bitmap` zero = all channels) | Scans the wrong band, or nothing | Step 5: test each `bands` value; log channels heard |
| 3 | Hidden networks with `show_hidden = true` on passive channels | Fewer hidden networks than expected | Compare with a phone's analyser app |
| 4 | Scan duration and the 15 s timeout / watchdog | Truncated scans, watchdog restarts | Log scan durations; tune `dwell_time_passive` |
| 5 | GCC in a clang-only project; clang-tidy on RISC-V | Rule change; work in `clang-tidy-cross.py` | Decide the rule first; fix the script in step 1 |
| 6 | `__noinit` reset note survives ESP32 resets? Datasheet: all resets but Chip Reset keep internal memory; the ROM loader could still overwrite it | Events screen misreports restarts | Test SW reset, watchdog, panic, power cut; if lost, `retainedmem0` + retention API |
| 7 | 2.2" module variants (VCC, LED wiring, BGR, mirroring) | Dark, mirrored or colour-swapped panel | Measure; `rotation`, `h-mirror`, `red-blue-swap`, `display-inversion` |
| 8 | SPI over the GPIO matrix at 20+ MHz | Garbled picture at higher clocks | Start at 20 MHz; try 26/40 |
| 9 | USB Serial/JTAG console with no host: blocking or dropping? | Start-up hangs on a charger (the Pico problem) | Boot from a USB charger and a battery; watch the display |
| 10 | Wi-Fi blob licence and size | Binary libraries in the firmware | As now with Infineon's: document in README's licence section ([esp32-wifi-lib][esp-wifi-lib]) |
| 11 | 5 GHz bandwidth not in Zephyr's scan result | Overlap figures approximate | Start with the 80 MHz assumption; Zephyr API patch later |
| 12 | Long-run stability of the ESP32 Wi-Fi driver (the AIROC leaked buffers after 12 h) | Needs the restart path, or not | 24–48 h soak test with the event log |
| 13 | Antenna placement and case loss at 5 GHz | Fewer 5 GHz networks heard | Compare open board vs in case |

### Effort estimate

For one developer who knows this code base and Zephyr, with the hardware on
the desk. "Days" are focused working days, including the docs, tests and
review the project's rules require.

| Step | Work | Days |
|---|---|---|
| 1 | Toolchain in the Dockerfile, west.yml, `make xiao`, `clang-tidy-cross.py` for RISC-V, board `.conf`, hello world and console on hardware | 1–1.5 |
| 2 | Display: overlay, orientation, colours, SPI clock, backlight; the meter's existing screens on the ILI9341 | 0.5–1 |
| 3 | Buttons and RGB LED: overlay, wiring, check all screens and the X+Y chord | 0.5 |
| 4 | 2.4 GHz scans on the C5 (5 GHz ignored, as today): country code, timing, hidden networks via the driver patch, watchdog and reset note (retention API), event log | 1.5–2 |
| | **A working 2.4 GHz meter on the new hardware** | **≈ 3.5–5** |
| 5 | Dual-band scanning: driver patch (bands, dwell), scan timing, Kconfig | 1–1.5 |
| 6 | Data model and 5 GHz rules in `chan_stats.c`/`ap_history.c`, with unit tests | 2–3 |
| 7 | UI: 5 GHz chart screen, networks screen band handling, footer, LED; `tests/ui_render` screenshots | 2–3 |
| 8 | Docs: README, USAGE, quick reference (HTML and PDF), hardware README, wiring diagram, BOM | 1–1.5 |
| 9 | Case: second layout in `case.py`, test prints | 1–2 |
| 10 | Soak test (24–48 h, elapsed rather than effort), fixes | 0.5–1 |
| | **Dual-band meter, documented, in a case** | **≈ 12–18 (2½–3½ weeks)** |

Upstreaming the driver patch (and optionally the scan-result bandwidth
field) is extra, about 1–2 days plus review time, and can run in parallel.

### Recommended plan

1. **Board and hello world.** Add the SDK's riscv64 toolchain, esptool and
   hal_espressif (with blobs) to the image and `make xiao` to the Makefile.
   Fix `clang-tidy-cross.py` for RISC-V. Flash `hello_world`, then the
   meter as it is with only the overlay: console over USB, the board LED.
   *Exit: the app starts, the watchdog runs, the event log writes and
   reads back across resets (test each reset cause).*
2. **Display.** Wire the TFT; the meter's screens appear in landscape, the
   right way round, with the right colours. *Exit: matches
   `tests/ui_render` screenshots; SPI clock settled.*
3. **Buttons (and LED).** *Exit: A/B/X/Y and X+Y as on the Pico; LED colours
   and dimming.*
4. **2.4 GHz scan.** Country code at start-up, the driver patch for hidden
   networks, check what the default scan returns (log `band`). *Exit: the
   2.4 GHz meter works as on the Pico, side by side with it.* At this point
   the port is a usable 2.4 GHz meter with better radio hardware.
5. **5 GHz scanning.** Bands and dwell in the patch, scan timing, timeouts.
   *Exit: every 5 GHz AP a phone's analyser sees, within each scan, in under
   about 6 s a sweep.*
6. **Model and UI.** Band-aware `chan_stats`, the 5 GHz chart screen, the
   networks screen, the recommendation; unit and render tests first. *Exit:
   `make test` green with new tests and screenshots; on hardware, the
   figures make sense against a known router.*
7. **Docs, case, soak test.** Then a pull request per step or two, each
   reviewed against `.github/pull_request_template.md`.

Steps 1–4 can go into the repository as a second target without
disturbing the Pico build; 5–6 change shared code and need the Pico's
2.4 GHz behaviour kept exactly (the existing tests guard it).

---

## 9. What was verified, and how

Everything here was done in a cloud container with Docker; **no real
hardware**. The XIAO, the TFT, the buttons and the radio were never run.

**Built and checked (verified):**

- **Zephyr board presence**: the Zephyr tree at main (`e7b400b`), v4.4.0,
  v4.5.0 and the project's pin (`a915b2f`): `boards/seeed/xiao_esp32c5`
  absent in v4.4.0, present from the pin on; `git diff` shows the board
  identical at the pin and v4.5.0, only property order changed on main,
  and `drivers/wifi/esp32/src/esp_wifi_drv.c` identical at the pin and
  main.
- **A study image**: the project's Dockerfile, plus the Zephyr SDK 1.0.1
  (minimal, riscv64 GNU, LLVM), `hal_espressif` (`b16851b`), `mbedtls`,
  `tf-psa-crypto`, esptool 5.6.0. Kept outside the repository.
- **`samples/hello_world` for `xiao_esp32c5/esp32c5/hpcore`**: builds and
  links with GCC 14.3.0 (flash 134 KB, SRAM 58 KB); **fails with the SDK's
  LLVM**, for the two reasons in section 6.
- **The meter app itself** for `xiao_esp32c5/esp32c5/hpcore`, unchanged
  `app/` sources, with appendix A as the overlay, `prj.conf` minus the
  `CONFIG_WHD_WIFI_COUNTRY` line, `-DCONFIG_BUILD_ONLY_NO_BLOBS=y` and
  `-DMETER_CLANG_TIDY=OFF`: compiles and links with **no warnings** under
  the project's `-Wall -Wextra -Wpedantic -Werror`: flash 294 KB, SRAM
  209 KB of 378 KB. The generated config has `CONFIG_WIFI_ESP32`,
  `CONFIG_ILI9341`, `CONFIG_MIPI_DBI_SPI`, `CONFIG_INPUT_GPIO_KEYS`,
  `CONFIG_LED_PWM`, `CONFIG_PWM_LED_ESP32`, `CONFIG_LED_GPIO`,
  `CONFIG_WDT_ESP32`, `CONFIG_FCB`, `CONFIG_TASK_WDT`,
  `CONFIG_SERIAL_ESP32_USB`, `CONFIG_ESP_SIMPLE_BOOT`. `gfx.c`'s
  `BUILD_ASSERT` on RGB_565X passes; the devicetree has the ILI9341 at
  240×320 with `rotation = <90>`.
- **With clang-tidy on**, the same build fails in clang-tidy (section 6).
- **The draft driver patch** (appendix B) applied to the pinned Zephyr
  compiles into the meter's C5 build.
- **`make test`** (documentation check, style check, native_sim tests) on
  this branch: see the end of this section.

**Could not be done here:**

- **The Wi-Fi blobs**: this session's network policy refused downloads in
  the `github.com/<owner>/<repo>/raw/...` form that both hal_espressif and
  hal_infineon use for their blobs (HTTP 403 from the proxy). So the C5
  build used Zephyr's `BUILD_ONLY_NO_BLOBS` (compiles and links the driver,
  but the image won't run Wi-Fi), and the project's own image could only be
  built without its CYW43439 blobs, so **`make pico` wasn't run**. No
  repository file changed, so the Pico build is as before. The step to run
  on a normal network is `west blobs fetch hal_espressif`.
- **Seeed's wiki site** and some other web pages couldn't be fetched
  directly; Seeed's wiki pages were read from their source repository
  ([Seeed-Studio/wiki-documents][seeed-gs-src], `docusaurus-version`,
  `a127af9`), which is what the site is built from.

**Inferred from documentation, not verified:**

- The default band mode is `WIFI_BAND_MODE_AUTO`, so a scan covers 5 GHz
  (Espressif workshop).
- The `channel_bitmap` band-skip semantics in appendix B (header comments).
- SPI above 20 MHz over the GPIO matrix (binding text; the datasheet gives
  only the 80 MHz SPI2 maximum).
- Scan timings (library defaults × channel counts), the meter's actual
  current draw, charging current, TFT module electrical details.
- That the ROM loader leaves the `__noinit` note alone across resets (risk
  6; the datasheet confirms that the resets themselves keep SRAM).

**From the ESP32-C5 datasheet** ([pre-release v0.5][esp-ds], 2025-05-19,
marked preliminary; checked after the first version of this report): the
strapping pins and what each selects, the default eFuse behaviour of GPIO7,
SPI2's 80 MHz maximum and IO_MUX pins, the GPIO drive currents and 45 kΩ
pulls, the Wi-Fi receive and transmit currents, that resets other than a
Chip Reset keep internal memory, and the watchdogs. The figures may change
in the final datasheet.

**`make test` on this branch** (in the project's image built without its
Wi-Fi blobs, which native_sim doesn't use): the documentation check, the
style check and every native_sim suite pass: meter_boot 5, meter_chan 17,
meter_health 2, meter_history 7, meter_keys 2, meter_paging 2,
meter_raster 6, meter_ui 9, meter_event_log 5.

---

## 10. Sources

Zephyr (paths at the project's pin `a915b2f`, the same at v4.5.0 unless
noted):

- [`boards/seeed/xiao_esp32c5`][z-board] and [board docs][z-board-doc]
- [`drivers/wifi/esp32/src/esp_wifi_drv.c`][z-esp-drv]: `esp32_wifi_scan()`, `scan_done_handler()`, `esp32_wifi_reg_domain()`
- `drivers/wifi/esp32/Kconfig.esp32` (`WIFI_ESP32` selects MBEDTLS, PSA_CRYPTO; `HEAP_MEM_POOL_ADD_SIZE_ESP_WIFI`)
- `soc/espressif/esp32c5/Kconfig.caps` (`SOC_ESP32_WIFI_SUPPORT_5G`), `soc/espressif/esp32c5/default.ld` (`.dram0.noinit`, `.rtc_noinit`)
- `dts/riscv/espressif/esp32c5/esp32c5_common.dtsi` (`spi2`, `ledc0`, `wdt0`, `gpio0` with 29 GPIOs, `retainedmem0`)
- `dts/vendor/espressif/partitions_0x2000_default_8M.dtsi` (`storage_partition`)
- [`drivers/display/display_ili9xxx.c`][z-ili-drv], [`dts/bindings/display/ilitek,ili9341.yaml`][z-ili9341], [`ilitek,ili9xxx-common.yaml`][z-ili9xxx-common]
- `drivers/mipi_dbi/mipi_dbi_spi.c` (pixel format ignored on write)
- `dts/bindings/spi/espressif,esp32-spi.yaml` (`dma-enabled`, `use-iomux`, the 20 MHz note)
- `drivers/hwinfo/hwinfo_esp32.c` (reset causes)
- `include/zephyr/net/wifi_mgmt.h` (`wifi_scan_params`, `wifi_scan_result`), `include/zephyr/net/wifi.h` (`wifi_frequency_bands`, `wifi_frequency_bandwidths`)
- `tests/drivers/pwm/pwm_gpio_loopback/socs/esp32c5_hpcore.overlay` (LEDC channel nodes)
- [Zephyr SDK 1.0.1 release][sdk]

Espressif:

- [ESP32-C5 Series Datasheet][esp-ds], pre-release v0.5 (2025-05-19): §2.3.4 GPIO restrictions, chapter 3 Boot Configurations (Tables 3-1 to 3-8), §4.1.2.1 internal memory, §4.1.3.2 reset, §4.1.3.9 watchdogs, §4.2.1.2 SPI controller, §4.2.1.7 LED PWM, Table 5-4 DC characteristics, Tables 5-7, 5-8, 5-11 current consumption, Appendix A Table 7-1 pin overview
- hal_espressif (`b16851b`, the pin's revision): `components/esp_wifi/include/esp_wifi.h` (`esp_wifi_scan_start()` defaults, `esp_wifi_set_band_mode()`), `esp_wifi_types_generic.h` (`wifi_scan_config_t`, `wifi_scan_channel_bitmap_t`, `wifi_ap_record_t`), `components/soc/esp32c5/register/soc/io_mux_reg.h` (SPI2 IO_MUX pins), `zephyr/module.yml` (blobs)
- [ESP-IDF Wi-Fi driver guide, ESP32-C5][esp-wifi-guide] (band modes, country policy, DFS, scan dwell times); source in [esp-idf `docs/en/api-guides/wifi-driver/`][esp-idf-docs-src] (`0e6b480`)
- [ESP-IDF GPIO reference, ESP32-C5][esp-gpio] (GPIO13/14 USB-JTAG, GPIO16–22 flash/PSRAM)
- [`esp_wifi_regulatory.txt`][esp-reg] (country GB, US)
- [ESP32-C5-DevKitC-1 user guide][devkitc] (strapping pins)
- [esptool: boot mode selection, ESP32-C5][esptool-boot]
- [ESP-IDF with ESP32-C5 workshop, lecture 1][esp-workshop] (band mode default)
- [esp32-wifi-lib][esp-wifi-lib] (the Wi-Fi binary libraries)

Seeed:

- [XIAO ESP32-C5 getting started][seeed-gs] (pin map, LEDs, battery, charger, schematic link); [its source][seeed-gs-src]
- Seeed wiki source pages *XIAO ESP32C5 With Zephyr* and *XIAO ESP32C5 WiFi Usage* (flashing with BOOT held; dual-band scan example)
- [CNX Software: XIAO ESP32-C5 announcement][cnx] (battery pads)

[z-board]: https://github.com/zephyrproject-rtos/zephyr/tree/v4.5.0/boards/seeed/xiao_esp32c5
[z-board-doc]: https://docs.zephyrproject.org/latest/boards/seeed/xiao_esp32c5/doc/index.html
[z-esp-drv]: https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/drivers/wifi/esp32/src/esp_wifi_drv.c
[z-ili-drv]: https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/drivers/display/display_ili9xxx.c
[z-ili9341]: https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/dts/bindings/display/ilitek%2Cili9341.yaml
[z-ili9xxx-common]: https://github.com/zephyrproject-rtos/zephyr/blob/v4.5.0/dts/bindings/display/ilitek%2Cili9xxx-common.yaml
[sdk]: https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v1.0.1
[esp-ds]: https://www.espressif.com/documentation/esp32-c5_datasheet_en.pdf
[esp-wifi-guide]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-guides/wifi-driver/overview.html
[esp-idf-docs-src]: https://github.com/espressif/esp-idf/tree/master/docs/en/api-guides/wifi-driver
[esp-gpio]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/peripherals/gpio.html
[esp-reg]: https://github.com/espressif/esp-idf/blob/master/components/esp_wifi/regulatory/esp_wifi_regulatory.txt
[devkitc]: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html
[esptool-boot]: https://docs.espressif.com/projects/esptool/en/latest/esp32c5/advanced-topics/boot-mode-selection.html
[esp-workshop]: https://developer.espressif.com/workshops/esp-idf-with-esp32-c5/lecture-1/
[esp-wifi-lib]: https://github.com/espressif/esp32-wifi-lib
[seeed-gs]: https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/
[seeed-gs-src]: https://github.com/Seeed-Studio/wiki-documents/blob/docusaurus-version/sites/en/docs/Sensor/SeeedStudio_XIAO/SeeedStudio_XIAO_ESP32C5/XIAO_ESP32C5_Getting_Started.md
[cnx]: https://www.cnx-software.com/2026/01/16/xiao-esp32-c5-a-tiny-usb-c-iot-board-with-dual-band-wifi-6-bluetooth-5-0-le-zigbee-and-thread-connectivity/

---

## Appendix A: trial devicetree overlay

What the verified build used, as it would go in
`app/boards/xiao_esp32c5_esp32c5_hpcore.overlay`. It keeps the Display
Pack's node labels (`pack_buttons`, `pack_rgb_led`, `pack_led_*`,
`pack_leds`, `pack_backlight`) so `status_led.c` builds unchanged; a real
port would rename them to something neutral in both overlays.

```dts
/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * TRIAL for the ESP32-C5 port study: Seeed XIAO ESP32-C5 with a 2.2" ILI9341
 * 240x320 SPI TFT, four push buttons to ground and a common-anode RGB LED.
 *
 *   Function          XIAO pad   GPIO
 *   TFT SCK           D8         8     SPI2 SCLK (GPIO matrix)
 *   TFT MOSI (SDI)    D10        10    SPI2 MOSI
 *   TFT CS            D3         7     strapping pin: input on the TFT
 *   TFT DC (RS)       D2         25    strapping pin: input on the TFT
 *   TFT RESET         D6         11    (UART0 TX on the board: disabled)
 *   TFT LED           D9         9     backlight, through a transistor
 *   Button A, B       D0, D1     1, 0
 *   Button X, Y       D4, D5     23, 24 (I2C0 on the board: disabled)
 *   LED red           D7         12    LEDC channel 0
 *   LED green, blue   MTCK, MTDO 4, 5  pads on the back; LEDC channels 1, 2
 */

#include <zephyr/dt-bindings/display/panel.h>
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/dt-bindings/mipi_dbi/mipi_dbi.h>
#include <zephyr/dt-bindings/pwm/pwm.h>

/ {
	chosen {
		zephyr,display = &tft;
	};

	tft_bus: mipi_dbi {
		compatible = "zephyr,mipi-dbi-spi";
		spi-dev = <&spi2>;
		dc-gpios = <&gpio0 25 GPIO_ACTIVE_HIGH>;
		reset-gpios = <&gpio0 11 GPIO_ACTIVE_LOW>;
		write-only;
		#address-cells = <1>;
		#size-cells = <0>;

		tft: ili9341@0 {
			compatible = "ilitek,ili9341";
			reg = <0>;
			mipi-max-frequency = <20000000>;
			mipi-mode = "MIPI_DBI_MODE_SPI_4WIRE";
			width = <240>;
			height = <320>;
			rotation = <90>;
			pixel-format = <PANEL_PIXEL_FORMAT_RGB_565X>;
		};
	};

	pack_buttons: buttons {
		compatible = "gpio-keys";
		debounce-interval-ms = <30>;

		button_a {
			gpios = <&gpio0 1 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "A";
			zephyr,code = <INPUT_KEY_A>;
		};

		button_b {
			gpios = <&gpio0 0 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "B";
			zephyr,code = <INPUT_KEY_B>;
		};

		button_x {
			gpios = <&gpio0 23 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "X";
			zephyr,code = <INPUT_KEY_X>;
		};

		button_y {
			gpios = <&gpio0 24 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			label = "Y";
			zephyr,code = <INPUT_KEY_Y>;
		};
	};

	pack_rgb_led: rgb_led {
		compatible = "pwm-leds";

		pack_led_red: led_red {
			pwms = <&ledc0 0 PWM_HZ(1000) PWM_POLARITY_INVERTED>;
			label = "RGB LED red";
		};

		pack_led_green: led_green {
			pwms = <&ledc0 1 PWM_HZ(1000) PWM_POLARITY_INVERTED>;
			label = "RGB LED green";
		};

		pack_led_blue: led_blue {
			pwms = <&ledc0 2 PWM_HZ(1000) PWM_POLARITY_INVERTED>;
			label = "RGB LED blue";
		};
	};

	pack_leds: leds_ext {
		compatible = "gpio-leds";

		pack_backlight: backlight {
			gpios = <&gpio0 9 GPIO_ACTIVE_HIGH>;
			label = "LCD backlight";
		};
	};
};

&pinctrl {
	spim2_tft: spim2_tft {
		group1 {
			pinmux = <SPIM2_SCLK_GPIO8>, <SPIM2_MOSI_GPIO10>;
			output-low;
		};
	};

	ledc0_rgb: ledc0_rgb {
		group1 {
			pinmux = <LEDC_CH0_GPIO12>, <LEDC_CH1_GPIO4>, <LEDC_CH2_GPIO5>;
			output-enable;
		};
	};
};

&spi2 {
	pinctrl-0 = <&spim2_tft>;
	pinctrl-names = "default";
	cs-gpios = <&gpio0 7 GPIO_ACTIVE_LOW>;
	dma-enabled;
	status = "okay";
};

&ledc0 {
	pinctrl-0 = <&ledc0_rgb>;
	pinctrl-names = "default";
	#address-cells = <1>;
	#size-cells = <0>;
	status = "okay";

	channel0@0 {
		reg = <0x0>;
		timer = <0>;
	};

	channel1@1 {
		reg = <0x1>;
		timer = <0>;
	};

	channel2@2 {
		reg = <0x2>;
		timer = <0>;
	};
};

/* Their pins are buttons, the TFT's reset and the LED here. */
&uart0 {
	status = "disabled";
};

&i2c0 {
	status = "disabled";
};
```

## Appendix B: draft Wi-Fi driver patch

Against `drivers/wifi/esp32/src/esp_wifi_drv.c` at the project's pin.
Compiles; untested on hardware. For `zephyr/patches/zephyr/` with an entry
in `zephyr/patches.yml`, and worth sending upstream.

```diff
diff --git a/drivers/wifi/esp32/src/esp_wifi_drv.c b/drivers/wifi/esp32/src/esp_wifi_drv.c
index ee7e80b94..bcc95c96e 100644
--- a/drivers/wifi/esp32/src/esp_wifi_drv.c
+++ b/drivers/wifi/esp32/src/esp_wifi_drv.c
@@ -1823,9 +1823,26 @@ static int esp32_wifi_scan(const struct device *dev __unused,
 
 	wifi_scan_config_t scan_config = { 0 };
 
+	/* Report hidden networks too, as other Zephyr Wi-Fi drivers do. */
+	scan_config.show_hidden = true;
+
 	if (params) {
 		/* The enum values are same, so, no conversion needed */
 		scan_config.scan_type = params->scan_type;
+		/* 0 keeps the library's default (120 ms active, 360 ms passive). */
+		scan_config.scan_time.active.max = params->dwell_time_active;
+		scan_config.scan_time.passive = params->dwell_time_passive;
+#if defined(CONFIG_SOC_ESP32_WIFI_SUPPORT_5G)
+		/* Bit 0 of each bitmap skips that band; 0 scans all its channels. */
+		if (params->bands != 0U) {
+			if ((params->bands & BIT(WIFI_FREQ_BAND_2_4_GHZ)) == 0U) {
+				scan_config.channel_bitmap.ghz_2_channels = BIT(0);
+			}
+			if ((params->bands & BIT(WIFI_FREQ_BAND_5_GHZ)) == 0U) {
+				scan_config.channel_bitmap.ghz_5_channels = BIT(0);
+			}
+		}
+#endif
 	}
 
 #if defined(CONFIG_ESP32_WIFI_AP_STA_MODE)
```
