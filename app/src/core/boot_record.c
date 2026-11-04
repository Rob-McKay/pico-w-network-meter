/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include "boot_record.h"

/* Version of the flash format, its first byte. */
#define RECORD_VERSION   1U
/* A sealed note's magic number: "MTRN". */
#define NOTE_MAGIC       0x4d54524eU
/* Bytes of a note covered by its checksum. */
#define NOTE_CHECKED     17U
#define SECONDS_PER_MIN  60U
#define SECONDS_PER_HOUR 3600U
#define HOURS_PER_DAY    24U

/**
 * @brief Read a signed 32-bit value stored as its two's complement bits.
 *
 * Done arithmetically: converting an out-of-range unsigned value to a
 * signed type is implementation-defined.
 *
 * @param v The bits.
 * @return The value.
 *
 * @sideeffects None; errno unchanged.
 */
static int32_t from_twos_complement(uint32_t v)
{
	if (v <= (uint32_t)INT32_MAX) {
		return (int32_t)v;
	}

	return -(int32_t)(~v) - 1;
}

/*
 * Public interface: documented in boot_record.h.
 *
 * Layout: version, cause, hw_reset, a zero byte, then boot, uptime_s, detail and arg as 32-bit
 * little-endian values; detail as its two's complement bits. hw_reset takes what was a zero
 * byte, so records from before it read as BOOT_HW_UNKNOWN and the version stays 1.
 */
void boot_record_encode(const struct boot_record *r, uint8_t *out)
{
	out[0] = RECORD_VERSION;
	out[1] = r->cause;
	out[2] = r->hw_reset;
	out[3] = 0;
	sys_put_le32(r->boot, &out[4]);
	sys_put_le32(r->uptime_s, &out[8]);
	sys_put_le32((uint32_t)r->detail, &out[12]);
	sys_put_le32(r->arg, &out[16]);
}

/*
 * Public interface: documented in boot_record.h.
 */
int boot_record_decode(const uint8_t *in, size_t len, struct boot_record *r)
{
	if (len != BOOT_RECORD_SIZE || in[0] != RECORD_VERSION || in[1] == BOOT_NONE ||
	    in[1] >= BOOT_CAUSE_COUNT) {
		return -EINVAL;
	}
	r->cause = in[1];
	r->hw_reset = in[2] < BOOT_HW_COUNT ? in[2] : (uint8_t)BOOT_HW_UNKNOWN;
	r->boot = sys_get_le32(&in[4]);
	r->uptime_s = sys_get_le32(&in[8]);
	r->detail = from_twos_complement(sys_get_le32(&in[12]));
	r->arg = sys_get_le32(&in[16]);

	return 0;
}

/**
 * @brief Write a length of time briefly: "3d04h", "2h31m" or "5m07s".
 *
 * @param s Seconds.
 * @param[out] buf The text.
 * @param size Size of @p buf.
 *
 * @sideeffects None (writes only through @p buf); errno may be set by snprintf() (only on an
 *              output error).
 */
static void duration(uint32_t s, char *buf, size_t size)
{
	uint32_t h = s / SECONDS_PER_HOUR;

	if (h >= HOURS_PER_DAY) {
		(void)snprintf(buf, size, "%" PRIu32 "d%02" PRIu32 "h", h / HOURS_PER_DAY,
			       h % HOURS_PER_DAY);
	} else if (h > 0U) {
		(void)snprintf(buf, size, "%" PRIu32 "h%02" PRIu32 "m", h,
			       (s % SECONDS_PER_HOUR) / SECONDS_PER_MIN);
	} else {
		(void)snprintf(buf, size, "%" PRIu32 "m%02" PRIu32 "s", s / SECONDS_PER_MIN,
			       s % SECONDS_PER_MIN);
	}
}

