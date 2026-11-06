/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Keeping the Wi-Fi chip out of its deep sleep, with
 * CONFIG_METER_WIFI_KEEP_AWAKE. The Pico W's is in
 * src/target/pico/wifi_awake.c.
 */

#ifndef METER_WIFI_AWAKE_H_
#define METER_WIFI_AWAKE_H_

/**
 * @brief Ask the Wi-Fi driver to keep the chip's bus up between requests,
 *        so the chip stays out of its deep sleep.
 *
 * Built only with CONFIG_METER_WIFI_KEEP_AWAKE; call it under
 * IS_ENABLED(CONFIG_METER_WIFI_KEEP_AWAKE), once the Wi-Fi driver is up.
 *
 * @retval 0 Asked; the driver applies it the next time its thread waits.
 * @retval -ENODEV The driver hasn't set itself up.
 *
 * @sideeffects Sets the Wi-Fi driver's delay before it lets the chip's bus sleep, so from
 *              then on the chip draws more power; logs it. errno unchanged.
 */
int wifi_awake_apply(void);

#endif /* METER_WIFI_AWAKE_H_ */
