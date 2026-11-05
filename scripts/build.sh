#!/usr/bin/env bash
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Build the network meter firmware. Runs inside the Docker image.
#
#   scripts/build.sh [pico|xiao] [extra CMake args...]
#
#   e.g. scripts/build.sh pico -DCONFIG_METER_SCAN_INTERVAL_S=30
#
#   pico   Raspberry Pi Pico W with a Pimoroni Pico Display Pack 2.0
#   xiao   Seeed XIAO ESP32-C5 with a 2.2" ILI9341 TFT, four buttons and an
#          RGB LED (docs/port-esp32c5.md)
#
# WIFI_COUNTRY sets the Wi-Fi regulatory domain, which decides the channels
# scanned. Its form depends on the target's Wi-Fi driver:
#
#   pico   a WHD country name without its WHD_COUNTRY_ prefix, e.g.
#          UNITED_KINGDOM (the default), UNITED_STATES, GERMANY, JAPAN. The
#          names are in the Infineon WHD's whd_types.h (whd_country_code_t).
#   xiao   an ISO 3166 alpha-2 code, e.g. GB (the default), US, DE, JP, or
#          00 for the world-safe set.
#
# The firmware ends up in out/<target>/:
#
#   pico   network-meter.uf2: hold BOOTSEL while plugging the Pico W in and
#          copy it to the RPI-RP2 drive. The console (log output) is a USB
#          serial port on the Pico's own USB socket: /dev/cu.usbmodem<N> on
#          a Mac, /dev/ttyACM<N> on Linux, at any baud rate.
#   xiao   network-meter.bin, the application image, for flash address
#          0x2000 (esptool --chip esp32c5 write-flash 0x2000
#          network-meter.bin), and network-meter-merged.bin, the same padded
#          to start at 0, for browser flashers. The console is the C5's USB
#          Serial/JTAG port on the USB-C socket.
#
# Both also get network-meter.elf, for debugging.
set -euo pipefail

target=${1:-pico}
shift || true

here=$(cd "$(dirname "$0")/.." && pwd)

case "$target" in
  pico)
    board=rpi_pico/rp2040/w
    country=${WIFI_COUNTRY:-UNITED_KINGDOM}
    if [[ ! $country =~ ^[A-Z_]+$ ]]; then
      echo "WIFI_COUNTRY: for pico, a WHD country name such as UNITED_KINGDOM" >&2
      exit 2
    fi
    country_arg="-DCONFIG_WHD_WIFI_COUNTRY=\"WHD_COUNTRY_${country}\""
    ;;
  xiao)
    board=xiao_esp32c5/esp32c5/hpcore
    country=${WIFI_COUNTRY:-GB}
    if [[ ! $country =~ ^([A-Z]{2}|00)$ ]]; then
      echo "WIFI_COUNTRY: for xiao, an ISO 3166 alpha-2 code such as GB, or 00" >&2
      exit 2
    fi
    country_arg="-DCONFIG_METER_WIFI_COUNTRY=\"${country}\""
    ;;
  *) echo "unknown target: $target (pico or xiao)" >&2; exit 2 ;;
esac

build_dir="$here/build/$target"
# The board's own settings (the Pico's USB console, the XIAO's TFT and
# flash size, ...) come from app/boards/<board>.conf and .overlay, which
# Zephyr adds by board name.
west build -p auto -b "$board" -d "$build_dir" "$here/app" -- "$country_arg" "$@"

out="$here/out/$target"
mkdir -p "$out"
cp "$build_dir/zephyr/zephyr.elf" "$out/network-meter.elf"
case "$target" in
  pico)
    cp "$build_dir/zephyr/zephyr.uf2" "$out/network-meter.uf2"
    echo "Firmware: out/$target/network-meter.uf2"
    ;;
  xiao)
    # Simple boot (no MCUboot): the ROM loads the application image
    # straight from CONFIG_BOOTLOADER_OFFSET_IN_FLASH (0x2000), where west
    # flash writes it. The build already writes the flash mode, speed and
    # size into the image's header, so the merged image is that image after
    # erased flash (0xFF) up to the offset. esptool merge-bin would give the
    # same bytes, but it always prints a hint to use --format hex, which
    # browser flashers don't take.
    offset=$(sed -n 's/^CONFIG_BOOTLOADER_OFFSET_IN_FLASH=\(0x[0-9a-fA-F]*\)$/\1/p' \
      "$build_dir/zephyr/.config")
    if [[ -z $offset ]]; then
      echo "CONFIG_BOOTLOADER_OFFSET_IN_FLASH not found in the build's .config" >&2
      exit 1
    fi
    cp "$build_dir/zephyr/zephyr.bin" "$out/network-meter.bin"
    { head -c "$((offset))" /dev/zero | tr '\0' '\377'; cat "$out/network-meter.bin"; } \
      > "$out/network-meter-merged.bin"
    echo "Firmware: out/$target/network-meter.bin (at 0x2000)," \
      "out/$target/network-meter-merged.bin (at 0)"
    ;;
esac
