# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/toolchain.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ui_render)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/strict.cmake)
meter_strict_warnings(app)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src)
target_include_directories(app PRIVATE ${APP_SRC})
target_sources(app PRIVATE
  src/main.c
  src/fake_display.c
  ${APP_SRC}/ui.c
  ${APP_SRC}/gfx.c
  ${APP_SRC}/chan_stats.c
  ${APP_SRC}/paging.c
  ${APP_SRC}/boot_record.c
  ${APP_SRC}/text_raster.c
)
