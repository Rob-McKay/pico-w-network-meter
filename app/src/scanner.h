/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi scans through Zephyr's Wi-Fi management API (on the Pico W, the
 * CYW43439 with the AIROC driver): each scan's results go into a struct chan_stats.
 */

#ifndef METER_SCANNER_H_
#define METER_SCANNER_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>

#include "chan_stats.h"

/**
 * @brief Find the Wi-Fi interface and listen for scan events.
 *
 * Call once, before scanner_run(). With CONFIG_METER_WIFI_REG_DOMAIN, also
 * sets the Wi-Fi country (wifi_country_apply()); a driver that refuses it
 * keeps its own, and scans still run. With CONFIG_METER_WIFI_KEEP_AWAKE,
 * keeps the Wi-Fi chip out of its deep sleep (wifi_awake_apply()).
 *
 * @retval 0 Ready.
 * @retval -ENODEV No Wi-Fi interface.
 *
 * @sideeffects Sets @c wifi_iface and registers the scan event callback with net_mgmt; logs a
 *              missing interface; those of wifi_country_apply() with
 *              CONFIG_METER_WIFI_REG_DOMAIN, and of wifi_awake_apply() with
 *              CONFIG_METER_WIFI_KEEP_AWAKE. errno unchanged.
 */
int scanner_init(void);

/**
 * @brief Scan all channels and wait for the results.
 *
 * Blocks the calling thread until the scan finishes or @p timeout passes.
 * Results that arrive after a timeout are discarded.
 *
 * @param[out] out The scan's access points, replacing what it held.
 * @param timeout Longest wait for the scan to finish.
 * @retval 0 Scan complete.
 * @retval -ETIMEDOUT Timed out: @p out has what arrived in time.
 * @retval -EIO The driver reported a failed scan.
 * @retval -ENODEV scanner_init() has not succeeded.
 * @retval other Negative errno from the scan request (for example, the AIROC driver returns
 *         -EAGAIN when it can't start a scan, -EINPROGRESS when it thinks one is still
 *         running).
 *
 * @sideeffects Sets and clears the scan target (the scan's results go to @p out), drives the
 *              radio, logs; blocks the calling thread for up to @p timeout. errno unchanged.
 */
int scanner_run(struct chan_stats *out, k_timeout_t timeout);

#endif /* METER_SCANNER_H_ */
