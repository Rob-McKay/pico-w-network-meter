/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Why the Pico W's Wi-Fi stops answering: after a failed scan, what the
 * CYW43439's interrupt line and the WHD library say, and whether waking the
 * WHD thread gets anything out of the chip.
 *
 * The chip signals the Pico on GPIO 24, which it shares with SPI data. The
 * WHD thread sends queued messages only while the chip has granted it
 * credits, and sleeps with no timeout until woken by that signal or by a
 * new message to send. Reads WHD-internal state, with the scheduler locked
 * so the WHD thread can't change it meanwhile (Zephyr runs on one of the
 * RP2040's cores, and no interrupt handler writes these fields).
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <cyabs_rtos_impl.h>
#include <whd_bus_common.h>
#include <whd_bus_protocol_interface.h>
#include <whd_chip.h>
#include <whd_chip_constants.h>
#include <whd_int.h>
#include <whd_sdio.h>
#include <whd_sdpcm.h>
#include <whd_spi.h>
#include <whd_thread.h>
#include <whd_types.h>
#include <whd_wifi_api.h>

#include "wifi_debug.h"
#include "wifi_irq_state.h"

LOG_MODULE_REGISTER(wifi_debug, CONFIG_METER_LOG_LEVEL);

/**
 * @brief Get the AIROC driver's WHD station interface.
 *
 * Declared here because its header, drivers/wifi/infineon/airoc_wifi.h, needs the driver's
 * own include paths.
 *
 * @return The interface; NULL before the driver has set it up.
 *
 * @sideeffects None; errno unchanged.
 */
whd_interface_t airoc_wifi_get_whd_interface(void);

/** Samples of the line, and the time between them, ms: about 10 ms in all. */
#define IRQ_SAMPLES    10U
#define IRQ_SAMPLE_GAP 1

/** How long to give the WHD thread after waking it, ms. */
#define WAKE_WAIT_MS 100

/** How long to let the WHD thread settle after a scan before reading the chip, ms. */
#define SETTLE_MS 500

/* The chip's ChipCommon core, whose first register holds the chip's ID in its low 16 bits:
 * at the same backplane address on every Broadcom/Cypress Wi-Fi chip (SI_ENUM_BASE).
 */
#define CHIPCOMMON_ID_ADDRESS 0x18000000U
#define CHIP_ID_MASK          0xffffU

/** The longest line of the chip's console log printed, with its terminator. */
#define CONSOLE_LINE 128U

/* The driver asks the chip for an active-low interrupt and waits for a falling edge
 * (CY_WIFI_HOST_WAKE_IRQ_EVENT is GPIO_INT_TRIG_LOW); the devicetree flags are active high,
 * so a level of 0 is asserted.
 */
#define IRQ_ASSERTED_LEVEL 0

static const struct gpio_dt_spec host_wake =
	GPIO_DT_SPEC_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(infineon_airoc_wifi), wifi_host_wake_gpios);

/** What the WHD library was doing, at one moment. */
struct whd_snapshot {
	uint8_t tx_seq;          /**< the next message's sequence number */
	uint8_t tx_max;          /**< the chip's credit: may send up to, not including, this */
	uint8_t credits;         /**< messages the chip will take now */
	uint32_t queued;         /**< messages waiting to be sent */
	uint32_t queued_control; /**< of which control messages (requests) */
	bool bus_up;             /**< WHD thinks the chip's bus is awake */
	bool irq_flag;           /**< the WHD thread has an interrupt to handle */
	bool waiting;            /**< the WHD thread is asleep on its semaphore */
	char state[32];          /**< the WHD thread's state, as Zephyr names it */
};

/**
 * @brief Find the WHD library's driver.
 *
 * @return It; NULL if the AIROC driver hasn't set it up.
 *
 * @sideeffects None; errno unchanged.
 */
static whd_driver_t whd_driver_get(void)
{
	whd_interface_t iface = airoc_wifi_get_whd_interface();

	return iface == NULL ? NULL : iface->whd_driver;
}

