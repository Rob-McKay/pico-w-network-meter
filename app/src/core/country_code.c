/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>

#include "country_code.h"

/**
 * @brief Tell whether a character is an upper-case ASCII letter.
 *
 * Compared as ASCII, not with isupper(), which depends on the locale.
 *
 * @param c The character.
 * @return True for 'A' to 'Z'.
 *
 * @sideeffects None; errno unchanged.
 */
static bool upper_letter(char c)
{
	return c >= 'A' && c <= 'Z';
}


/*
 * Public interface: documented in country_code.h.
 */
bool country_code_valid(const char *code)
{
	if (code == NULL || code[0] == '\0' || code[1] == '\0' || code[2] != '\0') {
		return false;
	}
	if (code[0] == '0' && code[1] == '0') {
		return true;
	}

	return upper_letter(code[0]) && upper_letter(code[1]);
}
