#!/usr/bin/env bash
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Run the automated tests on native_sim (64-bit host builds), then build
# the firmware for both targets. Runs inside the Docker image.
#
#   tests/meter_units   channel statistics (counts, overlap-weighted
#                       interference, recommendation, dBm arithmetic) and
#                       the text rasteriser, under AddressSanitizer
#   tests/ui_render     the screens (ui.c, gfx.c) drawn on an in-memory
#                       320x240 panel: pixels checked, and screenshots saved
#                       as out/screens/*.ppm
#   tests/event_log     the event log of starts (event_log.c) on native_sim's
#                       simulated flash: kept across a reopen, rotated when
#                       full, started afresh on foreign data; built a second
#                       time allowing fewer sectors than the partition has,
#                       where the log must stay off and the flash untouched
#
# Every build compiles our code with -Wall -Wextra -Wpedantic -Werror and
# runs clang-tidy on it (cmake/strict.cmake), so the builds below fail on
# any warning or finding.
#
# After them, scripts/check-header-includes.py checks that every header
# includes the headers it uses itself, and the firmware is built for both
# targets, Pico W and XIAO ESP32-C5, as scripts/build.sh builds it with its
# defaults: the same warnings and clang-tidy checks, with each board's own
# configuration, which the native_sim builds don't see.
#
# First, scripts/check-docs.py checks that every function has a Doxygen
# comment documenting its parameters and return value, and
# scripts/check-style.sh that the code follows Zephyr's coding style.
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)

"$here/scripts/check-docs.py"
"$here/scripts/check-style.sh"

# The UI test writes its screenshots into existing files only.
mkdir -p "$here/out/screens"
for f in channels networks networks-page2 events faded; do
	: > "$here/out/screens/$f.ppm"
done

## @brief Fail unless a test run passed a given test: a suite whose
#        predicate doesn't hold is skipped, which ztest doesn't count as a
#        failure.
#
# @param 1 The run's output.
# @param 2 The test's name.
#
# @sideeffects Reads the output; prints and exits with status 1 if the test didn't pass.
expect_ran() {
	if ! grep -q "PASS - $2 " "$1"; then
		echo "$2 didn't run: its suite was skipped" >&2
		exit 1
	fi
}

cd "$here"
for t in meter_units ui_render event_log; do
	build_dir="$here/build/test-$t"
	west build -p auto -b native_sim/native/64 -d "$build_dir" "$here/tests/$t"
	if [ "$t" = event_log ]; then
		# The simulated flash goes in a file: keep it in the build directory.
		"$build_dir/zephyr/zephyr.exe" --flash="$build_dir/flash.bin" | tee "$build_dir/run.log"
		expect_ran "$build_dir/run.log" test_dump
	else
		"$build_dir/zephyr/zephyr.exe"
	fi
done

# The event log again, allowing 2 sectors where native_sim's storage partition
# has 4: only the meter_event_log_too_small suite fits that build.
build_dir="$here/build/test-event_log-too-small"
west build -p auto -b native_sim/native/64 -d "$build_dir" "$here/tests/event_log" -- \
	-DCONFIG_METER_EVENT_LOG_MAX_SECTORS=2
"$build_dir/zephyr/zephyr.exe" --flash="$build_dir/flash.bin" | tee "$build_dir/run.log"
expect_ran "$build_dir/run.log" test_left_alone

# Every header includes what it uses (clang-tidy checks only the file being
# compiled, so headers are checked here, with the test builds' flags).
"$here/scripts/check-header-includes.py" "$here"/build/test-meter_units \
	"$here"/build/test-ui_render "$here"/build/test-event_log

# Both firmware targets, with the boards' default settings (as
# scripts/build.sh builds them; its own build directories are left alone).
for board in rpi_pico/rp2040/w xiao_esp32c5/esp32c5/hpcore; do
	west build -p auto -b "$board" -d "$here/build/test-firmware-${board%%/*}" "$here/app"
done
