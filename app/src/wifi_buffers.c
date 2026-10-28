/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/sflist.h>

#include "wifi_buffers.h"

#ifdef CONFIG_METER_DEBUG_WIFI_BUFFERS

/* The AIROC driver's pool: AIROC_WIFI_TX_PACKET_POOL_COUNT plus
 * AIROC_WIFI_RX_PACKET_POOL_COUNT buffers of AIROC_WIFI_PACKET_POOL_SIZE
 * bytes, the defaults in drivers/wifi/infineon/airoc_wifi.c.
 */
#define AIROC_POOL_COUNT 20U
#define AIROC_POOL_SIZE  2400U

/**
 * @brief Find the AIROC driver's buffer pool among Zephyr's.
 *
 * @return The only pool of its shape, or NULL if none or several are.
 *
 * @sideeffects None; errno unchanged.
 */
static struct net_buf_pool *find_pool(void)
{
	struct net_buf_pool *found = NULL;

	STRUCT_SECTION_FOREACH(net_buf_pool, pool) {
		if (pool->buf_count == AIROC_POOL_COUNT && pool->alloc != NULL &&
		    pool->alloc->max_alloc_size == AIROC_POOL_SIZE) {
			if (found != NULL) {
				return NULL;
			}
			found = pool;
		}
	}

	return found;
}

#endif /* CONFIG_METER_DEBUG_WIFI_BUFFERS */

/*
 * Public interface: documented in wifi_buffers.h.
 *
 * Without CONFIG_METER_DEBUG_WIFI_BUFFERS, only -ENOTSUP is built. In use = all - never
 * handed out (uninit_count) - on the free list. The free list is a
 * k_lifo, whose queue is counted with its own lock held; uninit_count only falls, and a stale
 * read of it errs by a buffer at most.
 */
int wifi_buffers_in_use(void)
{
#ifdef CONFIG_METER_DEBUG_WIFI_BUFFERS
	static struct net_buf_pool *pool;
	unsigned int free_count = 0;
	sys_sfnode_t *node;
	k_spinlock_key_t key;

	if (pool == NULL) {
		pool = find_pool();
		if (pool == NULL) {
			return -ENOENT;
		}
	}
	key = k_spin_lock(&pool->free._queue.lock);
	SYS_SFLIST_FOR_EACH_NODE(&pool->free._queue.data_q, node) {
		free_count++;
	}
	k_spin_unlock(&pool->free._queue.lock, key);

	return (int)pool->buf_count - (int)pool->uninit_count - (int)free_count;
#else
	return -ENOTSUP;
#endif
}