/**
 * @brief Name a hardware reset cause that explains a restart the note can't.
 *
 * Each name is at most as long as "Wi-Fi restart", so that a description fits the events
 * screen as the others do.
 *
 * @param hw_reset enum boot_hw_reset.
 * @return The name, or NULL for a cause that adds nothing ("restart" says as much).
 *
 * @sideeffects None; errno unchanged.
 */
static const char *hw_reset_name(uint8_t hw_reset)
{
	switch (hw_reset) {
	case BOOT_HW_PIN:
		return "reset button";
	case BOOT_HW_WATCHDOG:
		return "HW watchdog";
	case BOOT_HW_BROWNOUT:
		return "brown-out";
	case BOOT_HW_CPU_FAULT:
		return "CPU fault";
	default:
		return NULL;
	}
}

/*
 * Public interface: documented in boot_record.h.
 */
void boot_record_describe(const struct boot_record *r, char *buf, size_t size)
{
	char ran[16];
	unsigned int boot = (unsigned int)r->boot;
	const char *hw = hw_reset_name(r->hw_reset);

	if (size == 0U) {
		return;
	}
	duration(r->uptime_s, ran, sizeof(ran));
	switch (r->cause) {
	case BOOT_POWER_ON:
		(void)snprintf(buf, size, "#%u %s", boot,
			       r->hw_reset == BOOT_HW_BROWNOUT ? "brown-out" : "power on");
		break;
	case BOOT_WIFI_RESTART:
		(void)snprintf(buf, size, "#%u Wi-Fi restart after %s", boot, ran);
		break;
	case BOOT_CRASH:
		(void)snprintf(buf, size, "#%u crash %d after %s", boot, (int)r->detail, ran);
		break;
	case BOOT_WATCHDOG:
		(void)snprintf(buf, size, "#%u %s hung after %s", boot,
			       r->detail == BOOT_WATCHED_SCAN ? "scan" : "main", ran);
		break;
	case BOOT_UNEXPLAINED:
		(void)snprintf(buf, size, "#%u %s after %s", boot, hw != NULL ? hw : "restart",
			       ran);
		break;
	default:
		(void)snprintf(buf, size, "#%u unknown start", boot);
		break;
	}
}

/**
 * @brief Work out a note's checksum.
 *
 * Over its fields, little-endian, rather than its bytes, which would
 * include padding.
 *
 * @param n The note.
 * @return CRC-32 (IEEE).
 *
 * @sideeffects None; errno unchanged.
 */
static uint32_t note_check(const struct boot_note *n)
{
	uint8_t bytes[NOTE_CHECKED];

	sys_put_le32(n->magic, &bytes[0]);
	sys_put_le32(n->uptime_s, &bytes[4]);
	sys_put_le32((uint32_t)n->detail, &bytes[8]);
	sys_put_le32(n->arg, &bytes[12]);
	bytes[16] = n->cause;

	return crc32_ieee(bytes, sizeof(bytes));
}

/*
 * Public interface: documented in boot_record.h.
 */
void boot_note_seal(struct boot_note *n)
{
	n->magic = NOTE_MAGIC;
	n->check = note_check(n);
}

/*
 * Public interface: documented in boot_record.h.
 */
bool boot_note_valid(const struct boot_note *n)
{
	return n->magic == NOTE_MAGIC && n->cause < BOOT_CAUSE_COUNT && n->check == note_check(n);
}

/*
 * Public interface: documented in boot_record.h.
 */
void boot_record_from_note(const struct boot_note *n, uint32_t boot, struct boot_record *r)
{
	r->boot = boot;
	r->hw_reset = BOOT_HW_UNKNOWN;
	if (!boot_note_valid(n) || n->cause == BOOT_POWER_ON) {
		r->cause = BOOT_POWER_ON;
		r->uptime_s = 0;
		r->detail = 0;
		r->arg = 0;
		return;
	}
	r->cause = n->cause == BOOT_NONE ? (uint8_t)BOOT_UNEXPLAINED : n->cause;
	r->uptime_s = n->uptime_s;
	r->detail = n->detail;
	r->arg = n->arg;
}
