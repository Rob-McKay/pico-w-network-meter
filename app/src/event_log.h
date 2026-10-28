/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The log of starts (boot_record.h), kept in the flash partition
 * storage_partition with Zephyr's flash circular buffer (FCB), so it
 * survives power cuts and firmware updates. When the partition fills, the
 * oldest sector of records is erased. Each start adds one record, so the
 * flash wears slowly.
 *
 * Main thread only: it is used at start-up and by the events screen.
 */

#ifndef METER_EVENT_LOG_H_
#define METER_EVENT_LOG_H_

#include <stddef.h>
#include <stdint.h>

#include "boot_record.h"

/** Records kept in RAM for the events screen: the newest. */
#define EVENT_LOG_RECENT 64U

/**
 * @brief Open the log and read the newest records into RAM.
 *
 * Flash holding something other than a log (or a log with an unknown
 * format) is erased and started afresh. Opening it again reads it again.
 *
 * @retval 0 Open.
 * @retval <0 Negative errno: no storage partition, or flash that can't be
 *         read or erased. The log stays closed and empty.
 *
 * @sideeffects Reads the storage partition, and may erase it; fills the module's FCB state and
 *              its RAM copy of the newest records; logs what it found and any failure. errno
 *              unchanged.
 */
int event_log_init(void);

/**
 * @brief Add a record, erasing the oldest sector first if the log is full.
 *
 * @param r The record.
 * @retval 0 Added.
 * @retval -ENODEV The log isn't open.
 * @retval <0 Other negative errno from the flash.
 *
 * @sideeffects Writes (and may erase) the storage partition, which stops interrupts while the
 *              flash is busy (up to about 50 ms an erase); updates the module's FCB state and
 *              RAM copy; logs a failure. errno unchanged.
 */
int event_log_add(const struct boot_record *r);

/**
 * @brief Find the number of the newest start logged.
 *
 * @return Its boot number, or 0 if the log is empty or closed.
 *
 * @sideeffects None; errno unchanged.
 */
uint32_t event_log_last_boot(void);

/**
 * @brief Count the records kept in RAM.
 *
 * @return Up to EVENT_LOG_RECENT.
 *
 * @sideeffects None; errno unchanged.
 */
size_t event_log_count(void);

/**
 * @brief Get one of the records kept in RAM, newest first.
 *
 * @param i 0 for the newest, up to event_log_count() - 1.
 * @return The record, or NULL if there is no such record.
 *
 * @sideeffects None; errno unchanged.
 */
const struct boot_record *event_log_get(size_t i);

/** Takes one record of event_log_dump()'s walk. */
typedef void (*event_log_each_fn)(const struct boot_record *r, void *arg);

/**
 * @brief Pass every record in flash, oldest first, to a function: the
 *        whole log, not only the newest kept in RAM.
 *
 * @param each The function.
 * @param arg Passed on to it.
 * @return How many records it was given, or a negative errno: -ENODEV if
 *         the log isn't open, or from the flash.
 *
 * @sideeffects Reads the storage partition; those of @p each. errno unchanged.
 */
int event_log_dump(event_log_each_fn each, void *arg);

#endif /* METER_EVENT_LOG_H_ */
