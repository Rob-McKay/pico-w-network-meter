/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Channel statistics for the 2.4 and 5 GHz Wi-Fi bands, from one scan's
 * access points. No Zephyr Wi-Fi API here: scanner.c feeds it, the UI and
 * the unit tests read it.
 *
 * Each 2.4 GHz channel is 20 MHz wide, but channels are only 5 MHz apart,
 * so a network interferes with its neighbours up to 3 channels away. The
 * "interference" of a channel adds up the signal power of every network
 * heard, each weighted by how much of its 20 MHz overlaps the channel's.
 *
 * 5 GHz channels (36, 40, ...) don't overlap each other; a network spreads
 * over a block of them by bonding channels. Zephyr's scan results don't
 * say how wide a network is, so every 5 GHz network is taken to use its
 * whole 80 MHz block (four channels), as nearly every home router does:
 * it counts in full on each of the block's channels and not at all
 * outside it.
 *
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

/** The bands the meter knows. 2.4 GHz is 0, so a zeroed record is a
 *  2.4 GHz one.
 */
enum chan_band {
	CHAN_BAND_2G4, /**< 2.4 GHz: channels CHAN_FIRST..CHAN_LAST */
	CHAN_BAND_5G,  /**< 5 GHz: the channels chan_5g_index() knows */
	CHAN_BAND_COUNT,
};

/** 5 GHz 20 MHz channels the meter knows: 36 to 177, in 80 MHz blocks. */
#define CHAN_5G_COUNT          28
/** 5 GHz channels in one 80 MHz block. */
#define CHAN_5G_BLOCK_CHANNELS 4
/** 80 MHz blocks: 36-48, 52-64, 100-112, 116-128, 132-144, 149-161,
 *  165-177.
 */
#define CHAN_5G_BLOCKS         (CHAN_5G_COUNT / CHAN_5G_BLOCK_CHANNELS)

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
	uint8_t channel;   /**< CHAN_FIRST..CHAN_LAST, or a 5 GHz channel */
	uint8_t band;      /**< enum chan_band */
	int8_t rssi;       /**< dBm */
	bool secured;      /**< anything but an open network */
	uint8_t freshness; /**< CHAN_FRESH when just heard, down to 0 (faded out) */
};

