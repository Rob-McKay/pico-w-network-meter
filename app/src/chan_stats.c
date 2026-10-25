/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "chan_stats.h"

/* Width of a 2.4 GHz channel (802.11g/n, 20 MHz), MHz. */
#define CHAN_WIDTH_MHZ 20
/* The unit of power, dBm: 10 dB below the weakest value converted, so that
 * every whole dBm from -130 up converts back exactly.
 */
#define POWER_FLOOR_DBM (-140)
/* Weakest value chan_dbm_to_power() takes, dBm. */
#define DBM_MIN (-130)
/* Fixed-point scale of the mantissa tables: 10^MANTISSA_DIGITS. */
#define MANTISSA_ONE 10000U
#define MANTISSA_DIGITS 4U
/* Steps of 1 dB in a decade (10 dB). */
#define DB_PER_DECADE 10
/* Interference thresholds of chan_stats_level(), dBm. */
#define LEVEL_MODERATE_DBM (-85)
#define LEVEL_BUSY_DBM (-70)

/* 10^(r/10) for r = 0..9, times MANTISSA_ONE. */
static const uint32_t db_mantissa[DB_PER_DECADE] = {
	10000, 12589, 15849, 19953, 25119, 31623, 39811, 50119, 63096, 79433,
};

/* 10^((r+0.5)/10) for r = 0..9, times MANTISSA_ONE: where rounding to the
 * nearest whole dB moves from r to r+1.
 */
static const uint32_t db_round_up[DB_PER_DECADE] = {
	11220, 14125, 17783, 22387, 28184, 35481, 44668, 56234, 70795, 89125,
};

/**
 * @brief Raise 10 to a power.
 *
 * @param exp Exponent, 0-19.
 * @return 10^exp.
 */
static uint64_t pow10_u64(unsigned int exp)
{
	uint64_t v = 1;

	for (unsigned int i = 0; i < exp; i++) {
		v *= 10U;
	}

	return v;
}

/**
 * @brief Centre frequency of a 2.4 GHz channel.
 *
 * @param channel CHAN_FIRST..CHAN_LAST.
 * @return MHz.
 */
static int centre_mhz(int channel)
{
	return channel == 14 ? 2484 : 2407 + (5 * channel);
}

/**
 * @brief How much of a network's channel overlaps another channel.
 *
 * @param a One channel.
 * @param b The other.
 * @return Overlap in MHz: CHAN_WIDTH_MHZ for the same channel, down to 0.
 */
static int overlap_mhz(int a, int b)
{
	int df = centre_mhz(a) - centre_mhz(b);

	if (df < 0) {
		df = -df;
	}

	return df >= CHAN_WIDTH_MHZ ? 0 : CHAN_WIDTH_MHZ - df;
}

uint64_t chan_dbm_to_power(int dbm)
{
	if (dbm < DBM_MIN) {
		dbm = DBM_MIN;
	} else if (dbm > 0) {
		dbm = 0;
	}

	unsigned int x = (unsigned int)(dbm - POWER_FLOOR_DBM);
	uint64_t scaled = pow10_u64(x / DB_PER_DECADE) * db_mantissa[x % DB_PER_DECADE];

	return (scaled + (MANTISSA_ONE / 2U)) / MANTISSA_ONE;
}

int chan_power_to_dbm(uint64_t power)
{
	if (power == 0U) {
		return CHAN_RSSI_NONE;
	}

	unsigned int decade = 0;

	while (power / pow10_u64(decade) >= 10U) {
		decade++;
	}

	/* power / 10^decade is in [1, 10): its mantissa, in MANTISSA_ONE units,
	 * worked out without overflowing for powers above 10^15.
	 */
	uint64_t mantissa = decade >= MANTISSA_DIGITS
				    ? power / pow10_u64(decade - MANTISSA_DIGITS)
				    : power * pow10_u64(MANTISSA_DIGITS - decade);
	unsigned int step = 0;

	while (step < DB_PER_DECADE && mantissa >= db_round_up[step]) {
		step++;
	}

	return POWER_FLOOR_DBM + (int)((decade * DB_PER_DECADE) + step);
}

void chan_stats_reset(struct chan_stats *s)
{
	memset(s, 0, sizeof(*s));
}

int chan_stats_add(struct chan_stats *s, const struct chan_ap *ap)
{
	if (ap->channel < CHAN_FIRST || ap->channel > CHAN_LAST) {
		s->ignored++;
		return -EINVAL;
	}

	for (size_t i = 0; i < s->ap_count; i++) {
		if (memcmp(s->aps[i].bssid, ap->bssid, CHAN_BSSID_LEN) == 0) {
			if (ap->rssi > s->aps[i].rssi) {
				s->aps[i] = *ap;
			}
			return 0;
		}
	}

	if (s->ap_count >= CHAN_MAX_APS) {
		s->dropped++;
		return -ENOSPC;
	}
	s->aps[s->ap_count++] = *ap;

	return 0;
}

void chan_stats_channel(const struct chan_stats *s, uint8_t channel, struct chan_info *out)
{
	uint64_t power = 0;

	out->channel = channel;
	out->ap_count = 0;
	out->best_rssi = CHAN_RSSI_NONE;

	for (size_t i = 0; i < s->ap_count; i++) {
		const struct chan_ap *ap = &s->aps[i];
		int overlap = overlap_mhz(ap->channel, channel);

		if (ap->channel == channel) {
			out->ap_count++;
			if (ap->rssi > out->best_rssi) {
				out->best_rssi = ap->rssi;
			}
		}
		power += (chan_dbm_to_power(ap->rssi) * (unsigned int)overlap) / CHAN_WIDTH_MHZ;
	}

	out->interference = (int16_t)chan_power_to_dbm(power);
}

uint8_t chan_stats_recommend(const struct chan_stats *s)
{
	static const uint8_t candidates[] = {1, 6, 11};
	struct chan_info best;

	chan_stats_channel(s, candidates[0], &best);
	for (size_t i = 1; i < sizeof(candidates); i++) {
		struct chan_info c;

		chan_stats_channel(s, candidates[i], &c);
		if (c.interference < best.interference ||
		    (c.interference == best.interference && c.ap_count < best.ap_count)) {
			best = c;
		}
	}

	return best.channel;
}

size_t chan_stats_strongest(const struct chan_stats *s, const struct chan_ap **out, size_t max)
{
	size_t n = 0;

	/* Insertion sort into out[]: a scan has at most CHAN_MAX_APS. */
	for (size_t i = 0; i < s->ap_count; i++) {
		const struct chan_ap *ap = &s->aps[i];
		size_t pos = n;

		while (pos > 0 && out[pos - 1]->rssi < ap->rssi) {
			pos--;
		}
		if (pos >= max) {
			continue;
		}
		for (size_t j = (n < max ? n : max - 1); j > pos; j--) {
			out[j] = out[j - 1];
		}
		out[pos] = ap;
		if (n < max) {
			n++;
		}
	}

	return n;
}

enum chan_level chan_stats_level(int interference)
{
	if (interference >= LEVEL_BUSY_DBM) {
		return CHAN_LEVEL_BUSY;
	}
	if (interference >= LEVEL_MODERATE_DBM) {
		return CHAN_LEVEL_MODERATE;
	}

	return CHAN_LEVEL_CLEAR;
}
