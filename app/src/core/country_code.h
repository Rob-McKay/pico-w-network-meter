/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi country codes, as CONFIG_METER_WIFI_COUNTRY gives them to the
 * driver (wifi_country.c). Hardware independent, so it is unit tested on
 * native_sim.
 */

#ifndef METER_COUNTRY_CODE_H_
#define METER_COUNTRY_CODE_H_

#include <stdbool.h>

/**
 * @brief Tell whether a string is a country code a Wi-Fi driver takes.
 *
 * Two upper-case ASCII letters (ISO 3166 alpha-2, such as "GB"), or "00",
 * Zephyr's code for the world-safe regulatory domain. Whether the letters
 * name a real country is the driver's to judge.
 *
 * @param code NUL-terminated string, or NULL.
 * @return True if it has that form.
 *
 * @sideeffects None; errno unchanged.
 */
bool country_code_valid(const char *code);

#endif /* METER_COUNTRY_CODE_H_ */
