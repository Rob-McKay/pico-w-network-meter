/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/util.h>

#include "scanner.h"

LOG_MODULE_REGISTER(scanner, CONFIG_METER_LOG_LEVEL);

#define SCAN_EVENTS (NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)

static struct net_if *wifi_iface;
static struct net_mgmt_event_callback scan_cb;

/* The scan in progress: results go to *target while it is set. The mutex
 * covers target and scan_status against the net_mgmt event thread.
 */
static K_MUTEX_DEFINE(scan_lock);
static struct chan_stats *target;
static int scan_status;
/* Results the driver delivered for the scan, before any were merged. */
static unsigned int raw_results;
/* Zephyr's definition macro can end in an empty statement: ISO C -Wpedantic
 * flags the ';' as extra. Nothing else is exempted.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static K_SEM_DEFINE(scan_done, 0, 1);
#pragma GCC diagnostic pop

/**
 * @brief Convert a Zephyr scan result to an access point record.
 *
 * Only 2.4 GHz results get a channel; anything else is left at 0, which
 * chan_stats_add() counts as ignored.
 *
 * @param r Zephyr's scan result.
 * @param[out] ap The access point.
 *
 * @sideeffects None (writes only through @p ap); errno unchanged.
 */
static void to_chan_ap(const struct wifi_scan_result *r, struct chan_ap *ap)
{
	size_t len = MIN((size_t)r->ssid_length, (size_t)CHAN_SSID_MAX);

	memset(ap, 0, sizeof(*ap));
	memcpy(ap->ssid, r->ssid, len);
	ap->ssid[len] = '\0';
	memcpy(ap->bssid, r->mac, MIN((size_t)r->mac_length, sizeof(ap->bssid)));
	ap->channel = r->band == WIFI_FREQ_BAND_2_4_GHZ ? r->channel : 0U;
	ap->rssi = r->rssi;
	ap->secured = r->security != WIFI_SECURITY_TYPE_NONE;
	ap->freshness = CHAN_FRESH;
}

/**
 * @brief Tell whether a scan result is an empty record.
 *
 * The driver sometimes passes on a record with nothing in it: no BSSID,
 * channel or signal. It is not a network (a hidden network still has a
 * BSSID), so it is dropped rather than counted.
 *
 * @param ap The result.
 * @return True if its BSSID is all zeros.
 *
 * @sideeffects None; errno unchanged.
 */
static bool is_empty_record(const struct chan_ap *ap)
{
	static const uint8_t zero[CHAN_BSSID_LEN];

	return memcmp(ap->bssid, zero, sizeof(zero)) == 0;
}

/**
 * @brief Add a scan result to the scan in progress (net_mgmt event thread).
 *
 * Results arriving when no scan is waiting (after a timeout) are dropped.
 *
 * @param r The result.
 *
 * @sideeffects Logs the result; under @c scan_lock, adds it to the waiting scan's statistics
 *              (@c target) and counts it in @c raw_results. errno unchanged.
 */
static void on_result(const struct wifi_scan_result *r)
{
	struct chan_ap ap;

	to_chan_ap(r, &ap);
	if (is_empty_record(&ap)) {
		return;
	}
	LOG_DBG("Result: %02x:%02x:%02x:%02x:%02x:%02x ch %u %d dBm \"%s\"", ap.bssid[0],
		ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
		(unsigned int)ap.channel, ap.rssi, ap.ssid);

	k_mutex_lock(&scan_lock, K_FOREVER);
	if (target != NULL) {
		raw_results++;
		(void)chan_stats_add(target, &ap);
	}
	k_mutex_unlock(&scan_lock);
}

/**
 * @brief End the scan in progress (net_mgmt event thread).
 *
 * A "done" with no scan waiting (one that timed out) is dropped, so it
 * can't end a later scan. The driver's events carry no scan identity, so
 * one that arrives after the next scan has started still ends that scan
 * early; CONFIG_METER_SCAN_TIMEOUT_S, far longer than a scan, keeps that
 * unlikely.
 *
 * @param st The driver's status, or NULL.
 *
 * @sideeffects Under @c scan_lock, sets @c scan_status; gives @c scan_done, waking
 *              scanner_run(), if a scan is waiting. errno unchanged.
 */
