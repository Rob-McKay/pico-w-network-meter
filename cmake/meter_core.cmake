# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# The meter's hardware-independent core, app/src/core/: channel statistics,
# network history, scan health, paging, boot records, key chords, text
# rasterising and Wi-Fi country codes. It uses no Zephyr device API (only
# utility headers such as sys/util.h), so it builds for every target and is
# unit tested on native_sim. The firmware and every test suite take it from
# this one list.
# Include after find_package(Zephyr).

set(METER_CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../app/src/core)
set(METER_CORE_SOURCES
  ${METER_CORE_DIR}/chan_stats.c
  ${METER_CORE_DIR}/ap_history.c
  ${METER_CORE_DIR}/scan_health.c
  ${METER_CORE_DIR}/paging.c
  ${METER_CORE_DIR}/boot_record.c
  ${METER_CORE_DIR}/keys.c
  ${METER_CORE_DIR}/text_raster.c
  ${METER_CORE_DIR}/country_code.c
  ${METER_CORE_DIR}/wifi_irq_state.c
)

# @brief Build the core into a target, and let its sources include the
#        core's headers by name ("chan_stats.h").
#
# @param target CMake target (Zephyr's app).
#
# @sideeffects Adds METER_CORE_SOURCES to @p target's sources and
#              METER_CORE_DIR to its include directories.
function(meter_core target)
  target_include_directories(${target} PRIVATE ${METER_CORE_DIR})
  target_sources(${target} PRIVATE ${METER_CORE_SOURCES})
endfunction()