/**
 * @brief Sample the chip's interrupt line.
 *
 * Reads the pad, whichever of the PIO (SPI data) and SIO (interrupt) has it at the time:
 * reading doesn't disturb either.
 *
 * @param[out] asserted IRQ_SAMPLES samples: true where the line was asserted.
 * @retval 0 Sampled.
 * @retval -ENODEV The GPIO port isn't ready.
 * @retval <0 Another negative errno from gpio_pin_get_dt().
 *
 * @sideeffects Reads GPIO 24; sleeps IRQ_SAMPLE_GAP ms between samples, blocking the calling
 *              thread for about 10 ms. errno unchanged.
 */
static int sample_line(bool *asserted)
{
	if (!gpio_is_ready_dt(&host_wake)) {
		return -ENODEV;
	}
	for (unsigned int i = 0; i < IRQ_SAMPLES; i++) {
		int level = gpio_pin_get_dt(&host_wake);

		if (level < 0) {
			return level;
		}
		asserted[i] = level == IRQ_ASSERTED_LEVEL;
		if (i + 1U < IRQ_SAMPLES) {
			k_msleep(IRQ_SAMPLE_GAP);
		}
	}

	return 0;
}

/**
 * @brief Copy the WHD library's state.
 *
 * @param whd The WHD driver.
 * @param[out] s Its state.
 *
 * @sideeffects Locks and unlocks the scheduler (the WHD thread can't run meanwhile); reads
 *              WHD's credit, queue, bus and thread fields. errno unchanged.
 */
static void snapshot(whd_driver_t whd, struct whd_snapshot *s)
{
	const whd_sdpcm_info_t *sdpcm = &whd->sdpcm_info;
	struct k_thread *thread = &whd->thread_info.whd_thread->z_thread;

	k_sched_lock();
	s->tx_seq = sdpcm->tx_seq;
	s->tx_max = sdpcm->tx_max;
	s->credits = whd_sdpcm_get_available_credits(whd);
	s->queued = sdpcm->totpkt_in_q;
	s->queued_control = sdpcm->npkt_in_q[ARRAY_SIZE(sdpcm->npkt_in_q) - 1U];
	s->bus_up = whd_bus_is_up(whd) == WHD_TRUE;
	s->irq_flag = whd->thread_info.bus_interrupt == WHD_TRUE;
	s->waiting = thread->base.pended_on == &whd->thread_info.transceive_semaphore.wait_q;
	(void)k_thread_state_str(thread, s->state, sizeof(s->state));
	k_sched_unlock();
}

/**
 * @brief Log a copy of the WHD library's state.
 *
 * @param when When it was taken, for the log.
 * @param s The state.
 *
 * @sideeffects Logs a line. errno unchanged.
 */
static void log_snapshot(const char *when, const struct whd_snapshot *s)
{
	LOG_INF("WHD %s: credits %u (seq %u, max %u), queued %u (%u requests), bus %s, "
		"interrupt flag %s, thread %s%s",
		when, (unsigned int)s->credits, (unsigned int)s->tx_seq, (unsigned int)s->tx_max,
		(unsigned int)s->queued, (unsigned int)s->queued_control, s->bus_up ? "up" : "down",
		s->irq_flag ? "set" : "clear", s->state,
		s->waiting ? ", asleep waiting to be woken" : "");
}

/**
 * @brief Ask the chip's bus to stay awake, and wake it.
 *
 * As WHD_WLAN_KEEP_AWAKE() does, without its assertion; undo with let_bus_sleep().
 *
 * @param whd The WHD driver.
 * @retval 0 Awake.
 * @retval -EIO It didn't wake (the WHD error is logged).
 *
 * @sideeffects Counts a keep-awake request in WHD's state; drives the chip's bus to wake it;
 *              logs a failure. errno unchanged.
 */
