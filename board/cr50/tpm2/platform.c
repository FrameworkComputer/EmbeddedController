/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Platform.h"
#include "TPM_Types.h"

#include "trng.h"
#include "util.h"
#include "version.h"

uint16_t _cpri__GenerateRandom(size_t random_size,
			uint8_t *buffer)
{
	rand_bytes(buffer, random_size);
	return random_size;
}

/*
 * Return the pointer to the character immediately after the first dash
 * encountered in the passed in string, or NULL if there is no dashes in the
 * string.
 */
static const char *char_after_dash(const char *str)
{
	char c;

	do {
		c = *str++;

		if (c == '-')
			return str;
	} while (c);

	return NULL;
}

/*
 * The properly formatted build_info string has the ec code SHA1 after the
 * first dash, and tpm2 code sha1 after the second dash.
 */

void   _plat__GetFwVersion(uint32_t *firmwareV1, uint32_t *firmwareV2)
{
	const char *ver_str = char_after_dash(build_info);

	/* Just in case the build_info string is misformatted. */
	*firmwareV1 = 0;
	*firmwareV2 = 0;

	if (!ver_str)
		return;

	*firmwareV1 = strtoi(ver_str, NULL, 16);

	ver_str = char_after_dash(ver_str);
	if (!ver_str)
		return;

	*firmwareV2 = strtoi(ver_str, NULL, 16);
}
