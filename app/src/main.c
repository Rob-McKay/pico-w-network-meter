/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pico W network meter: scans the 2.4 GHz Wi-Fi band every
 * CONFIG_METER_SCAN_INTERVAL_S seconds and shows per-channel statistics on
 * a Pimoroni Pico Display Pack 2.0, and on the serial console.
 *
 * Buttons: A next screen, B previous screen, X scan now.
 * RGB LED: blue while scanning, then green, amber or red for how busy the
 * recommended channel is; magenta for an error.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "chan_stats.h"
#include "controls.h"
#include "scanner.h"
#include "status_led.h"
#include "ui.h"

LOG_MODULE_REGISTER(meter, CONFIG_METER_LOG_LEVEL);

/* The scan in progress, and the last complete one, which is on screen. */
static struct chan_stats scanning;
static struct chan_stats shown;

/**
 * @brief Log a scan's channel statistics on the console.
 *
 * @param s Statistics.
 * @param scan_no Scan number.
 */
static void report(const struct chan_stats *s, unsigned int scan_no)
{
	LOG_INF("Scan %u: %u networks on 2.4 GHz, %u other, best channel %u", scan_no,
		(unsigned int)s->ap_count, (unsigned int)(s->ignored + s->dropped),
		chan_stats_recommend(s));
	for (uint8_t ch = CHAN_FIRST; ch <= CHAN_LAST; ch++) {
		struct chan_info info;

		chan_stats_channel(s, ch, &info);
		if (info.ap_count > 0U || info.interference != CHAN_RSSI_NONE) {
			LOG_INF("  ch %2u: %u networks, strongest %4d dBm, interference %4d dBm",
				ch, info.ap_count, info.best_rssi, info.interference);
		}
	}
	for (size_t i = 0; i < s->ap_count; i++) {
		LOG_DBG("  %-32s ch %2u %4d dBm", s->aps[i].ssid, s->aps[i].channel,
			s->aps[i].rssi);
	}
}

/**
 * @brief Pick the LED status for a scan: how busy the best channel is.
 *
 * @param s Statistics.
 * @return The status.
 */
static enum status scan_status(const struct chan_stats *s)
{
	struct chan_info best;

	chan_stats_channel(s, chan_stats_recommend(s), &best);
	switch (chan_stats_level(best.interference)) {
	case CHAN_LEVEL_BUSY:
		return STATUS_BUSY;
	case CHAN_LEVEL_MODERATE:
		return STATUS_MODERATE;
	default:
		return STATUS_CLEAR;
	}
}

/**
 * @brief Run one scan and show it.
 *
 * A scan that times out still shows what it heard. A failed one leaves the
 * last results on screen.
 *
 * @param[in,out] scan_no Scans shown so far; incremented if this one is.
 * @param view Screen to draw.
 */
static void scan_once(unsigned int *scan_no, enum ui_view view)
{
	int ret;

	status_set(STATUS_SCANNING);
	ret = scanner_run(&scanning, K_SECONDS(CONFIG_METER_SCAN_TIMEOUT_S));
	if (ret != 0 && ret != -EAGAIN) {
		char msg[32];

		LOG_WRN("Scan failed: %d", ret);
		(void)snprintf(msg, sizeof(msg), "Scan failed (%d)", ret);
		ui_message(msg);
		status_set(STATUS_ERROR);
		return;
	}
	if (ret == -EAGAIN) {
		LOG_WRN("Scan timed out: showing partial results");
	}

	shown = scanning;
	(*scan_no)++;
	ui_show(view, &shown, *scan_no, *scan_no == 1U);
	report(&shown, *scan_no);
	status_set(scan_status(&shown));
}

/**
 * @brief Handle buttons until the next scan is due.
 *
 * @param[in,out] view Screen shown; A and B change it.
 * @param scan_no Scans shown so far (0: nothing to redraw yet).
 */
static void wait_for_next_scan(enum ui_view *view, unsigned int scan_no)
{
	int64_t due = k_uptime_get() + ((int64_t)CONFIG_METER_SCAN_INTERVAL_S * MSEC_PER_SEC);

	for (int64_t now = k_uptime_get(); now < due; now = k_uptime_get()) {
		int key = controls_wait(K_MSEC(due - now));

		if (key == INPUT_KEY_X) {
			return;
		}
		if (key == INPUT_KEY_A) {
			*view = (enum ui_view)(((int)*view + 1) % UI_VIEW_COUNT);
		} else if (key == INPUT_KEY_B) {
			*view = (enum ui_view)(((int)*view + UI_VIEW_COUNT - 1) % UI_VIEW_COUNT);
		} else {
			continue;
		}
		if (scan_no > 0U) {
			ui_show(*view, &shown, scan_no, true);
		}
	}
}

/**
 * @brief Entry point: start the display and Wi-Fi, then scan for ever.
 *
 * @return 0 if the Wi-Fi interface is missing (nothing else to do).
 */
int main(void)
{
	enum ui_view view = UI_VIEW_CHANNELS;
	unsigned int scan_no = 0;

	controls_init();
	/* Clear the panel before the backlight shows its power-on contents. */
	if (ui_init() != 0) {
		LOG_ERR("Display not ready: console output only");
	}
	if (status_init() != 0) {
		LOG_ERR("LEDs not ready");
	}
	ui_message("Scanning...");

	if (scanner_init() != 0) {
		ui_message("No Wi-Fi interface");
		status_set(STATUS_ERROR);
		return 0;
	}

	for (;;) {
		scan_once(&scan_no, view);
		wait_for_next_scan(&view, scan_no);
	}
}
