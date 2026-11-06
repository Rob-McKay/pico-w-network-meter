/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Keeping the CYW43439 out of its save/restore deep sleep.
 *
 * After each transfer the WHD library can keep the chip's bus up for a while before it lets
 * it sleep; when it lets it sleep, it clears the chip's keep-SDIO-on bit and the chip goes
 * into deep sleep, to be woken by setting the bit again before the next request. Every few
 * hours the chip fails to wake properly: its gSPI side answers, but its backplane and
 * firmware don't, and every request times out until the meter restarts
 * (docs/targets/pico.md). With a delay far longer than the time between scans, which every
 * transfer starts again, the bus never sleeps and the chip never goes into deep sleep. A
 * debug build doing this scanned for over 24 hours without a failure, against failures
 * from 16 minutes to a few hours without it.
 *
 * WHD's setter for the delay is declared but not defined upstream: the carried patch
 * zephyr/patches/hal_infineon/whd-delayed-bus-release-setter.patch defines it.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include <whd.h>
#include <whd_bus_common.h>
#include <whd_int.h>

#include "wifi_awake.h"

LOG_MODULE_REGISTER(wifi_awake, CONFIG_METER_LOG_LEVEL);

/** How long WHD keeps the bus up after a transfer, ms: an hour, against scans seconds
 *  apart.
 */
#define BUS_AWAKE_MS (60U * 60U * 1000U)

/**
 * @brief Get the AIROC driver's WHD station interface.
 *
 * Declared here because its header, drivers/wifi/infineon/airoc_wifi.h, needs the driver's
 * own include paths.
 *
 * @return The interface; NULL before the driver has set it up.
 *
 * @sideeffects None; errno unchanged.
 */
whd_interface_t airoc_wifi_get_whd_interface(void);

/*
 * Public interface: documented in wifi_awake.h.
 */
int wifi_awake_apply(void)
{
	whd_interface_t iface = airoc_wifi_get_whd_interface();

	if (iface == NULL || iface->whd_driver == NULL) {
		LOG_ERR("No Wi-Fi driver to keep awake");
		return -ENODEV;
	}
	whd_bus_sdio_wlan_set_delayed_bus_powersave_milliseconds(iface->whd_driver, BUS_AWAKE_MS);
	LOG_INF("Wi-Fi chip kept out of deep sleep (CONFIG_METER_WIFI_KEEP_AWAKE)");

	return 0;
}
