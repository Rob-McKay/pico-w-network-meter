/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Counts scans that fail in a row, to tell when the Wi-Fi driver has
 * stopped working and the meter must restart to recover. Hardware
 * independent, so it is unit tested on native_sim.
 */

#ifndef METER_SCAN_HEALTH_H_
#define METER_SCAN_HEALTH_H_

#include <stdbool.h>

/* Failed scans in a row. */
struct scan_health {
	unsigned int failures; /**< since the last scan that worked */
};

/**
 * @brief Start counting afresh.
 *
 * @param[out] h Counter.
 *
 * @sideeffects None (writes only through @p h); errno unchanged.
 */
void scan_health_reset(struct scan_health *h);

/**
 * @brief Record a scan's outcome.
 *
 * A scan that worked, even one that heard nothing, ends a run of failures.
 *
 * @param[in,out] h Counter.
 * @param ret The scan's outcome: 0, or a negative errno if it failed.
 * @param limit Failures in a row that call for a restart; 0 never does.
 * @return True once @p limit scans in a row have failed.
 *
 * @sideeffects None (writes only through @p h); errno unchanged.
 */
bool scan_health_record(struct scan_health *h, int ret, unsigned int limit);

#endif /* METER_SCAN_HEALTH_H_ */