static int keep_bus_awake(whd_driver_t whd)
{
	whd_result_t res;

	whd->internal_info.whd_wlan_status.keep_wlan_awake++;
	res = whd_ensure_wlan_bus_is_up(whd);
	if (res != WHD_SUCCESS) {
		LOG_ERR("The Wi-Fi chip's bus won't wake: 0x%08x", (unsigned int)res);
		return -EIO;
	}

	return 0;
}

/**
 * @brief Undo keep_bus_awake().
 *
 * As WHD_WLAN_LET_SLEEP() does.
 *
 * @param whd The WHD driver.
 *
 * @sideeffects Removes a keep-awake request from WHD's state; if none are left, wakes the
 *              WHD thread (whd_thread_notify()), which lets the bus sleep. errno unchanged.
 */
static void let_bus_sleep(whd_driver_t whd)
{
	whd->internal_info.whd_wlan_status.keep_wlan_awake--;
	if (whd->internal_info.whd_wlan_status.keep_wlan_awake == 0U) {
		whd_thread_notify(whd);
	}
}

/** What the chip's bus registers read. */
struct chip_regs {
	uint32_t test;    /**< gSPI test register: 0xFEEDBEAD if the bus works */
	uint8_t clock;    /**< clock status: SBSDIO_ALP_AVAIL, SBSDIO_HT_AVAIL */
	uint8_t sleep;    /**< sleep control: SBSDIO_SLPCSR_KEEP_WL_KSO, _WL_DEVON */
	uint32_t id;      /**< ChipCommon's first register: the chip ID in its low 16 bits */
	whd_result_t res; /**< the first read that failed; WHD_SUCCESS if none */
};

/**
 * @brief Read the chip's bus registers: its gSPI test register (no clock
 *        or firmware needed), its clock and sleep status, and its chip ID
 *        (over the backplane, which needs its clocks).
 *
 * @param whd The WHD driver, its bus awake.
 * @param[out] r What they read; a read that failed leaves 0.
 *
 * @sideeffects Reads the registers over the chip's bus. errno unchanged.
 */
static void read_chip_regs(whd_driver_t whd, struct chip_regs *r)
{
	whd_result_t res[4];

	*r = (struct chip_regs){0};
	res[0] = whd_bus_read_register_value(whd, BUS_FUNCTION, SPI_READ_TEST_REGISTER, 4,
					     (uint8_t *)&r->test);
	res[1] = whd_bus_read_register_value(whd, BACKPLANE_FUNCTION, SDIO_CHIP_CLOCK_CSR, 1,
					     &r->clock);
	res[2] = whd_bus_read_register_value(whd, BACKPLANE_FUNCTION, SDIO_SLEEP_CSR, 1, &r->sleep);
	res[3] = whd_bus_read_backplane_value(whd, CHIPCOMMON_ID_ADDRESS, 4, (uint8_t *)&r->id);
	r->res = WHD_SUCCESS;
	for (size_t i = 0; i < ARRAY_SIZE(res); i++) {
		if (res[i] != WHD_SUCCESS) {
			r->res = res[i];
			break;
		}
	}
}

/**
 * @brief Log the chip's bus registers, and tell whether its ID is right.
 *
 * WHD's console reader loops until the chip gives it a valid address, so it is only run
 * once the chip has given its ID.
 *
 * @param whd The WHD driver, its bus awake.
 * @param when When, for the log.
 * @retval 0 The ID is the one WHD found at start-up.
 * @retval -EIO A read failed, or the ID is wrong.
 *
 * @sideeffects Those of read_chip_regs(): reads registers over the chip's bus; logs them.
 *              errno unchanged.
 */
