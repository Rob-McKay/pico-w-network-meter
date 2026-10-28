#!/usr/bin/env bash
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Run the automated tests on native_sim (64-bit host builds). Runs inside
# the Docker image.
#
#   tests/meter_units   channel statistics (counts, overlap-weighted
#                       interference, recommendation, dBm arithmetic) and
#                       the text rasteriser, under AddressSanitizer
#   tests/ui_render     the screens (ui.c, gfx.c) drawn on an in-memory
#                       320x240 panel: pixels checked, and screenshots saved
#                       as out/screens/*.ppm
#   tests/event_log     the event log of starts (event_log.c) on native_sim's
#                       simulated flash: kept across a reopen, rotated when
#                       full, started afresh on foreign data
#   tests/wifi_buffers  the debug count of the Wi-Fi driver's buffers in use
#                       (wifi_buffers.c), on a pool shaped like the driver's
#
# Every build compiles our code with -Wall -Wextra -Wpedantic -Werror and
# runs clang-tidy on it (cmake/strict.cmake), so the builds below fail on
# any warning or finding.
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

cd "$here"
for t in meter_units ui_render event_log wifi_buffers; do
	build_dir="$here/build/test-$t"
	west build -p auto -b native_sim/native/64 -d "$build_dir" "$here/tests/$t"
	if [ "$t" = event_log ]; then
		# The simulated flash goes in a file: keep it in the build directory.
		"$build_dir/zephyr/zephyr.exe" --flash="$build_dir/flash.bin"
	else
		"$build_dir/zephyr/zephyr.exe"
	fi
done