/** One scan's access points, each BSSID once. */
struct chan_stats {
	struct chan_ap aps[CHAN_MAX_APS];
	size_t ap_count;
	size_t dropped; /**< access points that did not fit in aps[] */
	/** Results on no channel the meter knows: 6 GHz, or a hidden network reported without
	 *  one.
	 */
	size_t ignored;
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
 * (a scan can hear an access point more than once). A result on no
 * channel of its band (6 GHz, or a hidden network the driver reports
 * without a channel) only counts in @p s->ignored.
 *
 * @param[in,out] s Statistics.
 * @param ap The access point.
 * @retval 0 Added or updated.
 * @retval -EINVAL Not on a channel of its band: counted as ignored.
 * @retval -ENOSPC No room: counted as dropped.
 *
 * @sideeffects None (writes only through @p s, including its ignored and dropped counts); errno
 *              unchanged.
 */
int chan_stats_add(struct chan_stats *s, const struct chan_ap *ap);

/**
 * @brief Count the access points on one band.
 *
 * @param s Statistics.
 * @param band enum chan_band.
 * @return How many of @p s->aps are on @p band.
 *
 * @sideeffects None; errno unchanged.
 */
size_t chan_stats_band_count(const struct chan_stats *s, enum chan_band band);

/**
 * @brief Work out one 2.4 GHz channel's figures.
 *
 * Only 2.4 GHz networks count. @c interference counts every network that
 * overlaps the channel, its own included. @c others leaves out the channel's strongest network (@c
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
 * @brief Work out one 5 GHz channel's figures.
 *
 * Every 5 GHz network counts in full on each channel of its 80 MHz block
 * (the file comment says why), so the channels of a block share their
 * @c interference and @c freshness. @c ap_count, @c best_rssi and @c others
 * are as on 2.4 GHz: the networks whose primary channel this is, the
 * strongest of them, and the interference without it.
 *
 * @param s Statistics.
 * @param channel A 5 GHz channel; any other gives an empty channel.
 * @param[out] out The channel's figures.
 *
 * @sideeffects None (writes only through @p out); errno unchanged.
 */
void chan_stats_channel_5g(const struct chan_stats *s, uint8_t channel, struct chan_info *out);

/**
 * @brief Pick the best 80 MHz block on 5 GHz.
 *
 * Of the blocks in @p blocks, the one with the least interference wins; on
 * a tie, the one with fewer networks, then one without DFS (a router on a
 * DFS channel must first listen for radar for a minute, and may have to
 * move), then the lower block.
 *
 * @param s Statistics.
 * @param blocks Bit n set to consider block n (0: 36-48, ...,
 *        CHAN_5G_BLOCKS - 1: 165-177), for the blocks the country allows.
 * @return The block's first channel (36, 52, ...), or 0 if @p blocks has
 *         none of them.
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t chan_stats_recommend_5g(const struct chan_stats *s, uint32_t blocks);

/**
 * @brief Say which 80 MHz blocks the 5 GHz recommendation may pick.
 *
 * @param with_132 Include 132-144 (the US allows it; Europe and the UK stop
 *        at 140, so not at 80 MHz there).
 * @param with_upper Include 149-161 (full power in the US; low-power,
 *        indoor short-range-device spectrum in the UK).
 * @return A mask for chan_stats_recommend_5g(): 36-48, 52-64, 100-112 and
 *         116-128 always, plus the two above if asked for. 165-177 is
 *         never included.
 *
 * @sideeffects None; errno unchanged.
 */
uint32_t chan_5g_blocks(bool with_132, bool with_upper);

/**
 * @brief List one band's access points strongest first.
 *
 * @param s Statistics.
 * @param band enum chan_band: only its access points are listed.
 * @param[out] out Pointers into @p s->aps.
 * @param max Room in @p out.
 * @return Number of entries written: at most @p max.
 *
 * @sideeffects None (writes only through @p out); errno unchanged.
 */
size_t chan_stats_strongest(const struct chan_stats *s, enum chan_band band,
			    const struct chan_ap **out, size_t max);

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
 * @brief Find a 5 GHz channel's place in the meter's list of them.
 *
 * @param channel Channel number.
 * @return 0 (36) to CHAN_5G_COUNT - 1 (177), or -1 if it isn't a 5 GHz
 *         20 MHz channel the meter knows.
 *
 * @sideeffects None; errno unchanged.
 */
int chan_5g_index(uint8_t channel);

/**
 * @brief Give the 5 GHz channel at a place in the meter's list of them.
 *
 * @param index 0 to CHAN_5G_COUNT - 1.
 * @return The channel number (36 ... 177), or 0 for an index past the end.
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t chan_5g_channel(size_t index);

/**
 * @brief Tell whether a 5 GHz channel needs radar detection (DFS).
 *
 * @param channel Channel number.
 * @return True for 52 to 144, the DFS channels in Europe and the US.
 *
 * @sideeffects None; errno unchanged.
 */
bool chan_5g_dfs(uint8_t channel);

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
 * A hidden network is named by its BSSID (its MAC address),
 * "hidden 02:1f:3c:12:34:56", which tells the access points apart and can
 * be looked up to find the maker; that takes 25 bytes.
 *
 * An SSID too long for @p buf is shortened in the middle, keeping its
 * start and end, "JurassicFi..UID35630", as the end often tells similar
 * names apart. Lengths are in bytes, so a name with multi-byte UTF-8
 * characters may be shortened sooner than it needs to be.
 *
 * @param ap The access point.
 * @param[out] buf The name, shortened to fit and always terminated.
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
 * @brief Name a level, as the screens and the console show it.
 *
 * @param level enum chan_level.
 * @return "clear", "moderate" or "busy"; "?" for a value that isn't a
 *         level.
 *
 * @sideeffects None; errno unchanged.
 */
const char *chan_level_name(enum chan_level level);

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