static void on_done(const struct wifi_status *st)
{
	bool waiting;

	k_mutex_lock(&scan_lock, K_FOREVER);
	waiting = target != NULL;
	if (waiting) {
		scan_status = (st != NULL && st->status != 0) ? -EIO : 0;
	}
	k_mutex_unlock(&scan_lock);

	if (waiting) {
		k_sem_give(&scan_done);
	}
}

/**
 * @brief Dispatch a Wi-Fi scan event (net_mgmt event thread).
 *
 * @param cb The callback, carrying the event's info.
 * @param mgmt_event NET_EVENT_WIFI_SCAN_RESULT or NET_EVENT_WIFI_SCAN_DONE.
 * @param iface Interface (unused: there is one).
 *
 * @sideeffects Those of on_result() or on_done(). errno unchanged.
 */
static void on_scan_event(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			  struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT && cb->info != NULL) {
		on_result((const struct wifi_scan_result *)cb->info);
	} else if (mgmt_event == NET_EVENT_WIFI_SCAN_DONE) {
		on_done((const struct wifi_status *)cb->info);
	}
}

/*
 * Public interface: documented in scanner.h.
 *
 * Prefers the station interface, falling back to the first Wi-Fi interface.
 */
int scanner_init(void)
{
	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		wifi_iface = net_if_get_first_wifi();
	}
	if (wifi_iface == NULL) {
		LOG_ERR("No Wi-Fi interface");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&scan_cb, on_scan_event, SCAN_EVENTS);
	net_mgmt_add_event_callback(&scan_cb);

	return 0;
}

/**
 * @brief Point incoming results at @p out, emptied, for a new scan.
 *
 * @param[out] out Where the scan's results go.
 *
 * @sideeffects Under @c scan_lock: empties @p out and makes it the scan's @c target, clears @c
 *              scan_status and @c raw_results, and resets @c scan_done. errno unchanged.
 */
static void arm(struct chan_stats *out)
{
	k_mutex_lock(&scan_lock, K_FOREVER);
	chan_stats_reset(out);
	target = out;
	scan_status = 0;
	raw_results = 0;
	k_sem_reset(&scan_done);
	k_mutex_unlock(&scan_lock);
}

/**
 * @brief Stop taking results, and log how the scan went.
 *
 * @param ret 0, or the error so far (failed request, -ETIMEDOUT timeout).
 * @return @p ret, or the driver's status if the scan completed.
 *
 * @sideeffects Under @c scan_lock, clears @c target, so later results are dropped; logs the
 *              scan's outcome. errno unchanged.
 */
static int disarm(int ret)
{
	k_mutex_lock(&scan_lock, K_FOREVER);
	if (ret == 0) {
		ret = scan_status;
	}
	LOG_INF("Scan ended (%d): %u results from the driver, %u networks kept", ret, raw_results,
		(unsigned int)target->ap_count);
	target = NULL;
	k_mutex_unlock(&scan_lock);

	return ret;
}

/**
 * @brief Ask the driver to scan every channel.
 *
 * @retval 0 Started.
 * @retval <0 Negative errno from the request.
 *
 * @sideeffects Starts a scan in the Wi-Fi driver and radio; logs a failure. errno unchanged.
 */
static int request_scan(void)
{
	struct wifi_scan_params params = {
		.scan_type = IS_ENABLED(CONFIG_METER_SCAN_PASSIVE) ? WIFI_SCAN_TYPE_PASSIVE
								   : WIFI_SCAN_TYPE_ACTIVE,
	};
	int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, wifi_iface, &params, sizeof(params));

	if (ret != 0) {
		LOG_ERR("Scan request failed: %d", ret);
	}

	return ret;
}

/*
 * Public interface: documented in scanner.h.
 *
 * Arms the scan target before the request, so no result is missed, and disarms it afterwards.
 * A timeout is -ETIMEDOUT, not -EAGAIN, which the driver returns when a scan can't start.
 * on_done() explains the one race the driver leaves.
 */
int scanner_run(struct chan_stats *out, k_timeout_t timeout)
{
	int ret;

	if (wifi_iface == NULL) {
		return -ENODEV;
	}

	arm(out);
	ret = request_scan();
	if (ret == 0 && k_sem_take(&scan_done, timeout) != 0) {
		ret = -ETIMEDOUT;
	}

	return disarm(ret);
}
