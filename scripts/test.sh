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
for f in channels networks networks-page2 faded; do
	: > "$here/out/screens/$f.ppm"
done

cd "$here"
for t in meter_units ui_render; do
	build_dir="$here/build/test-$t"
	west build -p auto -b native_sim/native/64 -d "$build_dir" "$here/tests/$t"
	"$build_dir/zephyr/zephyr.exe"
done
