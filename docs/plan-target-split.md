# Plan: separating the Pico-only parts from the shared code

A refactoring plan, so that the meter builds for two targets from one tree:

- **pico**: Raspberry Pi Pico W with a Pimoroni Pico Display Pack 2.0 (today);
- **xiao**: Seeed XIAO ESP32-C5 with a 2.2" ILI9341 TFT, four buttons and an
  RGB LED ([docs/port-esp32c5.md](port-esp32c5.md)).

The code that works on both is kept apart from each target's own parts.
This is a plan only; nothing here has been implemented. It covers the split
and the ESP32-C5 target at 2.4 GHz. The 5 GHz work (port study, section 5)
comes after it and builds on it.

## Where things stand

The port study's trial build showed that the C sources are already
nearly target-neutral. The unchanged `app/src/` compiled for the
ESP32-C5. Everything there talks to hardware through Zephyr's driver
models (display, input, LED, watchdog, flash map, net_mgmt Wi-Fi), so the
Pico-only parts are mostly *configuration*, not code. An inventory of what
is Pico-specific today:

| File | What is Pico-only |
|---|---|
| `app/prj.conf` | `CONFIG_WHD_WIFI_COUNTRY` (AIROC driver; an undefined symbol on the ESP32, which fails the build); comments on the RP2040 watchdog's 8.3 s limit |
| `app/boards/rpi_pico_rp2040_w.overlay` | All of it (already per board, which is right); its node labels `pack_*` are used by `status_led.c` |
| `app/usb-console.conf`, `app/usb-console.overlay` | All of it: CDC-ACM console, Raspberry Pi VID/PID |
| `app/src/usb_console.c` | All of it (already built only with `CONFIG_USBD_CDC_ACM_CLASS`) |
| `app/src/status_led.c` | The node labels `pack_rgb_led`, `pack_led_*`, `pack_leds`, `pack_backlight` |
| `app/src/event_log.c` | `MAX_SECTORS 16`: sized for the Pico's 64 KB partition. **On the XIAO the board's 192 KB partition has 48 sectors, and `flash_area_get_sectors()` returns `-ENOMEM` (`subsys/storage/flash_map/flash_map_layout.c`), so the log would fail to open.** |
| `app/src/watchdog.h/.c` | Comments on the RP2040 driver (`watchdog_stop()`'s reason); the code itself is generic |
| `app/src/scan_task.c`, `app/Kconfig` | Comments that the restart-on-failure path is for an AIROC leak; the code itself is generic |
| `app/src/main.c`, `gfx.h`, `gfx.c`, `ui.h`, `controls.h`, `status_led.h`, `scanner.h`, `reset_note.h` | Comments naming the Display Pack, ST7789V, CYW43439, AIROC or "the Pico" |
| `app/CMakeLists.txt` | `project(pico_w_network_meter)` |
| `cmake/toolchain.cmake` | ATfE for `rpi_pico*`; other boards fall through to Zephyr's default (right for the ESP32, but only implicitly) |
| `scripts/build.sh`, `Makefile` | One target (`pico`), UF2 output, WHD country names, the USB console files |
| `Dockerfile`, `west.yml` | ATfE, hal_rpi_pico, hal_infineon and the CYW43439 blobs only |
| `zephyr/patches.yml` | Two Pico-only patches (boot2 under clang; AIROC hidden networks). They apply harmlessly to a shared tree |
| `README.md`, `docs/USAGE.md`, `docs/quick-reference.*`, `hardware/` | Written for the Pico and Display Pack throughout |

The shared, hardware-independent logic (`chan_stats`, `ap_history`,
`scan_health`, `paging`, `boot_record`, `keys`, `text_raster`, `rgb565.h`)
has no target dependence at all. It is 2.4 GHz-only, but that's the 5 GHz
work, not this split.

## Principles

1. **Zephyr's mechanisms first.** A difference between targets goes, in
   order of preference, in:
   - the **devicetree** (`app/boards/<board>.overlay`, picked up by board
     name);
   - **Kconfig** defaults (`app/boards/<board>.conf`, also picked up by
     board name);
   - a **Kconfig-selected source file**.

   Never `#ifdef CONFIG_SOC_...` inside shared code.
2. **Shared code names no board.** No `pack_*` labels, no "Pico" in shared
   comments except as an example ("on the Pico, ..."). Node labels describe
   the role: `meter_display`, `meter_buttons`, `meter_rgb_led`.
3. **Target code lives under the target's name.** A C file that only makes
   sense on one chip goes in `app/src/target/<target>/`, with a Kconfig
   option that the target's board `.conf` enables. A Zephyr API that any
   driver may implement (regulatory domain, hwinfo reset cause, retention)
   is *shared* code behind a Kconfig option, even if only one target turns
   it on today.
4. **Every step leaves the Pico exactly as it is.** Same `zephyr.dts` and
   `.config` (apart from renamed labels and symbols), same screenshots,
   same behaviour. Each step is its own pull request, checked with
   `make test` and `make pico`, and by comparing the generated files before
   and after.

## Target layout

```
app/
  CMakeLists.txt            shared sources; adds src/target/<t>/ by Kconfig
  Kconfig                   shared METER_ options; rsource "src/target/*/Kconfig"
  prj.conf                  shared configuration only
  boards/
    rpi_pico_rp2040_w.overlay          Display Pack (meter_* labels)       pico
    rpi_pico_rp2040_w.conf             WHD country, RP2040 watchdog,       pico
                                       USB CDC-ACM console
    xiao_esp32c5_esp32c5_hpcore.overlay  ILI9341, buttons, RGB LED,        xiao
                                         64 KB storage_partition
    xiao_esp32c5_esp32c5_hpcore.conf     country, scan timing, flash size  xiao
  src/
    core/                   no Zephyr device APIs; unit tested on native_sim
      chan_stats.c/.h  ap_history.c/.h  scan_health.c/.h  paging.c/.h
      boot_record.c/.h  keys.c/.h  text_raster.c/.h  rgb565.h
    main.c  ui.c/.h  gfx.c/.h  controls.c/.h  status_led.c/.h
    scanner.c/.h  scan_task.c/.h  event_log.c/.h  reset_note.c/.h
    watchdog.c/.h  wifi_country.c/.h (new, optional)
    target/
      pico/   Kconfig  usb_console.c                                       pico
      esp32/  Kconfig  (empty at first; see below)                         xiao
cmake/
  toolchain.cmake           pico: ATfE; xiao: Zephyr SDK (explicit); native_sim: host clang
zephyr/
  patches.yml               all patches, each marked with the target it serves
  patches/zephyr/
    rpi-pico-boot2-clang-target.patch                                      pico
    airoc-hidden-network-results.patch                                     pico
    esp32-wifi-scan-params.patch (port study, appendix B)                  xiao
scripts/build.sh            build.sh pico | xiao
docs/
  targets/pico.md           hardware, pins, flashing, battery              pico
  targets/xiao-esp32c5.md   the same for the XIAO                          xiao
hardware/
  pico/                     today's case, wiring, BOM (moved)              pico
  xiao-esp32c5/             wiring, BOM (case later)                       xiao
```

**The ESP part.** Following principle 3, the ESP32-C5 target needs no
ESP-only C file to start with. Its parts are:

- the board overlay and `.conf`;
- the Wi-Fi driver patch;
- the toolchain, modules and blobs in the image;
- the `xiao` build target;
- its docs and hardware files.

`app/src/target/esp32/` exists, with its `Kconfig`, as the place for code
that turns out to be ESP-only. A candidate: calling
`esp_wifi_set_band_mode()` directly, if the driver patch can't cover it
(port study, risk 1). The Pico has its one target file, `usb_console.c`.

### Shared Kconfig options added

Each defaults to today's Pico behaviour, so `prj.conf` and the Pico's
board `.conf` need not mention it.

| Option | Default | xiao `.conf` | Replaces |
|---|---|---|---|
| `METER_WIFI_COUNTRY` (string, ISO 3166 alpha-2) | `""`: don't set; the driver's own setting applies (WHD on the Pico) | `"GB"` | nothing on the Pico; on the C5, sets the domain with `NET_REQUEST_WIFI_REG_DOMAIN` in a new `wifi_country.c`, built only when non-empty |
| `METER_SCAN_DWELL_PASSIVE_MS`, `METER_SCAN_DWELL_ACTIVE_MS` | `0`: driver default | `130`, `0` | nothing; `scanner.c` passes them in `wifi_scan_params` (needs the driver patch on the C5) |
| `METER_SCAN_TIMEOUT_S` (exists) | 15 | 30 until the dwell is tuned | |
| `METER_RESET_CAUSE_HWINFO` (bool) | `n` | `y` | nothing; records `hwinfo_get_reset_cause()` (power-on, pin, software, watchdog, panic, brown-out) in the boot record. The boot record format needs a field: `boot_record.c` and its tests |
| `METER_RESET_NOTE_RETAINED` (bool) | `n`: `__noinit` | `n` at first; `y` only if hardware testing shows the note lost (port study, risk 6) | splits `reset_note.c`'s storage into a noinit backend (today) and a Zephyr retention backend on `retainedmem0` |
| `METER_EVENT_LOG_MAX_SECTORS` (int) | 16 | 16 (with the 64 KB partition below) | the constant `MAX_SECTORS`; plus a clear `LOG_ERR` naming the partition when it has more sectors than this |
| `METER_USB_CONSOLE_FLOW` (bool, in `src/target/pico/Kconfig`) | `y if USBD_CDC_ACM_CLASS` | not available | `target_sources_ifdef(CONFIG_USBD_CDC_ACM_CLASS ...)` |

**The event log partition.** Rather than raise the sector buffer for 48
sectors, the XIAO overlay shrinks the board's `storage_partition` to the
Pico's 64 KB (`reg = <0x7b0000 DT_SIZE_K(64)>`). The log then behaves
identically on both targets: same capacity, same rotation, same tests.

### Devicetree labels

`status_led.c` uses node labels, so both overlays must provide the same
ones. They are renamed once, in step 1:

| Today (Pico overlay) | Shared name |
|---|---|
| `pack_lcd` (display), `pack_lcd_bus` | `meter_display`, `meter_display_bus` |
| `pack_buttons` | `meter_buttons` |
| `pack_rgb_led`, `pack_led_red/green/blue` | `meter_rgb_led`, `meter_led_red/green/blue` |
| `pack_leds`, `pack_backlight` | `meter_leds`, `meter_backlight` |

`zephyr,display` (chosen) stays as it is. `gfx.c` uses only that.

## Steps

Each step is one pull request. "Pico unchanged" means: build `make pico`
before and after, then diff `build/pico/zephyr/zephyr.dts` and
`build/pico/zephyr/.config`. Only the renames in that step may differ. The
`tests/ui_render` screenshots must also be byte-identical.

**Progress**

| Step | State |
|---|---|
| 1. Neutral names | Done: `refactor-neutral-names`. Pico `zephyr.bin` and `.config` byte-identical, `zephyr.dts` identical apart from the renamed labels and comments, screenshots byte-identical |
| 2–6 | Not started |

**Comparing Pico builds without the CYW43439 blobs.** Where `west blobs
fetch hal_infineon` can't run, as in the cloud sessions this plan was
written in, `CONFIG_BUILD_ONLY_NO_BLOBS` isn't enough: hal_infineon's
CMake reads the blobs' sizes regardless. Write placeholder files at the
two blob paths inside a throwaway container and build with a fixed
`-DBUILD_VERSION`, before and after. The images are not runnable, but
`zephyr.bin` must then be byte-identical for a step that changes no
behaviour:

```sh
b=/workdir/modules/hal/infineon/zephyr/blobs/img/whd/resources
mkdir -p $b/firmware/COMPONENT_43439 $b/clm/COMPONENT_43439/COMPONENT_MURATA-1YN
printf placeholder > $b/firmware/COMPONENT_43439/43439A0.bin
printf placeholder > $b/clm/COMPONENT_43439/COMPONENT_MURATA-1YN/43439A0.clm_blob
scripts/build.sh pico -DBUILD_VERSION=ab-compare
```

Compare `build/pico/zephyr/zephyr.bin` and `.config` with `cmp`, and
`zephyr.dts` with its `/* in ... */` comments stripped (they carry overlay
line numbers). `zephyr.elf` differs whenever a comment moves a line: its
debug information records line numbers.

### 1. Neutral names (Pico only; no behaviour change)

- Rename the node labels (table above) in the Pico overlay and
  `status_led.c`.
- Reword shared comments that name the Pico, Display Pack, ST7789V, AIROC
  or CYW43439 as the only case: `main.c`, `gfx.c/.h`, `ui.h`, `controls.h`,
  `status_led.h`, `scanner.h`, `reset_note.h`, `watchdog.h`,
  `scan_task.c`, `app/Kconfig`. Keep the Pico facts as examples where they
  explain a choice (the AIROC leak behind `METER_RECOVER_AFTER`).
- `project(network_meter)` in `app/CMakeLists.txt`.
- `@sideeffects` blocks that say "restarts the Pico" say "restarts the
  meter".
- *Check*: Pico unchanged; `make test`.
- *Size*: about half a day.

### 2. Pico-only configuration into the board files

- Move `CONFIG_WHD_WIFI_COUNTRY` and the RP2040 watchdog comments from
  `prj.conf` to `app/boards/rpi_pico_rp2040_w.conf`. Leave the watchdog
  *values* shared if both targets can use them, otherwise give each board
  its own.
- Fold `app/usb-console.conf` into the Pico's board `.conf` and
  `app/usb-console.overlay` into its overlay. Then a plain `west build -b
  rpi_pico/rp2040/w app` gives the same firmware as `build.sh`, and
  `build.sh` stops adding `EXTRA_CONF_FILE` and `EXTRA_DTC_OVERLAY_FILE`.
  The alternative is a Zephyr *snippet* (`app/snippets/pico-usb-console`),
  if a UART console build should stay possible. Recommended: fold; the
  Pico build always uses it.
- Move `usb_console.c` to `app/src/target/pico/` with
  `METER_USB_CONSOLE_FLOW`.
- *Check*: Pico unchanged (the `.config` and `zephyr.dts` must match the
  old build with the extra files). `make test`.
- *Size*: half a day to a day.

### 3. Separate the hardware-independent core

- Move the eight core files to `app/src/core/`.
- In CMake, build them as one list (`METER_CORE_SOURCES`, in a small
  `cmake/meter_core.cmake`) that `app/CMakeLists.txt` and the three
  `tests/*/CMakeLists.txt` include, instead of each naming files from
  `../../app/src`.
- `scripts/check-docs.py` and `check-style.sh` find sources by walking
  the tree. Check that they still cover `core/` and `target/`.
- *Check*: Pico unchanged; `make test`; the test lists of the three
  suites are unchanged.
- *Size*: half a day.

### 4. Shared hooks for the second target (off by default)

- Add the Kconfig options in the table above, with `wifi_country.c`, the
  dwell parameters in `scanner.c`, the hwinfo reset cause in
  `boot_record`, the reset-note backend split and
  `METER_EVENT_LOG_MAX_SECTORS`.
- Unit tests for every pure part:
  - the new boot-record field and its description ("reset button",
    "brown-out", ...);
  - the country-code check (two upper-case letters, or "00").
- `tests/event_log` gains a case with a partition larger than the sector
  buffer: it must fail with a logged error, not crash.
- *Check*: Pico unchanged (every option off); `make test` with the new
  tests.
- *Size*: one to one and a half days.

### 5. The ESP32-C5 target

The port study's sections 6 and 7 hold the details. This step:

- **Image.** `west.yml` gains hal_espressif, mbedtls and tf-psa-crypto.
  The Dockerfile gains the Zephyr SDK minimal with the riscv64 GNU
  toolchain, esptool and hal_espressif's Python needs, and the C5 Wi-Fi
  blobs.
- **Toolchain.** `cmake/toolchain.cmake` names the `xiao_esp32c5` case
  explicitly (Zephyr SDK, GCC) instead of falling through.
  `scripts/clang-tidy-cross.py` handles RISC-V/GCC compile commands.
  `docs/CODING.md` rule 0 says which compiler each target uses; GCC is the
  exception, for the ESP32-C5 only.
- **Board files.** `app/boards/xiao_esp32c5_esp32c5_hpcore.overlay`
  (appendix A of the study, with `meter_*` labels and the 64 KB partition)
  and `.conf`:
  - `CONFIG_ESPTOOLPY_FLASHSIZE_8MB`;
  - `METER_WIFI_COUNTRY`, the dwell and timeout settings;
  - `METER_RESET_CAUSE_HWINFO`.
- **Driver patch.** `zephyr/patches/zephyr/esp32-wifi-scan-params.patch`
  and its `patches.yml` entry. Each entry's comment names the target it
  is for.
- **Build target.** `scripts/build.sh xiao` and `make xiao`:
  - output `out/xiao/network-meter.bin` (app at 0x2000) and a merged image
    for browser flashers;
  - `WIFI_COUNTRY` is a WHD name for `pico` and an ISO code for `xiao`; the
    script says which.
- **Build check.** `scripts/test.sh` gains a build of both firmware
  targets after the unit tests. It needs the blobs, or
  `CONFIG_BUILD_ONLY_NO_BLOBS=y` if a blob-free check is wanted. The study
  showed that works for the C5; check whether the AIROC driver builds that
  way too. It would have caught the WHD symbol and the event-log sectors.
- *Check*:
  - Pico unchanged;
  - `make xiao` builds with no warnings and clang-tidy clean;
  - `make test`;
  - on hardware: study plan steps 1–4 (hello world, display, buttons,
    2.4 GHz scans).
- *Size*: one and a half to two days, plus the hardware bring-up the study
  estimates.

### 6. Docs and hardware per target

- `README.md`:
  - **Shared**: what the meter shows, how the figures are worked out, the
    build, the repository layout (with the new tree).
  - **Per target**: a short table linking `docs/targets/pico.md` and
    `docs/targets/xiao-esp32c5.md`, each with hardware, pins, flashing,
    console, battery and known issues. The CYW43439's 2.4 GHz-only limit,
    the AIROC restart and the UF2/BOOTSEL steps move to `pico.md`.
- `docs/USAGE.md` and the quick reference: the buttons, screens and LED
  colours are the same on both. Mention the flashing and battery
  differences in one place.
- `hardware/` → `hardware/pico/` (case, wiring, BOM moved unchanged).
  `hardware/xiao-esp32c5/` holds the study's wiring table and BOM. A second
  case layout in `case.py` is later work.
- `CLAUDE.md` and `docs/CODING.md`:
  - `make test` and both `make pico` and `make xiao` must pass;
  - say what was tested on each target's hardware;
  - when behaviour changes, update the docs of every target it affects.
- *Size*: about a day.

**Total**: about 5–6½ days for steps 1–6. Steps 1–4 are safe to do before
any XIAO hardware arrives: they change nothing on the Pico and can be
merged one at a time.

## After the split

The 5 GHz work (port study, section 5) then touches only shared code
(`core/chan_stats`, `core/ap_history`, `scanner.c`, `ui.c`), switched on by
a capability, not a board name. For example, a Kconfig option
`METER_BAND_5G`, which the XIAO's `.conf` enables and the Pico can't.
The Pico's 2.4 GHz behaviour stays guarded by the existing tests and
screenshots.

## Risks

- **Renames break muscle memory and open branches.** Do steps 1–3 close
  together and early, before the 5 GHz work branches off.
- **"Pico unchanged" depends on a Pico build.** Comparing `.config` and
  `zephyr.dts` needs `make pico`, which needs the CYW43439 blobs. The
  environment used for the port study couldn't download them, so these
  steps must be checked where it can.
- **Two toolchains in one image** make it larger (the study measured about
  +1.3 GB for the riscv64 toolchain, hal_espressif and crypto modules) and
  slower to build. If that becomes a problem, the image can be split into a
  shared base and one layer per target.
- **clang-tidy on GCC builds** is new ground for `clang-tidy-cross.py`; a
  clang-tidy finding that only appears with GCC's headers would need
  care, not a blanket exclusion.
