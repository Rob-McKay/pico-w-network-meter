# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Host-side convenience wrapper: everything runs in the Docker image.
#
#   make image                 build the Docker image for this machine
#                              (arm64 on Apple Silicon, amd64 on PCs)
#   make image-multiarch       build linux/amd64 + linux/arm64 with buildx
#                              (needs a registry: PUSH_TO=registry/name:tag)
#   make pico                  build the firmware for a Pico W with a
#                              Pimoroni Pico Display Pack 2.0:
#                              out/pico/network-meter.uf2 (WIFI_COUNTRY=
#                              sets the regulatory domain: a WHD country
#                              name, default UNITED_KINGDOM)
#                              BUILD_ARGS= passes CMake options, e.g.
#                              BUILD_ARGS=-DCONFIG_METER_LOG_LEVEL_DBG=y
#                              to log every scan result on the console
#                              (options stay set until `make clean`)
#   make xiao                  build the firmware for a Seeed XIAO
#                              ESP32-C5 with an ILI9341 TFT:
#                              out/xiao/network-meter.bin (at 0x2000) and
#                              network-meter-merged.bin (at 0); WIFI_COUNTRY=
#                              is an ISO code here, default GB; BUILD_ARGS=
#                              as for pico
#   make test                  run the unit tests (native_sim) and the
#                              documentation check
#   make shell                 interactive shell in the build container
#
# Flash a Pico W: hold BOOTSEL while plugging it in, then copy
# out/pico/network-meter.uf2 to the RPI-RP2 drive.
# Flash a XIAO ESP32-C5 from the host (Docker on a Mac can't reach USB):
#   esptool --chip esp32c5 write-flash 0x2000 out/xiao/network-meter.bin
# or open out/xiao/network-meter-merged.bin in a browser flasher (at 0).

IMAGE ?= pico-w-network-meter

# Optional: CA certificate of a TLS-intercepting proxy, passed as a build
# secret, and extra docker build flags (e.g. --network host, --build-arg,
# --build-arg BASE_IMAGE=mirror.gcr.io/library/ubuntu:24.04).
EXTRA_CA ?=
DOCKER_BUILD_FLAGS ?=
comma := ,
BUILD_FLAGS = $(DOCKER_BUILD_FLAGS) $(if $(EXTRA_CA),--secret id=extra_ca$(comma)src=$(EXTRA_CA))
# Interactive by default; DOCKER_TTY= for scripted/CI use.
DOCKER_TTY ?= -it
DOCKER_RUN = docker run --rm $(DOCKER_TTY) -v $(CURDIR):/workdir/meter $(IMAGE)

.PHONY: image image-multiarch pico xiao test shell clean

image:
	DOCKER_BUILDKIT=1 docker build $(BUILD_FLAGS) -t $(IMAGE) .

image-multiarch:
	@test -n "$(PUSH_TO)" || { echo "set PUSH_TO=registry/name:tag"; exit 1; }
	docker buildx build $(BUILD_FLAGS) --platform linux/amd64,linux/arm64 \
		-t $(PUSH_TO) --push .

BUILD_ARGS ?=

pico xiao:
	docker run --rm $(DOCKER_TTY) -e WIFI_COUNTRY \
		-v $(CURDIR):/workdir/meter $(IMAGE) scripts/build.sh $@ $(BUILD_ARGS)

test:
	$(DOCKER_RUN) scripts/test.sh

shell:
	$(DOCKER_RUN) bash

clean:
	rm -rf build out
