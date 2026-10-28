/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debugging the AIROC driver's packet buffer leak: counts the buffers of
 * its pool (airoc_pool) in use, with CONFIG_METER_DEBUG_WIFI_BUFFERS. The
 * driver keeps the pool private, so it is found among Zephyr's buffer pools
 * by its shape, and its free list counted. That reads net_buf and k_queue
 * internals: fine for a debug build of a pinned Zephyr, not for keeps.
 */

#ifndef METER_WIFI_BUFFERS_H_
#define METER_WIFI_BUFFERS_H_

/**
 * @brief Count the AIROC driver's packet buffers in use.
 *
 * @return How many of its pool's buffers are allocated; -ENOENT if the pool
 *         can't be told apart from the others; -ENOTSUP without
 *         CONFIG_METER_DEBUG_WIFI_BUFFERS.
 *
 * @sideeffects Reads the pool's free list with its lock held. errno unchanged.
 */
int wifi_buffers_in_use(void);

#endif /* METER_WIFI_BUFFERS_H_ */
