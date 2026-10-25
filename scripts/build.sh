#!/usr/bin/env bash
# Build the network meter firmware. Runs inside the Docker image.
#
#   scripts/build.sh [pico] [extra CMake args...]
#
#   e.g. scripts/build.sh pico -DCONFIG_METER_SCAN_INTERVAL_S=30
#
#   pico            Raspberry Pi Pico W with a Pimoroni Pico Display Pack 2.0
#
# WIFI_COUNTRY sets the Wi-Fi regulatory domain, which decides the channels
# scanned: a WHD country name without its WHD_COUNTRY_ prefix, e.g.
# UNITED_KINGDOM (the default), UNITED_STATES, GERMANY, JAPAN. The names are
# in the Infineon WHD's whd_types.h (whd_country_code_t).
#
# The firmware ends up in out/<variant>/network-meter.uf2: hold BOOTSEL
# while plugging the Pico W in and copy it to the RPI-RP2 drive.
set -euo pipefail

target=${1:-pico}
shift || true

here=$(cd "$(dirname "$0")/.." && pwd)

case "$target" in
  pico) board=rpi_pico/rp2040/w ;;
  *) echo "unknown target: $target" >&2; exit 2 ;;
esac

country=${WIFI_COUNTRY:-UNITED_KINGDOM}
if [[ ! $country =~ ^[A-Z_]+$ ]]; then
  echo "WIFI_COUNTRY: a WHD country name such as UNITED_KINGDOM" >&2
  exit 2
fi

build_dir="$here/build/$target"
west build -p auto -b "$board" -d "$build_dir" "$here/app" -- \
  "-DCONFIG_WHD_WIFI_COUNTRY=\"WHD_COUNTRY_${country}\"" "$@"

mkdir -p "$here/out/$target"
cp "$build_dir/zephyr/zephyr.uf2" "$here/out/$target/network-meter.uf2"
cp "$build_dir/zephyr/zephyr.elf" "$here/out/$target/network-meter.elf"
echo "Firmware: out/$target/network-meter.uf2"
