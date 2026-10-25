/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flow control for the console on the Pico's USB port, only while a host
 * has the port open.
 *
 * The CDC-ACM driver drops output it can't buffer at once, which garbles
 * bursts of log lines; with flow control it waits instead, but then it
 * waits for ever when no host is reading (and the boot banner would stop
 * the Pico starting). So flow control follows DTR, which a terminal raises
 * when it opens the port and drops when it closes it, checked every
 * CHECK_MS on the system work queue. Only the logging thread ever waits.
 *
 * uart_configure() here and the driver's poll_out() in the logging thread
 * share the driver's flow-control flag without a lock: a data race, but on
 * a single byte, which the Cortex-M0+ reads and writes whole, and poll_out()
 * reads it afresh every millisecond it waits, so it sees a change at once.
 * The driver offers no locked way to change it.
 */

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

/* How often DTR is checked, ms. */
#define CHECK_MS 200

static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static struct k_work_delayable watch;
static bool flow_on;

/**
 * @brief Turn the console's flow control on or off.
 *
 * @param on On.
 *
 * @sideeffects Reconfigures the console UART's flow control and records it in @c flow_on. errno
 *              unchanged.
 */
static void set_flow(bool on)
{
	struct uart_config cfg;

	if (uart_config_get(console, &cfg) != 0) {
		return;
	}
	cfg.flow_ctrl = on ? UART_CFG_FLOW_CTRL_RTS_CTS : UART_CFG_FLOW_CTRL_NONE;
	if (uart_configure(console, &cfg) == 0) {
		flow_on = on;
	}
}

/**
 * @brief Match flow control to DTR, then check again in CHECK_MS.
 *
 * @param work The delayable work item.
 *
 * @sideeffects Those of set_flow() when DTR has changed; reschedules itself on the system work
 *              queue. errno unchanged.
 */
static void watch_dtr(struct k_work *work)
{
	uint32_t dtr = 0;
	bool open;

	ARG_UNUSED(work);
	if (uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr) == 0) {
		open = dtr != 0U;
		if (open != flow_on) {
			set_flow(open);
		}
	}
	(void)k_work_reschedule(&watch, K_MSEC(CHECK_MS));
}

/**
 * @brief Start watching DTR.
 *
 * @return 0 (a console that isn't ready is left alone).
 *
 * @sideeffects Initialises @c watch and schedules it on the system work queue. errno unchanged.
 */
static int usb_console_init(void)
{
	if (device_is_ready(console)) {
		k_work_init_delayable(&watch, watch_dtr);
		(void)k_work_schedule(&watch, K_MSEC(CHECK_MS));
	}

	return 0;
}

/* Zephyr's definition macro can end in an empty statement: ISO C -Wpedantic
 * flags the ';' as extra. Nothing else is exempted.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
SYS_INIT(usb_console_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#pragma GCC diagnostic pop
