/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debugging Wi-Fi failures, with CONFIG_METER_DEBUG_WIFI_IRQ: a hook the
 * scan task calls when a scan fails. The Pico W's is in
 * src/target/pico/wifi_debug.c; it reads the Wi-Fi library's internals, so
 * it is for debug builds of a pinned Zephyr only.
 */

#ifndef METER_WIFI_DEBUG_H_
#define METER_WIFI_DEBUG_H_

/**
 * @brief Log the Wi-Fi driver's state after a failed scan, and prod it to
 *        read anything the chip has for it.
 *
 * Then prints the chip firmware's own console log, which says whether it
 * trapped or asserted.
 *
 * Built only with CONFIG_METER_DEBUG_WIFI_IRQ; call it under
 * IS_ENABLED(CONFIG_METER_DEBUG_WIFI_IRQ).
 *
 * @sideeffects Blocks the calling thread for about 110 ms and while the chip's log is read.
 *              Reads the chip's interrupt line (GPIO 24) and, with the scheduler locked,
 *              the WHD library's state; wakes the WHD thread to read from the chip; logs
 *              what it found before and after. Then wakes the chip's bus, and if the chip
 *              answers, reads its firmware's console log from its memory and prints it.
 *              errno unchanged.
 */
void wifi_debug_scan_failed(void);

/**
 * @brief After the first scan that works, run the same checks on the Wi-Fi
 *        chip as wifi_debug_scan_failed() ends with, to compare against.
 *
 * Built only with CONFIG_METER_DEBUG_WIFI_IRQ; call it under
 * IS_ENABLED(CONFIG_METER_DEBUG_WIFI_IRQ), from the scan thread.
 *
 * With CONFIG_METER_DEBUG_WIFI_NO_SR, it then stops the chip's bus sleeping
 * in its save/restore deep sleep, trying again after each scan until it can.
 *
 * @sideeffects The first time only: records that it ran; sleeps 500 ms, so the WHD thread
 *              has finished with the scan; wakes the chip's bus, reads its
 *              registers and logs them, and if the chip answers, reads its firmware's
 *              console log from its memory and prints it all, blocking the calling thread
 *              meanwhile. With CONFIG_METER_DEBUG_WIFI_NO_SR, until it has done so: sleeps
 *              500 ms, wakes the chip's bus and, with the scheduler locked, turns WHD's
 *              save/restore sleep off; logs it. Otherwise later calls do nothing. errno
 *              unchanged.
 */
void wifi_debug_scan_ok(void);

#endif /* METER_WIFI_DEBUG_H_ */
