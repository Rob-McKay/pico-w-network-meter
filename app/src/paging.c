/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "paging.h"

/*
 * Public interface: documented in paging.h.
 */
size_t paging_clamp(size_t first, size_t count, size_t rows)
{
	if (rows == 0U || count == 0U) {
		return 0;
	}

	size_t last = ((count - 1U) / rows) * rows;

	return first > last ? last : (first / rows) * rows;
}

/*
 * Public interface: documented in paging.h.
 *
 * Works on the clamped position, so moving can't overflow: first + rows is at most the last
 * page's start plus one page, within the list's length plus rows.
 */
size_t paging_move(size_t first, bool down, size_t count, size_t rows)
{
	if (rows == 0U) {
		return first;
	}

	first = paging_clamp(first, count, rows);
	if (down) {
		return paging_clamp(first + rows, count, rows);
	}

	return first >= rows ? first - rows : 0U;
}
