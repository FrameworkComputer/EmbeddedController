/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

#define HIDE_EC_STDLIB
#include "common.h"
#include "util.h"

/* Int and Long are same size, just forward to existing Long implementation */
int strtoi(const char *nptr, char **endptr, int base)
{
	return strtol(nptr, endptr, base);
}
BUILD_ASSERT(sizeof(int) == sizeof(long));

int strcasecmp(const char *s1, const char *s2)
{
	int diff;

	do {
		diff = tolower(*s1) - tolower(*s2);
		if (diff)
			return diff;
	} while (*(s1++) && *(s2++));
	return 0;
}

int parse_bool(const char *s, int *dest)
{
	/* off, disable, false, no */
	if (!strcasecmp(s, "off") || !strncasecmp(s, "dis", 3) ||
	    tolower(*s) == 'f' || tolower(*s) == 'n') {
		*dest = 0;
		return 1;
	}

	/* on, enable, true, yes */
	if (!strcasecmp(s, "on") || !strncasecmp(s, "ena", 3) ||
	    tolower(*s) == 't' || tolower(*s) == 'y') {
		*dest = 1;
		return 1;
	}

	/* dunno */
	return 0;
}

static int find_base(int base, int *c, const char **nptr)
{
	if ((base == 0 || base == 16) && *c == '0' &&
	    (**nptr == 'x' || **nptr == 'X')) {
		*c = (*nptr)[1];
		(*nptr) += 2;
		base = 16;
	} else if (base == 0) {
		base = *c == '0' ? 8 : 10;
	}
	return base;
}

unsigned long long int strtoull(const char *nptr, char **endptr, int base)
{
	uint64_t result = 0;
	int c = '\0';

	while ((c = *nptr++) && isspace(c))
		;

	if (c == '+') {
		c = *nptr++;
	} else if (c == '-') {
		if (endptr)
			*endptr = (char *)nptr - 1;
		return result;
	}

	base = find_base(base, &c, &nptr);

	while (c) {
		if (c >= '0' && c < '0' + MIN(base, 10))
			result = result * base + (c - '0');
		else if (c >= 'A' && c < 'A' + base - 10)
			result = result * base + (c - 'A' + 10);
		else if (c >= 'a' && c < 'a' + base - 10)
			result = result * base + (c - 'a' + 10);
		else
			break;

		c = *nptr++;
	}

	if (endptr)
		*endptr = (char *)nptr - 1;
	return result;
}
BUILD_ASSERT(sizeof(unsigned long long int) == sizeof(uint64_t));
