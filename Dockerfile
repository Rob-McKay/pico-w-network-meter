# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Build environment for the network meter's two targets: the Raspberry Pi
# Pico W (RP2040 + CYW43439, with a Pimoroni Pico Display Pack 2.0) and the
# Seeed XIAO ESP32-C5 (with an ILI9341 TFT).
#
# Contains Arm Toolchain for Embedded (ATfE, Arm's clang/LLVM bare-metal
# toolchain with picolibc) for the Pico firmware, the Zephyr SDK's RISC-V
# GCC for the ESP32-C5 firmware (Espressif's SoC support builds only with
# GCC), the host's clang for the native_sim unit tests, and a west
# workspace with Zephyr pinned by west.yml plus the two chips' Wi-Fi blobs
# and esptool (cmake/toolchain.cmake picks the compiler). Mount this
# repository at /workdir/meter and use the scripts in scripts/ (or `make`
# on the host).
#
# Multi-arch: builds natively for linux/amd64 (PCs, CI) and linux/arm64
# (Apple Silicon Macs, Arm Linux hosts); the matching ATfE and Zephyr SDK
# host builds are picked from TARGETARCH.
#
# Behind a TLS-intercepting proxy, pass its CA certificate as a build secret
# (docker build --secret id=extra_ca,src=proxy-ca.crt ...) along with the
# usual HTTP(S)_PROXY build args; see `make image EXTRA_CA=...`.

ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE}

ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

# - clang, libclang-rt-18-dev, llvm: the native_sim unit tests (compiler,
#   AddressSanitizer and coverage runtimes)
# - device-tree-compiler: Zephyr's extra devicetree checks
# - doxygen: the documentation check (scripts/check-docs.py)
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget git xz-utils file make cmake ninja-build \
        device-tree-compiler python3 python3-venv \
        clang libclang-rt-18-dev llvm doxygen \
    && rm -rf /var/lib/apt/lists/*

# Optional extra CA certificate (see top of file). git, wget and curl use the
# system store; point pip/requests at it as well.
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then \
        cp /run/secrets/extra_ca /usr/local/share/ca-certificates/extra_ca.crt \
        && update-ca-certificates; \
    fi
ENV PIP_CERT=/etc/ssl/certs/ca-certificates.crt \
    REQUESTS_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt

# Python tooling in a venv (Ubuntu 24.04 forbids system-wide pip installs).
# clang-tidy comes from PyPI: the build needs 20 or later (.clang-tidy's
# banned-function list), and Ubuntu 24.04 ships 18. clang-format checks the
# code against Zephyr's .clang-format (scripts/check-style.sh).
ENV VIRTUAL_ENV=/opt/venv \
    PATH=/opt/venv/bin:$PATH
RUN python3 -m venv $VIRTUAL_ENV \
    && pip install --no-cache-dir west clang-tidy==23.1.3 clang-format==20.1.8

# Arm Toolchain for Embedded (LLVM, picolibc) builds the Pico W firmware
# (cmake/toolchain.cmake); its armv6m multilib covers the RP2040's
# Cortex-M0+. Checksums from the release's .sha256 files.
ARG ATFE_VERSION=23.1.0
RUN case "${TARGETARCH:-$(dpkg --print-architecture)}" in \
        amd64) atfe_host=x86_64 \
               atfe_sha=a7be511613af15151c93961ad39c8cf9ae6889a453c8b547f42f4051a416dc8e ;; \
        arm64) atfe_host=AArch64 \
               atfe_sha=df3b055d05659d8fda710d820e6c1ceff7bca3234c5067abe82a16e781d8de99 ;; \
        *) echo "unsupported architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && atfe=ATfE-${ATFE_VERSION}-Linux-${atfe_host} \
    && cd /opt \
    && wget -q https://github.com/arm/arm-toolchain/releases/download/release-${ATFE_VERSION}-ATfE/${atfe}.tar.xz \
    && echo "${atfe_sha}  ${atfe}.tar.xz" | sha256sum -c - \
    && tar xf ${atfe}.tar.xz \
    && rm ${atfe}.tar.xz \
    && mv ${atfe} atfe-${ATFE_VERSION}
ENV ATFE_PATH=/opt/atfe-${ATFE_VERSION}

# The Zephyr SDK, minimal, with only its RISC-V GNU toolchain (GCC 14.3,
# picolibc), for the ESP32-C5 (cmake/toolchain.cmake). 1.0.1 is the
# version the pinned Zephyr asks for (zephyr/SDK_VERSION). Checked against
# the release's sha256.sum. The SDK's LLVM is left out: hal_espressif
# doesn't build with it.
ARG ZEPHYR_SDK_VERSION=1.0.1
RUN case "${TARGETARCH:-$(dpkg --print-architecture)}" in \
        amd64) sdk_host=x86_64 ;; \
        arm64) sdk_host=aarch64 ;; \
        *) echo "unsupported architecture: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && base=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION} \
    && sdk=zephyr-sdk-${ZEPHYR_SDK_VERSION} \
    && cd /opt \
    && wget -q $base/sha256.sum \
    && for f in ${sdk}_linux-${sdk_host}_minimal.tar.xz \
                toolchain_gnu_linux-${sdk_host}_riscv64-zephyr-elf.tar.xz; do \
           wget -q $base/$f && grep " $f\$" sha256.sum | sha256sum -c - || exit 1; \
       done \
    && tar xf ${sdk}_linux-${sdk_host}_minimal.tar.xz \
    && mkdir -p ${sdk}/gnu \
    && tar xf toolchain_gnu_linux-${sdk_host}_riscv64-zephyr-elf.tar.xz -C ${sdk}/gnu \
    && rm -f *.tar.xz sha256.sum
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}

# West workspace. Only west.yml is copied so that source edits don't
# invalidate this (slow) layer; the full repo is mounted at run time.
WORKDIR /workdir
COPY west.yml /workdir/meter/west.yml
RUN git config --global --add safe.directory '*' \
    && west init -l meter \
    && west update --narrow --fetch-opt=--depth=1 \
    && pip install --no-cache-dir -r zephyr/scripts/requirements-base.txt \
        -r modules/hal/espressif/zephyr/requirements.txt \
    && west zephyr-export
# Ubuntu's ccache (4.9) is older than Zephyr's minimum (4.12), so Zephyr
# would only report it unsuitable: tell it not to look.
ENV ZEPHYR_BASE=/workdir/zephyr \
    USE_CCACHE=0

# Patches carried until they are merged upstream (zephyr/patches.yml).
# A layer of its own, so changing a patch doesn't re-fetch the workspace.
COPY zephyr/patches.yml /workdir/meter/zephyr/patches.yml
COPY zephyr/patches /workdir/meter/zephyr/patches
RUN west patch apply

# CYW43439 firmware and CLM (regulatory) blob for the AIROC Wi-Fi driver.
# hal_infineon lists them as west blobs, fetched and checked against their
# sha256 sums; only the two the Pico W build links in are fetched.
RUN west blobs fetch hal_infineon --auto-accept \
        --allow-regex '.*/COMPONENT_43439/(43439A0\.bin|COMPONENT_MURATA-1YN/43439A0\.clm_blob)$'

# Espressif's Wi-Fi, PHY, coexistence and Bluetooth libraries for the
# ESP32-C5, also west blobs checked against their sha256 sums. All of
# hal_espressif's: Zephyr's Wi-Fi CMake checks every blob of the module,
# for every ESP chip, not just the ones the XIAO build links.
RUN west blobs fetch hal_espressif --auto-accept

WORKDIR /workdir/meter
CMD ["/bin/bash"]
