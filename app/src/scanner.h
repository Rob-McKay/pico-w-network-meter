/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi scans on the Pico W's CYW43439, through Zephyr's Wi-Fi management
 * API: each scan's results go into a struct chan_stats.
 */

#ifndef METER_SCANNER_H_
#define METER_SCANNER_H_

#include <zephyr/kernel.h>

#include "chan_stats.h"

/**
 * @brief Find the Wi-Fi interface and listen for scan events.
 *
 * Call once, before scanner_run().
 *
 * @retval 0 Ready.
 * @retval -ENODEV No Wi-Fi interface.
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
 * @retval -EAGAIN Timed out: @p out has what arrived in time.
 * @retval -EIO The driver reported a failed scan.
 * @retval other Negative errno from the scan request.
 */
int scanner_run(struct chan_stats *out, k_timeout_t timeout);

#endif /* METER_SCANNER_H_ */
