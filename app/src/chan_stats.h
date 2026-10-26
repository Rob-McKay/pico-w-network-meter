/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Channel statistics for the 2.4 GHz Wi-Fi band, from one scan's access
 * points. No Zephyr Wi-Fi API here: scanner.c feeds it, the UI and the unit
 * tests read it.
 *
 * Each 2.4 GHz channel is 20 MHz wide, but channels are only 5 MHz apart,
 * so a network interferes with its neighbours up to 3 channels away. The
 * "interference" of a channel adds up the signal power of every network
 * heard, each weighted by how much of its 20 MHz overlaps the channel's.
 * Arithmetic is integer only: dBm values are whole numbers.
 */

#ifndef METER_CHAN_STATS_H_
#define METER_CHAN_STATS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** First 2.4 GHz channel. */
#define CHAN_FIRST 1
/** Last 2.4 GHz channel (14: Japan, 802.11b only). */
#define CHAN_LAST  14
/** Number of 2.4 GHz channels. */
#define CHAN_COUNT (CHAN_LAST - CHAN_FIRST + 1)

/** Most access points kept from one scan. */
#define CHAN_MAX_APS   64
/** Longest SSID, in bytes. */
#define CHAN_SSID_MAX  32
/** BSSID (MAC address) length. */
#define CHAN_BSSID_LEN 6

/** Freshness of an access point heard in the latest scan. */
#define CHAN_FRESH 255

/** "No signal": the RSSI or interference of a channel with nothing on it. */
#define CHAN_RSSI_NONE (-127)

/** One access point (BSS) heard in a scan. */
struct chan_ap {
	char ssid[CHAN_SSID_MAX + 1]; /**< NUL-terminated; empty if hidden */
	uint8_t bssid[CHAN_BSSID_LEN];
	uint8_t channel;   /**< CHAN_FIRST..CHAN_LAST */
	int8_t rssi;       /**< dBm */
	bool secured;      /**< anything but an open network */
	uint8_t freshness; /**< CHAN_FRESH when just heard, down to 0 (faded out) */
};

/** One scan's access points, each BSSID once. */
struct chan_stats {
	struct chan_ap aps[CHAN_MAX_APS];
	size_t ap_count;
	size_t dropped; /**< access points that did not fit in aps[] */
	size_t ignored; /**< results not on a 2.4 GHz channel (or hidden) */
};

/** A channel's figures, worked out by chan_stats_channel(). */
struct chan_info {
	uint8_t channel;
	uint8_t ap_count;     /**< networks whose primary channel this is */
	int8_t best_rssi;     /**< strongest of those, or CHAN_RSSI_NONE */
	int16_t interference; /**< overlap-weighted power, dBm, or CHAN_RSSI_NONE */
	int16_t others;       /**< the same without the strongest network on the channel */
	uint8_t freshness;    /**< freshest network reaching the channel, or 0 */
};

/** How busy a channel is, from its interference. */
enum chan_level {
	CHAN_LEVEL_CLEAR,    /**< below -85 dBm, or nothing heard */
	CHAN_LEVEL_MODERATE, /**< -85 to -71 dBm */
	CHAN_LEVEL_BUSY,     /**< -70 dBm and above */
};

/**
 * @brief Empty the statistics, ready for a new scan.
 *
 * @param[out] s Statistics.
 *
 * @sideeffects None (writes only through @p s); errno unchanged.
 */
void chan_stats_reset(struct chan_stats *s);

/**
 * @brief Add one scan result.
 *
 * A BSSID already present is updated in place if this report is stronger
 * (a scan can hear an access point more than once). A result with no
 * 2.4 GHz channel (5 GHz, or a hidden network the driver reports without
 * one) only counts in @p s->ignored.
 *
 * @param[in,out] s Statistics.
 * @param ap The access point.
 * @retval 0 Added or updated.
 * @retval -EINVAL Not on a 2.4 GHz channel: counted as ignored.
 * @retval -ENOSPC No room: counted as dropped.
 *
 * @sideeffects None (writes only through @p s, including its ignored and dropped counts); errno
 *              unchanged.
 */
