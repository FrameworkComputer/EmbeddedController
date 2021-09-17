/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This code is intended for developer use to convert a CrOS EC OS
 * "gpio.inc" file into the expected device-tree format for Zephyr.
 * The only use of it is to do a proof-of-concept build of a CrOS EC
 * device using Zephyr OS, as Zephyr-only devices won't have a
 * equivalent CrOS EC build.
 *
 * It does not handle all cases (i.e., low voltage selection), and
 * some manual modifications to the output may be required.
 *
 **********************************************************************
 * DO NOT CREATE TESTS, SYSTEMS, OR INFRASTRUCTURE WHICH RELIES ON    *
 * THIS CODE.  It's seriously ugly code that's probably prone to      *
 * breakage.  It leaks memory.  In other words, consider it           *
 * "contrib-quality" code that should be deleted one day (once we are *
 * no longer doing proof-of-concept Zephyr builds).                   *
 **********************************************************************
 *
 * To compile:
 *   gcc -Iboard/${BOARD} -std=gnu11 util/gpios_to_zephyr_dts.c \
 *       -o /tmp/print_gpios
 *
 * Then run /tmp/print_gpios and paste the result into your gpio DTS
 * overlay.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 * Implement the macros used in the gpio.inc files, but print DTS
 * format equivalents.
 */
#define GPIO_INT_RISING GPIO_INPUT
#define GPIO_INT_FALLING GPIO_INPUT
#define GPIO_INT_BOTH GPIO_INPUT
#define GPIO_HIB_WAKE_HIGH 0
#define GPIO_HIB_WAKE_LOW 0
#define GPIO_LOCKED 0
#define GPIO_SEL_1P8V 0
#define GPIO_INT(name, pin, opts, int) GPIO(name, pin, opts)
#define GPIO(name, pin, opts) _gpio(#name, pin, opts)
#define UNUSED(pin)
#define UNIMPLEMENTED(name)
#define _gpio(name, pin, opts)                                   \
	printf("%s {\n\tgpios = <&%s %s>;\n\tlabel = %s;\n};\n", \
	       strlower(name), strlower(pin),                    \
	       maybe_parens(strip_zero_ors(#opts)), #name);
#define PIN(A, B) "gpio" #A " " #B
#define ALTERNATE(...)
#define IOEX(...)
#define IOEX_INT(...)

/* Strip out " | 0" and "0 | " from a string */
static char *strip_zero_ors(const char *s)
{
	char *out = malloc(strlen(s) + 2);
	const char *rd_ptr = s;
	char *wr_ptr = out;

	while (*rd_ptr) {
		if (!strncmp(rd_ptr, " | 0", 4) ||
		    !strncmp(rd_ptr, "0 | ", 4)) {
			rd_ptr += 4;
		} else {
			*wr_ptr = *rd_ptr;
			rd_ptr++;
			wr_ptr++;
		}
	}

	*wr_ptr = '\0';
	return out;
}

/*
 * Add parenthesis around the outside of a string if it contains the
 * '|' character.
 */
static char *maybe_parens(const char *s)
{
	size_t out_sz = strlen(s) + 3;
	char *out = malloc(out_sz);

	if (strchr(s, '|'))
		snprintf(out, out_sz, "(%s)", s);
	else
		snprintf(out, out_sz, "%s", s);

	return out;
}

/* Convert a string to lowercase */
static char *strlower(const char *s)
{
	char *out = strdup(s);

	for (int i = 0; out[i]; i++)
		out[i] = tolower(out[i]);

	return out;
}

int main(int argc, char *argv[])
{
#include "gpio.inc"
	return 0;
}
