/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
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
}

/**
 * @brief Handle a Wi-Fi scan event (net_mgmt event thread).
 *
 * @param cb The callback, carrying the event's info.
 * @param mgmt_event NET_EVENT_WIFI_SCAN_RESULT or NET_EVENT_WIFI_SCAN_DONE.
 * @param iface Interface (unused: there is one).
 */
static void on_scan_event(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			  struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT && cb->info != NULL) {
		struct chan_ap ap;

		to_chan_ap((const struct wifi_scan_result *)cb->info, &ap);
		k_mutex_lock(&scan_lock, K_FOREVER);
		if (target != NULL) {
			(void)chan_stats_add(target, &ap);
		}
		k_mutex_unlock(&scan_lock);
	} else if (mgmt_event == NET_EVENT_WIFI_SCAN_DONE) {
		const struct wifi_status *st = (const struct wifi_status *)cb->info;

		k_mutex_lock(&scan_lock, K_FOREVER);
		scan_status = (st != NULL && st->status != 0) ? -EIO : 0;
		k_mutex_unlock(&scan_lock);
		k_sem_give(&scan_done);
	}
}

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

int scanner_run(struct chan_stats *out, k_timeout_t timeout)
{
	int ret;

	k_mutex_lock(&scan_lock, K_FOREVER);
	chan_stats_reset(out);
	target = out;
	scan_status = 0;
	k_mutex_unlock(&scan_lock);
	k_sem_reset(&scan_done);

	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, wifi_iface, NULL, 0);
	if (ret == 0) {
		ret = k_sem_take(&scan_done, timeout) == 0 ? 0 : -EAGAIN;
	} else {
		LOG_ERR("Scan request failed: %d", ret);
	}

	k_mutex_lock(&scan_lock, K_FOREVER);
	target = NULL;
	if (ret == 0) {
		ret = scan_status;
	}
	k_mutex_unlock(&scan_lock);

	return ret;
}
