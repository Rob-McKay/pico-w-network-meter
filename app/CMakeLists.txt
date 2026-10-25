# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/toolchain.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_w_network_meter)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/strict.cmake)
meter_strict_warnings(app)

target_sources(app PRIVATE
  src/main.c
  src/chan_stats.c
  src/ap_history.c
  src/scan_task.c
  src/scanner.c
  src/ui.c
  src/gfx.c
  src/text_raster.c
  src/controls.c
  src/status_led.c
)

# Flow control for the console on the USB port (scripts/build.sh).
target_sources_ifdef(CONFIG_USBD_CDC_ACM_CLASS app PRIVATE src/usb_console.c)
