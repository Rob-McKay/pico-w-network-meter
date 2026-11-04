/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Wi-Fi regulatory domain, set from CONFIG_METER_WIFI_COUNTRY through
 * Zephyr's Wi-Fi management API (NET_REQUEST_WIFI_REG_DOMAIN). Built with
 * CONFIG_METER_WIFI_REG_DOMAIN, which a non-empty
 * CONFIG_METER_WIFI_COUNTRY sets.
 */

#ifndef METER_WIFI_COUNTRY_H_
#define METER_WIFI_COUNTRY_H_

#include <zephyr/net/net_if.h>

/**
 * @brief Ask the Wi-Fi driver for CONFIG_METER_WIFI_COUNTRY's regulatory
 *        domain, which decides the channels it scans.
 *
 * The domain is forced: the driver doesn't follow what nearby access
 * points advertise (802.11d) instead.
 *
 * @param iface The Wi-Fi interface.
 * @retval 0 Set.
 * @retval -EINVAL CONFIG_METER_WIFI_COUNTRY isn't a country code (country_code_valid()).
 * @retval <0 Other negative errno from the driver; it keeps its own domain.
 *
 * @sideeffects Sets the Wi-Fi driver's regulatory domain; logs the outcome. errno unchanged.
 */
int wifi_country_apply(struct net_if *iface);

#endif /* METER_WIFI_COUNTRY_H_ */