static int check_chip_answers(whd_driver_t whd, const char *when)
{
	struct chip_regs r;
	unsigned int expected = whd_chip_get_chip_id(whd);

	read_chip_regs(whd, &r);
	LOG_INF("Wi-Fi chip %s: test 0x%08x (0xfeedbead if the bus works), clock 0x%02x "
		"(ALP %s, HT %s), sleep 0x%02x (KSO %s, on %s), ID 0x%08x (want %u), read 0x%08x",
		when, (unsigned int)r.test, (unsigned int)r.clock,
		(r.clock & SBSDIO_ALP_AVAIL) != 0U ? "ready" : "off",
		(r.clock & SBSDIO_HT_AVAIL) != 0U ? "ready" : "off", (unsigned int)r.sleep,
		(r.sleep & SBSDIO_SLPCSR_KEEP_WL_KSO) != 0U ? "set" : "clear",
		(r.sleep & SBSDIO_SLPCSR_WL_DEVON) != 0U ? "yes" : "no", (unsigned int)r.id,
		expected, (unsigned int)r.res);

	return r.res == WHD_SUCCESS && (r.id & CHIP_ID_MASK) == expected ? 0 : -EIO;
}

/**
 * @brief Log the chip's bus registers, and if it answers, print its
 *        firmware's console log and whether it trapped or asserted.
 *
 * whd_wifi_read_wlan_log() prints each new line as "CONSOLE: ..." itself (WPRINT_MACRO);
 * the first call prints all the log the chip holds, later ones what's new.
 *
 * @param whd The WHD driver.
 * @param when When, for the log.
 *
 * @sideeffects Wakes the chip's bus and keeps it awake meanwhile (keep_bus_awake(),
 *              let_bus_sleep()); reads the chip's memory over it; WHD updates its copy of the
 *              chip's shared area and console position, and prints the log; logs the
 *              outcome. errno unchanged.
 */
static void check_chip(whd_driver_t whd, const char *when)
{
	static char line[CONSOLE_LINE];
	const wlan_shared_t *sh = &whd->internal_info.sh;
	whd_result_t res;

	if (keep_bus_awake(whd) == 0 && check_chip_answers(whd, when) == 0) {
		LOG_INF("The Wi-Fi chip's console log:");
		res = whd_wifi_read_wlan_log(whd, line, sizeof(line));
		if (res != WHD_SUCCESS) {
			LOG_ERR("Can't read the chip's console log: 0x%08x", (unsigned int)res);
		} else {
			LOG_INF("Chip firmware: flags 0x%08x, trap at 0x%08x, assert line %u",
				(unsigned int)sh->flags, (unsigned int)sh->trap_addr,
				(unsigned int)sh->assert_line);
		}
	}
	let_bus_sleep(whd);
}

/*
 * Public interface: documented in wifi_debug.h.
 *
 * Wakes the WHD thread whatever the line says: woken with its interrupt flag set, it reads
 * the chip's interrupt status over SPI, so it finds a message even if the line never
 * showed it.
 */
void wifi_debug_scan_failed(void)
{
	bool asserted[IRQ_SAMPLES];
	struct whd_snapshot s;
	whd_driver_t whd = whd_driver_get();
	int ret = sample_line(asserted);

	if (ret < 0) {
		LOG_ERR("Can't read the Wi-Fi interrupt line (GPIO 24): %d", ret);
	} else {
		LOG_INF("Wi-Fi interrupt line (GPIO 24): %s",
			wifi_irq_state_name(wifi_irq_classify(asserted, IRQ_SAMPLES)));
	}
	if (whd == NULL) {
		LOG_ERR("No WHD driver to look at");
		return;
	}
	snapshot(whd, &s);
	log_snapshot("before", &s);

	LOG_WRN("Waking the WHD thread to read from the chip");
	whd_thread_notify_irq(whd);
	k_msleep(WAKE_WAIT_MS);
	snapshot(whd, &s);
	log_snapshot("after", &s);

	check_chip(whd, "after the failure");
}

/*
 * Public interface: documented in wifi_debug.h.
 *
 * Only the scan thread calls it, so @c checked needs no lock.
 */
void wifi_debug_scan_ok(void)
{
	static bool checked;
	whd_driver_t whd;

	if (checked) {
		return;
	}
	checked = true;
	whd = whd_driver_get();
	if (whd == NULL) {
		LOG_ERR("No WHD driver to look at");
		return;
	}
	/* Let the WHD thread finish with the scan first: this reads the chip's bus too. */
	k_msleep(SETTLE_MS);
	check_chip(whd, "while working");
}
