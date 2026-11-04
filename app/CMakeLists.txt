# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/toolchain.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(network_meter)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/strict.cmake)
meter_strict_warnings(app)

target_sources(app PRIVATE
  src/main.c
  src/chan_stats.c
  src/ap_history.c
  src/scan_task.c
  src/scan_health.c
  src/paging.c
  src/boot_record.c
  src/event_log.c
  src/reset_note.c
  src/watchdog.c
  src/scanner.c
  src/ui.c
  src/gfx.c
  src/text_raster.c
  src/controls.c
  src/keys.c
  src/status_led.c
)

# Target-only code: src/target/<target>/, each file under its own option.
# The Pico's: flow control for the console on its USB port.
target_sources_ifdef(CONFIG_METER_USB_CONSOLE_FLOW app PRIVATE src/target/pico/usb_console.c)
