/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The debug count of the AIROC driver's buffers in use (wifi_buffers.c),
 * on a pool of the same shape as the driver's.
 */

#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>

#include "wifi_buffers.h"

/* As the driver defines it: 20 buffers of 2400 bytes. Zephyr's definition
 * macro has an empty initializer and can end in an empty statement, which
 * ISO C -Wpedantic flags. Nothing else is exempted.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wc23-extensions"
NET_BUF_POOL_FIXED_DEFINE(test_airoc_pool, 20, 2400, 0, NULL);
#pragma GCC diagnostic pop

/**
 * @brief Buffers taken count as in use; freed ones don't, however freed.
 *
 * The driver frees with net_buf_destroy(), which skips the pool's own
 * accounting; the count must follow it as well as net_buf_unref().
 *
 * @sideeffects Allocates from and frees to the test's pool. errno unchanged.
 */
ZTEST(meter_wifi_buffers, test_count)
{
	struct net_buf *bufs[3];

	zassert_equal(wifi_buffers_in_use(), 0);
	for (size_t i = 0; i < ARRAY_SIZE(bufs); i++) {
		bufs[i] = net_buf_alloc_len(&test_airoc_pool, 1500, K_NO_WAIT);
		zassert_not_null(bufs[i]);
	}
	zassert_equal(wifi_buffers_in_use(), 3);

	net_buf_destroy(bufs[0]);
	zassert_equal(wifi_buffers_in_use(), 2, "freed as the driver does");
	net_buf_unref(bufs[1]);
	zassert_equal(wifi_buffers_in_use(), 1, "freed the usual way");

	bufs[0] = net_buf_alloc_len(&test_airoc_pool, 100, K_NO_WAIT);
	zassert_not_null(bufs[0]);
	zassert_equal(wifi_buffers_in_use(), 2, "a reused buffer counts again");
	net_buf_unref(bufs[0]);
	net_buf_unref(bufs[2]);
	zassert_equal(wifi_buffers_in_use(), 0);
}

ZTEST_SUITE(meter_wifi_buffers, NULL, NULL, NULL, NULL, NULL);
