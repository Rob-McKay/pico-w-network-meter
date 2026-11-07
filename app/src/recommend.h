/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The channel the meter recommends, under the build's settings: one place
 * for the choice the screen, the LED and the console all show.
 */

#ifndef METER_RECOMMEND_H_
#define METER_RECOMMEND_H_

#include <stdint.h>

#include "chan_stats.h"

/**
 * @brief Pick the 80 MHz block to recommend on 5 GHz.
 *
 * From the blocks CONFIG_METER_RECOMMEND_5G_132 and
 * CONFIG_METER_RECOMMEND_5G_UPPER allow (chan_5g_blocks()).
 *
 * @param s The scan.
 * @return The block's first channel (chan_stats_recommend_5g()).
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t recommend_5g(const struct chan_stats *s);

#endif /* METER_RECOMMEND_H_ */
