/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/toolchain.h>

#include "country_code.h"
#include "wifi_country.h"

LOG_MODULE_REGISTER(wifi_country, CONFIG_METER_LOG_LEVEL);

/* A wrong length is caught at build time; the letters at run time. */
BUILD_ASSERT(sizeof(CONFIG_METER_WIFI_COUNTRY) == WIFI_COUNTRY_CODE_LEN + 1U,
	     "CONFIG_METER_WIFI_COUNTRY: two letters, such as \"GB\"");

/*
 * Public interface: documented in wifi_country.h.
 */
int wifi_country_apply(struct net_if *iface)
{
	struct wifi_reg_domain domain = {
		.oper = WIFI_MGMT_SET,
		.force = true,
	};
	int ret;

	if (!country_code_valid(CONFIG_METER_WIFI_COUNTRY)) {
		LOG_ERR("CONFIG_METER_WIFI_COUNTRY \"%s\" isn't a country code",
			CONFIG_METER_WIFI_COUNTRY);
		return -EINVAL;
	}
	memcpy(domain.country_code, CONFIG_METER_WIFI_COUNTRY, WIFI_COUNTRY_CODE_LEN);
	ret = net_mgmt(NET_REQUEST_WIFI_REG_DOMAIN, iface, &domain, sizeof(domain));
	if (ret != 0) {
		LOG_ERR("Wi-Fi country %s refused (%d): the driver keeps its own",
			CONFIG_METER_WIFI_COUNTRY, ret);
		return ret;
	}
	LOG_INF("Wi-Fi country %s", CONFIG_METER_WIFI_COUNTRY);

	return 0;
}
