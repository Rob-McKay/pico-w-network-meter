/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Paging through a list longer than the screen, a screenful (page) at a
 * time. Pages start at multiples of the page size, so they don't shift as
 * the list grows or shrinks. Hardware independent, so it is unit tested
 * on native_sim.
 */

#ifndef METER_PAGING_H_
#define METER_PAGING_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Move to the next or previous page.
 *
 * @param first First item shown now.
 * @param down Next page if true, previous if false.
 * @param count Items in the list.
 * @param rows Items to a page; 0 leaves @p first as it is.
 * @return First item of the page moved to: no further than the first or
 *         last page.
 *
 * @sideeffects None; errno unchanged.
 */
size_t paging_move(size_t first, bool down, size_t count, size_t rows);

/**
 * @brief Keep a page within a list that may have changed length.
 *
 * @param first First item shown.
 * @param count Items in the list now.
 * @param rows Items to a page; 0 gives 0.
 * @return @p first, rounded down to a page start, and moved back to the
 *         last page if the list has shrunk past it.
 *
 * @sideeffects None; errno unchanged.
 */
size_t paging_clamp(size_t first, size_t count, size_t rows);

#endif /* METER_PAGING_H_ */
