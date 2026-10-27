/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pico W network meter: scans the 2.4 GHz Wi-Fi band every
 * CONFIG_METER_SCAN_INTERVAL_S seconds (scan_task.c) and shows per-channel
 * statistics on a Pimoroni Pico Display Pack 2.0, and on the serial
 * console.
 *
 * Networks stay on screen after they were last heard: at full brightness
 * for CONFIG_METER_FADE_START_S seconds, then fading to black by
 * CONFIG_METER_FADE_END_S. The screen is redrawn every
 * CONFIG_METER_REDRAW_MS while anything changes.
 *
 * Each start is logged in flash with why it happened (event_log.c,
 * reset_note.c), and shown on the events screen. A watchdog restarts the
 * meter if a thread stops responding (watchdog.c).
 *
 * Buttons: A next screen, B previous screen. On the channels screen X scans
 * now; on the networks and events screens X and Y page up and down a list
 * too long for the screen.
 * RGB LED: blue while scanning, then green, amber or red for how busy the
 * recommended channel is; magenta if the latest scan failed.
 */

#include <stdbool.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "boot_record.h"
#include "chan_stats.h"
#include "controls.h"
#include "event_log.h"
#include "paging.h"
#include "reset_note.h"
#include "scan_task.h"
#include "scanner.h"
#include "status_led.h"
#include "ui.h"
#include "watchdog.h"

LOG_MODULE_REGISTER(meter, CONFIG_METER_LOG_LEVEL);

/* Starts printed on the console at start-up, newest first. */
#define STARTS_ON_CONSOLE 10U
/* Longest description of a start, and a NUL. */
#define START_TEXT_MAX    40

/* The main thread's watchdog deadline: it goes round every CONFIG_METER_REDRAW_MS. */
#define MAIN_DEADLINE_MS 5000U
BUILD_ASSERT(MAIN_DEADLINE_MS >= 4U * CONFIG_METER_REDRAW_MS,
	     "the main thread's watchdog deadline must allow several redraws");

/**
 * @brief Map how busy a channel is to the LED status that shows it.
 *
 * @param level How busy.
 * @return Green, amber or red status.
 *
 * @sideeffects None; errno unchanged.
 */
static enum status level_status(enum chan_level level)
{
	switch (level) {
	case CHAN_LEVEL_BUSY:
		return STATUS_BUSY;
	case CHAN_LEVEL_MODERATE:
		return STATUS_MODERATE;
	default:
		return STATUS_CLEAR;
	}
}

/**
 * @brief Pick the LED status: scanning (or no scan finished yet), failed,
 *        or how busy the recommended channel is.
 *
 * @param s The networks showing.
 * @param state The scan thread's progress.
 * @return The status.
 *
 * @sideeffects None; errno unchanged.
 */
static enum status led_status(const struct chan_stats *s, const struct scan_task_state *state)
{
	struct chan_info best;

	if (state->busy || state->scans == 0U) {
		return STATUS_SCANNING;
	}
	if (state->last_error != 0) {
		return STATUS_ERROR;
	}
	chan_stats_channel(s, chan_stats_recommend(s), &best);

	return level_status(chan_stats_level(best.interference));
}

/* What the screen shows, to redraw only when something changes. */
struct screen {
	enum ui_view view;
	size_t first;             /* networks or events screen: first row shown */
	bool full;                /* the whole screen needs drawing */
	unsigned int drawn_scans; /* scan count on screen */
	struct chan_stats shown;  /* networks on screen */
};

/**
 * @brief Change the screen shown, back to the top of its list.
 *
 * @param[in,out] scr What is on screen.
 * @param step 1 for the next screen, -1 for the previous one.
 *
 * @sideeffects None (writes only through @p scr); errno unchanged.
 */
static void change_view(struct screen *scr, int step)
{
	scr->view = (enum ui_view)(((int)scr->view + UI_VIEW_COUNT + step) % UI_VIEW_COUNT);
	scr->first = 0;
}

/**
 * @brief Count the rows of the list on screen.
 *
 * @param scr What is on screen.
 * @return Networks or events; 0 on the channels screen.
 *
 * @sideeffects None; errno unchanged.
 */
static size_t list_length(const struct screen *scr)
{
	switch (scr->view) {
	case UI_VIEW_NETWORKS:
		return scr->shown.ap_count;
	case UI_VIEW_EVENTS:
		return event_log_count();
	default:
		return 0;
	}
}

/**
 * @brief Page the list on screen up or down.
 *
 * @param[in,out] scr What is on screen.
 * @param down Down a page if true, up if false.
 * @return True if the page changed.
 *
 * @sideeffects None (writes only through @p scr); errno unchanged.
 */
static bool page_list(struct screen *scr, bool down)
{
	size_t was = scr->first;

	scr->first = paging_move(scr->first, down, list_length(scr), ui_list_rows());

	return scr->first != was;
}

/**
 * @brief Act on a button: A and B change the screen; X and Y page the
 *        networks and events screens, and X scans now on the channels
 *        screen.
 *
 * @param key Key code from controls_wait(), or a negative errno.
 * @param[in,out] scr What is on screen.
 * @return True if the screen needs redrawing in full.
 *
 * @sideeffects Writes @p scr through its pointer; X on the channels screen wakes the scan
 *              thread (scan_task_request()). errno unchanged.
 */