int chan_stats_add(struct chan_stats *s, const struct chan_ap *ap);

/**
 * @brief Work out one channel's figures.
 *
 * @c interference counts every network that overlaps the channel, its own
 * included. @c others leaves out the channel's strongest network (@c
 * best_rssi): it is what interferes with that network, from other
 * networks on the same channel and those spilling over from overlapping
 * channels.
 *
 * @param s Statistics.
 * @param channel CHAN_FIRST..CHAN_LAST.
 * @param[out] out The channel's figures.
 *
 * @sideeffects None (writes only through @p out); errno unchanged.
 */
void chan_stats_channel(const struct chan_stats *s, uint8_t channel, struct chan_info *out);

/**
 * @brief Pick the best of the non-overlapping channels 1, 6 and 11.
 *
 * The one with the least interference wins; on a tie, the one with fewer
 * networks, then the lower channel.
 *
 * @param s Statistics.
 * @return 1, 6 or 11.
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t chan_stats_recommend(const struct chan_stats *s);

/**
 * @brief List the access points strongest first.
 *
 * @param s Statistics.
 * @param[out] out Pointers into @p s->aps.
 * @param max Room in @p out.
 * @return Number of entries written: at most @p max.
 *
 * @sideeffects None (writes only through @p out); errno unchanged.
 */
size_t chan_stats_strongest(const struct chan_stats *s, const struct chan_ap **out, size_t max);

/**
 * @brief Tell whether two sets of statistics hold the same access points,
 *        in the same order, with the same counts.
 *
 * @param a One set.
 * @param b The other.
 * @return True if they are equal.
 *
 * @sideeffects None; errno unchanged.
 */
bool chan_stats_equal(const struct chan_stats *a, const struct chan_stats *b);

/**
 * @brief Tell whether a channel is one of the standard, non-overlapping
 *        channels 1, 6 and 11.
 *
 * @param channel Channel.
 * @return True for 1, 6 and 11.
 *
 * @sideeffects None; errno unchanged.
 */
bool chan_is_standard(uint8_t channel);

/**
 * @brief Tell whether an access point hides its network's name.
 *
 * @param ap The access point.
 * @return True if its SSID is empty (or all NULs, as some hidden networks
 *         send it).
 *
 * @sideeffects None; errno unchanged.
 */
bool chan_ap_hidden(const struct chan_ap *ap);

/**
 * @brief Name an access point for display.
 *
 * A hidden network is named by the last three bytes of its BSSID (its MAC
 * address), "hidden 12:34:56", which tell the access points apart; the
 * first three only name the maker; that takes 16 bytes.
 *
 * @param ap The access point.
 * @param[out] buf The name, cut to fit and always terminated.
 * @param size Size of @p buf; 0 writes nothing.
 *
 * @sideeffects None (writes only through @p buf); errno may be set by snprintf() (only on an
 *              output error).
 */
void chan_ap_name(const struct chan_ap *ap, char *buf, size_t size);

/**
 * @brief Classify an interference level.
 *
 * @param interference dBm, or CHAN_RSSI_NONE.
 * @return The level.
 *
 * @sideeffects None; errno unchanged.
 */
enum chan_level chan_stats_level(int interference);

/**
 * @brief Convert a power in dBm to a linear power.
 *
 * Exposed for the unit tests.
 *
 * @param dbm -130 to 0 dBm; values outside are clamped.
 * @return Power in units of 10^-14 mW (-140 dBm is 1).
 *
 * @sideeffects None; errno unchanged.
 */
uint64_t chan_dbm_to_power(int dbm);

/**
 * @brief Convert a linear power back to dBm, to the nearest whole dB.
 *
 * Exposed for the unit tests.
 *
 * @param power Units of 10^-14 mW, as chan_dbm_to_power() gives.
 * @return dBm, or CHAN_RSSI_NONE for zero power.
 *
 * @sideeffects None; errno unchanged.
 */
int chan_power_to_dbm(uint64_t power);

#endif /* METER_CHAN_STATS_H_ */
