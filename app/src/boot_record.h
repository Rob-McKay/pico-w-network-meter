/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Why the meter started: one record per start, kept in flash by
 * event_log.c. A restart the meter makes itself (reset_note.c) leaves a
 * note in RAM that survives it, saying why; a power cut loses RAM, and so
 * the note. Hardware independent, so it is unit tested on native_sim.
 */

#ifndef METER_BOOT_RECORD_H_
#define METER_BOOT_RECORD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Why the meter started. */
enum boot_cause {
	BOOT_NONE,         /**< in a note: running, no restart asked for */
	BOOT_POWER_ON,     /**< power applied (or restored after a cut) */
	BOOT_WIFI_RESTART, /**< restarted itself: the Wi-Fi driver stopped working */
	BOOT_CRASH,        /**< restarted after a fatal error */
	BOOT_WATCHDOG,     /**< restarted: a thread stopped responding */
	BOOT_UNEXPLAINED,  /**< restarted with no reason recorded */
	BOOT_CAUSE_COUNT,
};

/** Threads the watchdog watches: a watchdog restart's detail. */
enum boot_watched {
	BOOT_WATCHED_MAIN, /**< the main thread: display, buttons, LED */
	BOOT_WATCHED_SCAN, /**< the scan thread */
};

/** One start of the meter. */
struct boot_record {
	uint32_t boot;     /**< counts starts, from 1 */
	uint32_t uptime_s; /**< how long the run before it lasted, s; 0 if unknown */
	int32_t detail;    /**< Wi-Fi: last error; crash: reason; watchdog: boot_watched */
	uint32_t arg;      /**< Wi-Fi: failed scans in a row; crash: program counter */
	uint8_t cause;     /**< enum boot_cause */
};

/** A record's size in flash, bytes. */
#define BOOT_RECORD_SIZE 20U

/** The note a restart leaves in RAM: why, and how long the run lasted. */
struct boot_note {
	uint32_t magic;    /**< a fixed value, set by boot_note_seal() */
	uint32_t uptime_s; /**< how long the run lasted, s */
	int32_t detail;    /**< as in struct boot_record */
	uint32_t arg;      /**< as in struct boot_record */
	uint8_t cause;     /**< enum boot_cause; BOOT_NONE while running */
	uint32_t check;    /**< CRC-32 of the fields above */
};

/**
 * @brief Write a record in its flash format.
 *
 * Little-endian, field by field, so the format doesn't depend on the
 * compiler's struct layout.
 *
 * @param r The record.
 * @param[out] out BOOT_RECORD_SIZE bytes.
 *
 * @sideeffects None (writes only through @p out); errno unchanged.
 */
void boot_record_encode(const struct boot_record *r, uint8_t *out);

/**
 * @brief Read a record from its flash format.
 *
 * @param in The bytes.
 * @param len How many there are.
 * @param[out] r The record.
 * @retval 0 Read.
 * @retval -EINVAL Not a record: the wrong size, version or cause.
 *
 * @sideeffects None (writes only through @p r); errno unchanged.
 */
int boot_record_decode(const uint8_t *in, size_t len, struct boot_record *r);

/**
 * @brief Describe a record in a line of text, such as
 *        "#12 Wi-Fi restart after 2h31m".
 *
 * The time is how long the run before it lasted.
 *
 * @param r The record.
 * @param[out] buf The text, cut to fit and always terminated.
 * @param size Size of @p buf; 0 writes nothing.
 *
 * @sideeffects None (writes only through @p buf); errno may be set by snprintf() (only on an
 *              output error).
 */
void boot_record_describe(const struct boot_record *r, char *buf, size_t size);

/**
 * @brief Seal a note: set its magic number and checksum.
 *
 * @param[in,out] n The note, its other fields filled in.
 *
 * @sideeffects None (writes only through @p n); errno unchanged.
 */
void boot_note_seal(struct boot_note *n);

/**
 * @brief Tell whether a note is one boot_note_seal() wrote, not what RAM
 *        holds after power-on.
 *
 * @param n The note.
 * @return True if its magic number and checksum match.
 *
 * @sideeffects None; errno unchanged.
 */
bool boot_note_valid(const struct boot_note *n);

/**
 * @brief Work out a start's record from the note the run before left.
 *
 * No valid note means the power was off; a valid one without a cause, that
 * something restarted the meter without warning it (the reset button, a
 * debugger, the hardware watchdog).
 *
 * @param n The note found in RAM at start-up.
 * @param boot The start's number.
 * @param[out] r The record.
 *
 * @sideeffects None (writes only through @p r); errno unchanged.
 */
void boot_record_from_note(const struct boot_note *n, uint32_t boot, struct boot_record *r);

#endif /* METER_BOOT_RECORD_H_ */
