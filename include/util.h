/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Various utility functions and macros */

#ifndef __CROS_EC_UTIL_H
#define __CROS_EC_UTIL_H

#include "common.h"
#include "config.h"

/**
 * Trigger a compilation failure if the condition
 * is not verified at build time.
 */
#define BUILD_ASSERT(cond) ((void)sizeof(char[1 - 2*!(cond)]))

/**
 * Trigger a debug exception if the condition
 * is not verified at runtime.
 */
#ifdef CONFIG_DEBUG
#define ASSERT(cond) do {			\
		if (!(cond))			\
			__asm("bkpt");		\
	} while (0);
#else
#define ASSERT(cond)
#endif


/* Standard macros / definitions */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NULL ((void *)0)

/**
 * macros for integer division with various rounding variants
 * default integer division rounds down.
 */
#define DIV_ROUND_UP(x, y) (((x) + ((y) - 1)) / (y))
#define DIV_ROUND_NEAREST(x, y) (((x) + ((y) / 2)) / (y))

static inline uint8_t hex2asc(uint8_t hex)
{
	return hex + ((hex > 9) ? 'A' : '0');
}

/* Standard library functions */
int atoi(const char *nptr);
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
void *memcpy(void *dest, const void *src, int len);
void *memset(void *dest, int c, int len);
void *memmove(void *dest, const void *src, int len);
int strcasecmp(const char *s1, const char *s2);
int strlen(const char *s);
int strtoi(const char *nptr, char **endptr, int base);

/* Like strncpy(), but guarantees null termination. */
char *strzcpy(char *dest, const char *src, int len);

int tolower(int c);

/* 64-bit divide-and-modulo.  Does the equivalent of:
 *
 *   r = *n % d;
 *   *n /= d;
 *   return r;
 */
int uint64divmod(uint64_t *v, int by);

#endif  /* __CROS_EC_UTIL_H */
