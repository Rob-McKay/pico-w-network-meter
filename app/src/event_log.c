/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/fs/fcb.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "event_log.h"

LOG_MODULE_REGISTER(event_log, CONFIG_METER_LOG_LEVEL);

#define LOG_PARTITION PARTITION_ID(storage_partition)
/* The FCB's magic number: "MTRL". */
#define FCB_MAGIC     0x4d54524cU
/* The FCB's format version: the records' own version is inside them. */
#define FCB_VERSION   1U
/* Most flash sectors the log uses. */
#define MAX_SECTORS   16U

/* The log in flash, and a copy of its newest records in RAM: a ring, the
 * oldest at recent_start. Main thread only.
 */
static struct fcb fcb;
static struct flash_sector sectors[MAX_SECTORS];
static bool log_open;
static struct boot_record recent[EVENT_LOG_RECENT];
static size_t recent_start;
static size_t recent_count;
static uint32_t last_boot;

/**
 * @brief Add a record to the RAM copy, dropping the oldest when full.
 *
 * @param r The record.
 *
 * @sideeffects Writes @c recent, @c recent_start, @c recent_count and @c last_boot. errno
 *              unchanged.
 */
static void remember(const struct boot_record *r)
{
	if (recent_count < EVENT_LOG_RECENT) {
		recent[(recent_start + recent_count) % EVENT_LOG_RECENT] = *r;
		recent_count++;
	} else {
		recent[recent_start] = *r;
		recent_start = (recent_start + 1U) % EVENT_LOG_RECENT;
	}
	last_boot = MAX(last_boot, r->boot);
}

/**
 * @brief Read one record from flash into the RAM copy (fcb_walk() callback).
 *
 * Entries that aren't records (another size or format) are skipped.
 *
 * @param ctx The entry.
 * @param arg Unused.
 * @return 0: carry on walking.
 *
 * @sideeffects Reads the storage partition; those of remember(). errno unchanged.
 */
static int read_entry(struct fcb_entry_ctx *ctx, void *arg)
{
	uint8_t bytes[BOOT_RECORD_SIZE];
	struct boot_record r;

	ARG_UNUSED(arg);
	if (ctx->loc.fe_data_len != sizeof(bytes) ||
	    flash_area_read(ctx->fap, FCB_ENTRY_FA_DATA_OFF(ctx->loc), bytes, sizeof(bytes)) != 0 ||
	    boot_record_decode(bytes, sizeof(bytes), &r) != 0) {
		return 0;
	}
	remember(&r);

	return 0;
}

/**
 * @brief Set up the FCB over the storage partition's sectors.
 *
 * @retval 0 Open.
 * @retval <0 Negative errno from the flash map or the FCB.
 *
 * @sideeffects Reads the storage partition; fills @c fcb and @c sectors. errno unchanged.
 */
static int open_fcb(void)
{
	uint32_t count = ARRAY_SIZE(sectors);
	int ret = flash_area_get_sectors(LOG_PARTITION, &count, sectors);

	if (ret != 0) {
		return ret;
	}
	fcb = (struct fcb){
		.f_magic = FCB_MAGIC,
		.f_version = FCB_VERSION,
		.f_sector_cnt = (uint16_t)count,
		.f_sectors = sectors,
	};

	return fcb_init(LOG_PARTITION, &fcb);
}

/**
 * @brief Erase the storage partition.
 *
 * @retval 0 Erased.
 * @retval <0 Negative errno from the flash map.
 *
 * @sideeffects Erases the storage partition. errno unchanged.
 */
static int erase_partition(void)
{
	const struct flash_area *fa;
	int ret = flash_area_open(LOG_PARTITION, &fa);

	if (ret != 0) {
		return ret;
	}
	ret = flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	return ret;
}

/*
 * Public interface: documented in event_log.h.
 *
 * fcb_init() refuses sectors that hold something else; those are erased once and the log
 * started again. The RAM copy starts empty, so opening the log again reads it afresh.
 */
int event_log_init(void)
{
	int ret;

	log_open = false;
	recent_start = 0;
	recent_count = 0;
	last_boot = 0;
	ret = open_fcb();

	if (ret != 0) {
		LOG_WRN("No event log in flash (%d): starting one", ret);
		ret = erase_partition();
		if (ret == 0) {
			ret = open_fcb();
		}
	}
	if (ret != 0) {
		LOG_ERR("Event log unavailable: %d", ret);
		return ret;
	}
	log_open = true;
	ret = fcb_walk(&fcb, NULL, read_entry, NULL);
	if (ret != 0) {
		LOG_WRN("Event log read stopped: %d", ret);
	}
	LOG_INF("Event log: %u starts, the last #%u", (unsigned int)recent_count,
		(unsigned int)last_boot);

	return 0;
}

/**
 * @brief Write a record into the FCB at its next free place.
 *
 * @param bytes The record, in flash format.
 * @retval 0 Written.
 * @retval -ENOSPC The FCB is full.
 * @retval <0 Other negative errno from the FCB or flash.
 *
 * @sideeffects Writes the storage partition; updates @c fcb. errno unchanged.
 */
static int append(const uint8_t *bytes)
{
	struct fcb_entry loc;
	int ret = fcb_append(&fcb, BOOT_RECORD_SIZE, &loc);

	if (ret != 0) {
		return ret;
	}
	ret = flash_area_write(fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc), bytes, BOOT_RECORD_SIZE);
	if (ret != 0) {
		return ret;
	}

	return fcb_append_finish(&fcb, &loc);
}

/*
 * Public interface: documented in event_log.h.
 */
int event_log_add(const struct boot_record *r)
{
	uint8_t bytes[BOOT_RECORD_SIZE];
	int ret;

	if (!log_open) {
		return -ENODEV;
	}
	boot_record_encode(r, bytes);
	ret = append(bytes);
	if (ret == -ENOSPC) {
		ret = fcb_rotate(&fcb);
		if (ret == 0) {
			ret = append(bytes);
		}
	}
	if (ret != 0) {
		LOG_ERR("Event log write failed: %d", ret);
		return ret;
	}
	remember(r);

	return 0;
}

/*
 * Public interface: documented in event_log.h.
 */
uint32_t event_log_last_boot(void)
{
	return last_boot;
}

/*
 * Public interface: documented in event_log.h.
 */
size_t event_log_count(void)
{
	return recent_count;
}

/*
 * Public interface: documented in event_log.h.
 */
const struct boot_record *event_log_get(size_t i)
{
	if (i >= recent_count) {
		return NULL;
	}

	return &recent[(recent_start + recent_count - 1U - i) % EVENT_LOG_RECENT];
}
