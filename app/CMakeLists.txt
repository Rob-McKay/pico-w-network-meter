# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/toolchain.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(network_meter)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/strict.cmake)
meter_strict_warnings(app)

# The hardware-independent core (cmake/meter_core.cmake), then the rest.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/meter_core.cmake)
meter_core(app)

target_sources(app PRIVATE
  src/main.c
  src/scan_task.c
  src/event_log.c
  src/reset_note.c
  src/watchdog.c
  src/scanner.c
  src/ui.c
  src/gfx.c
  src/controls.c
  src/status_led.c
)
# Hooks a board's .conf turns on (app/Kconfig).
target_sources_ifdef(CONFIG_METER_WIFI_REG_DOMAIN app PRIVATE src/wifi_country.c)
target_sources_ifdef(CONFIG_METER_RESET_CAUSE_HWINFO app PRIVATE src/reset_cause.c)

# Target-only code: src/target/<target>/, each file under its own option. It may use
# the app's own headers, in src/.
target_include_directories(app PRIVATE src)
# The Pico's: flow control for the console on its USB port.
target_sources_ifdef(CONFIG_METER_USB_CONSOLE_FLOW app PRIVATE src/target/pico/usb_console.c)
# The Pico's: keeping the Wi-Fi chip out of its deep sleep.
target_sources_ifdef(CONFIG_METER_WIFI_KEEP_AWAKE app PRIVATE src/target/pico/wifi_awake.c)