static bool handle_key(int key, struct screen *scr)
{
	bool list = scr->view != UI_VIEW_CHANNELS;

	switch (key) {
	case INPUT_KEY_A:
		change_view(scr, 1);
		return true;
	case INPUT_KEY_B:
		change_view(scr, -1);
		return true;
	case INPUT_KEY_X:
		if (list) {
			return page_list(scr, false);
		}
		scan_task_request();
		return false;
	case INPUT_KEY_Y:
		return list && page_list(scr, true);
	default:
		return false;
	}
}

/**
 * @brief Redraw the screen if the networks, their fading, the scan count
 *        or the view have changed.
 *
 * @param[in,out] scr What is on screen.
 * @param next The networks to show.
 * @param scans Scans finished so far.
 *
 * The page shown is kept within the list, which may have shrunk.
 *
 * @sideeffects Draws on the panel (ui_show()) and updates @p scr. errno may be set by
 *              snprintf() (only on an output error).
 */
static void update_screen(struct screen *scr, const struct chan_stats *next, unsigned int scans)
{
	if (!scr->full && scans == scr->drawn_scans && chan_stats_equal(next, &scr->shown)) {
		return;
	}
	scr->shown = *next;
	scr->first = paging_clamp(scr->first, list_length(scr), ui_list_rows());
	ui_show(scr->view, &scr->shown, scans, scr->first, scr->full);
	scr->drawn_scans = scans;
	scr->full = false;
}

/**
 * @brief Start the display, LEDs and buttons.
 *
 * Failures are logged; the meter carries on with what works.
 *
 * @sideeffects Initialises the button queue, the display (cleared) and the LEDs (backlight on,
 *              RGB LED off), and writes "Scanning..." in the footer; logs failures. errno may
 *              be set by snprintf() (only on an output error).
 */
static void start_io(void)
{
	controls_init();
	/* Clear the panel before the backlight shows its power-on contents. */
	if (ui_init() != 0) {
		LOG_ERR("Display not ready: console output only");
	}
	if (status_init() != 0) {
		LOG_ERR("LEDs not ready");
	}
	ui_message("Scanning...");
}

/**
 * @brief Log this start in flash, with why it happened, and show the log
 *        on the console and the events screen.
 *
 * Takes the note the run before left (reset_note_take()) first, before
 * anything else can restart the meter.
 *
 * @sideeffects Those of reset_note_take(), event_log_init() and event_log_add(): starts this
 *              run's note, reads and writes the flash log; logs the newest starts; gives the
 *              UI its event source (ui_set_events()). errno may be set by snprintf() (only on
 *              an output error).
 */
static void log_start(void)
{
	struct boot_note note;
	struct boot_record r;
	char text[START_TEXT_MAX];

	reset_note_take(&note);
	if (event_log_init() != 0) {
		return;
	}
	boot_record_from_note(&note, event_log_last_boot() + 1U, &r);
	(void)event_log_add(&r);
	for (size_t i = 0; i < MIN(event_log_count(), (size_t)STARTS_ON_CONSOLE); i++) {
		const struct boot_record *e = event_log_get(i);

		boot_record_describe(e, text, sizeof(text));
		LOG_INF("Start %s (detail %d, 0x%08x)", text, (int)e->detail, (unsigned int)e->arg);
	}
	ui_set_events(event_log_count(), event_log_get);
}

/**
 * @brief Entry point: start the display and Wi-Fi, then redraw for ever.
 *
 * @return 0 if the Wi-Fi interface is missing (nothing else to do).
 *
 * @sideeffects Runs for ever: logs the start (log_start()), starts the watchdog and the scan
 *              thread, and drives the display, the RGB LED and the button queue; feeds the
 *              main thread's watchdog channel and keeps the restart note's uptime. Returns
 *              only if there is no Wi-Fi interface. errno may be set by snprintf() (only on an
 *              output error).
 */
int main(void)
{
	/* Static: struct screen holds a whole scan, too big for the stack. */
	static struct screen scr = {.view = UI_VIEW_CHANNELS, .full = true};
	static struct chan_stats next;
	int watch;

	log_start();
	start_io();
	if (scanner_init() != 0) {
		ui_message("No Wi-Fi interface");
		status_set(STATUS_ERROR);
		return 0;
	}
	(void)watchdog_init();
	watch = watchdog_add(BOOT_WATCHED_MAIN, MAIN_DEADLINE_MS);
	scan_task_start();

	for (;;) {
		struct scan_task_state state;

		watchdog_feed(watch);
		reset_note_alive(k_uptime_get());

		if (handle_key(controls_wait(K_MSEC(CONFIG_METER_REDRAW_MS)), &scr)) {
			scr.full = true;
		}
		scan_task_snapshot(k_uptime_get(), &next, &state);
		status_set(led_status(&next, &state));
		if (state.scans > 0U) { /* "Scanning..." until the first scan ends */
			update_screen(&scr, &next, state.scans);
		}
	}
}
