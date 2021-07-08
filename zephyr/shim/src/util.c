/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

#define HIDE_EC_STDLIB
#include "common.h"
#include "console.h"
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

void hexdump(const uint8_t *data, int len)
{
	/*
	 * NOTE: Could be replaced with LOG_HEXDUMP_INF(data, len, "CBI RAW");
	 * if we enabled CONFIG_LOG=y in future.
	 */
	int i, j;

	if (!data || !len)
		return;

	for (i = 0; i < len; i += 16) {
		/* Left column (Hex) */
		for (j = i; j < i + 16; j++) {
			if (j < len)
				ccprintf(" %02x", data[j]);
			else
				ccprintf("   ");
		}
		/* Right column (ASCII) */
		ccprintf(" |");
		for (j = i; j < i + 16; j++) {
			int c = j < len ? data[j] : ' ';

			ccprintf("%c", isprint(c) ? c : '.');
		}
		ccprintf("|\n");
	}
}

/**
 * Parse offset and size from command line argv[shift] and argv[shift+1]
 *
 * Default values: If argc<=shift, leaves offset unchanged, returning error if
 * *offset<0.  If argc<shift+1, leaves size unchanged, returning error if
 * *size<0.
 */
int parse_offset_size(int argc, char **argv, int shift, int *offset, int *size)
{
	char *e;
	int i;

	if (argc > shift) {
		i = (uint32_t)strtoi(argv[shift], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		*offset = i;
	} else if (*offset < 0)
		return EC_ERROR_PARAM_COUNT;

	if (argc > shift + 1) {
		i = (uint32_t)strtoi(argv[shift + 1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		*size = i;
	} else if (*size < 0)
		return EC_ERROR_PARAM_COUNT;

	return EC_SUCCESS;
}

int uint64divmod(uint64_t *n, int d)
{
	uint64_t q = 0, mask;
	int r = 0;

	/* Divide-by-zero returns zero */
	if (!d) {
		*n = 0;
		return 0;
	}

	/* Common powers of 2 = simple shifts */
	if (d == 2) {
		r = *n & 1;
		*n >>= 1;
		return r;
	} else if (d == 16) {
		r = *n & 0xf;
		*n >>= 4;
		return r;
	}

	/* If v fits in 32-bit, we're done. */
	if (*n <= 0xffffffff) {
		uint32_t v32 = *n;
		r = v32 % d;
		*n = v32 / d;
		return r;
	}

	/* Otherwise do integer division the slow way. */
	for (mask = (1ULL << 63); mask; mask >>= 1) {
		r <<= 1;
		if (*n & mask)
			r |= 1;
		if (r >= d) {
			r -= d;
			q |= mask;
		}
	}
	*n = q;
	return r;
}

int get_next_bit(uint32_t *mask)
{
	int bit = 31 - __builtin_clz(*mask);
	*mask &= ~BIT(bit);
	return bit;
}

char *strzcpy(char *dest, const char *src, int len)
{
	char *d = dest;

	if (len <= 0)
		return dest;
	while (len > 1 && *src) {
		*(d++) = *(src++);
		len--;
	}
	*d = '\0';
	return dest;
}

/* stateful conditional stuff */
enum cond_internal_bits {
	COND_CURR_MASK = BIT(0), /* current value */
	COND_RISE_MASK = BIT(1), /* set if 0->1 */
	COND_FALL_MASK = BIT(2), /* set if 1->0 */
};

void cond_init(cond_t *c, int val)
{
	if (val)
		*c = COND_CURR_MASK;
	else
		*c = 0;
}

void cond_set(cond_t *c, int val)
{
	if (val && cond_is(c, 0))
		*c |= COND_RISE_MASK;
	else if (!val && cond_is(c, 1))
		*c |= COND_FALL_MASK;
	if (val)
		*c |= COND_CURR_MASK;
	else
		*c &= ~COND_CURR_MASK;
}

int cond_went(cond_t *c, int val)
{
	int ret;

	if (val) {
		ret = *c & COND_RISE_MASK;
		*c &= ~COND_RISE_MASK;
	} else {
		ret = *c & COND_FALL_MASK;
		*c &= ~COND_FALL_MASK;
	}

	return ret;
}

int cond_is(cond_t *c, int val)
{
	if (val)
		return *c & COND_CURR_MASK;
	else
		return !(*c & COND_CURR_MASK);
}

int binary_first_base3_from_bits(int *bits, int nbits)
{
	int binary_below = 0;
	int has_z = 0;
	int base3 = 0;
	int i;

	/* Loop through every ternary digit, from MSB to LSB. */
	for (i = nbits - 1; i >= 0; i--) {
		/*
		 * We keep track of the normal ternary result and whether
		 * we found any bit that was a Z. We also determine the
		 * amount of numbers that can be represented with only binary
		 * digits (no Z) whose value in the normal ternary system
		 * is lower than the one we are parsing. Counting from the left,
		 * we add 2^i for any '1' digit to account for the binary
		 * numbers whose values would be below it if all following
		 * digits we parsed would be '0'. As soon as we find a '2' digit
		 * we can total the remaining binary numbers below as 2^(i+1)
		 * because we know that all binary representations counting only
		 * this and following digits must have values below our number
		 * (since 1xxx is always smaller than 2xxx).
		 *
		 * Example: 1 0 2 1 (counting from the left / most significant)
		 * '1' at 3^3: Add 2^3 = 8 to account for binaries 0000-0111
		 * '0' at 3^2: Ignore (not all binaries 1000-1100 are below us)
		 * '2' at 3^1: Add 2^(1+1) = 4 to account for binaries 1000-1011
		 * Stop adding for lower digits (3^0), all already accounted
		 * now. We know that there can be no binary numbers 1020-102X.
		 */
		base3 = (base3 * 3) + bits[i];

		if (!has_z) {
			switch (bits[i]) {
			case 0: /* Ignore '0' digits. */
				break;
			case 1:	/* Account for binaries 0 to 2^i - 1. */
				binary_below += 1 << i;
				break;
			case 2:	/* Account for binaries 0 to 2^(i+1) - 1. */
				binary_below += 1 << (i + 1);
				has_z = 1;
			}
		}
	}

	if (has_z)
		return base3 + (1 << nbits) - binary_below;

	/* binary_below is normal binary system value if !has_z. */
	return binary_below;
}
