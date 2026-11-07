/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/sys/util.h>

#include "chan_stats.h"
#include "recommend.h"

/*
 * Public interface: documented in recommend.h.
 */
uint8_t recommend_5g(const struct chan_stats *s)
{
	return chan_stats_recommend_5g(s,
				       chan_5g_blocks(IS_ENABLED(CONFIG_METER_RECOMMEND_5G_132),
						      IS_ENABLED(CONFIG_METER_RECOMMEND_5G_UPPER)));
}
