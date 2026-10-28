/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The event log (event_log.c) on native_sim's simulated flash: it keeps
 * records across a reopen, as across a restart; erases its oldest sector
 * when full; and starts afresh on flash that holds something else.
 */

#include <string.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include "boot_record.h"
#include "event_log.h"

#define LOG_PARTITION PARTITION_ID(storage_partition)

/**
 * @brief Erase the log's partition before each test.
 *
 * @param fixture Unused.
 *
 * @sideeffects Erases the simulated flash's storage partition. errno unchanged.
 */
static void erase(void *fixture)
{
	const struct flash_area *fa;

	ARG_UNUSED(fixture);
	zassert_ok(flash_area_open(LOG_PARTITION, &fa));
	zassert_ok(flash_area_erase(fa, 0, fa->fa_size));
	flash_area_close(fa);
}

/**
 * @brief Add a start to the log, as the meter does at start-up.
 *
 * @param cause Why it started.
 *
 * @sideeffects Writes the simulated flash (event_log_add()). errno unchanged.
 */
static void add_start(enum boot_cause cause)
{
	struct boot_record r = {
		.boot = event_log_last_boot() + 1U,
		.cause = (uint8_t)cause,
		.uptime_s = 60,
	};

	zassert_ok(event_log_add(&r));
}

/**
 * @brief An empty partition gives an empty log.
 *
 * @sideeffects Reads the simulated flash. errno unchanged.
 */
ZTEST(meter_event_log, test_empty)
{
	zassert_ok(event_log_init());
	zassert_equal(event_log_count(), 0);
	zassert_equal(event_log_last_boot(), 0);
	zassert_is_null(event_log_get(0));
}

/**
 * @brief Records survive reopening the log, as across a restart, and come
 *        back newest first.
 *
 * @sideeffects Writes and reads the simulated flash. errno unchanged.
 */
ZTEST(meter_event_log, test_reopen)
{
	zassert_ok(event_log_init());
	add_start(BOOT_POWER_ON);
	add_start(BOOT_WIFI_RESTART);
	add_start(BOOT_CRASH);

	zassert_ok(event_log_init());
	zassert_equal(event_log_count(), 3);
	zassert_equal(event_log_last_boot(), 3);
	zassert_equal(event_log_get(0)->boot, 3, "newest first");
	zassert_equal(event_log_get(0)->cause, BOOT_CRASH);
	zassert_equal(event_log_get(2)->cause, BOOT_POWER_ON);
	zassert_is_null(event_log_get(3));
}

/**
 * @brief A full log erases its oldest sector and carries on; the boot
 *        numbers carry on too, and RAM keeps the newest EVENT_LOG_RECENT.
 *
 * @sideeffects Writes, erases and reads the simulated flash. errno unchanged.
 */
ZTEST(meter_event_log, test_full)
{
	const uint32_t starts = 2000;

	zassert_ok(event_log_init());
	for (uint32_t i = 0; i < starts; i++) {
		add_start(BOOT_POWER_ON);
	}
	zassert_equal(event_log_last_boot(), starts);
	zassert_equal(event_log_count(), EVENT_LOG_RECENT);

	zassert_ok(event_log_init());
	zassert_equal(event_log_last_boot(), starts, "numbering survives the rotation");
	zassert_equal(event_log_count(), EVENT_LOG_RECENT);
	zassert_equal(event_log_get(0)->boot, starts);
	zassert_equal(event_log_get(EVENT_LOG_RECENT - 1U)->boot, starts - EVENT_LOG_RECENT + 1U);
}

/**
 * @brief Flash holding something else is erased and a new log started.
 *
 * @sideeffects Writes, erases and reads the simulated flash. errno unchanged.
 */
ZTEST(meter_event_log, test_foreign_flash)
{
	const struct flash_area *fa;
	uint8_t junk[64];

	(void)memset(junk, 0x5a, sizeof(junk));
	zassert_ok(flash_area_open(LOG_PARTITION, &fa));
	zassert_ok(flash_area_write(fa, 0, junk, sizeof(junk)));
	flash_area_close(fa);

	zassert_ok(event_log_init());
	zassert_equal(event_log_count(), 0);
	add_start(BOOT_POWER_ON);
	zassert_ok(event_log_init());
	zassert_equal(event_log_count(), 1);
}

/**
 * @brief Count the records a dump gives, and check they come oldest first
 *        (event_log_dump() function).
 *
 * @param r The record.
 * @param arg The previous boot number seen (uint32_t), updated.
 *
 * @sideeffects Writes through @p arg. errno unchanged.
 */
static void check_order(const struct boot_record *r, void *arg)
{
	uint32_t *previous = arg;

	zassert_true(r->boot > *previous, "oldest first");
	*previous = r->boot;
}

/**
 * @brief A dump gives every record in flash, oldest first, not just the
 *        newest kept in RAM.
 *
 * @sideeffects Writes, erases and reads the simulated flash. errno unchanged.
 */
ZTEST(meter_event_log, test_dump)
{
	uint32_t previous = 0;
	int n;

	zassert_ok(event_log_init());
	for (uint32_t i = 0; i < 100; i++) {
		add_start(BOOT_WIFI_RESTART);
	}
	n = event_log_dump(check_order, &previous);
	zassert_equal(n, 100, "all of them: more than the %u in RAM", EVENT_LOG_RECENT);
	zassert_equal(previous, 100, "ends with the newest");
}

ZTEST_SUITE(meter_event_log, NULL, NULL, erase, NULL, NULL);
