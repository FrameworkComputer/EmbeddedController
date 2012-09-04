/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Various utility functions and macros */

#ifndef __CROS_EC_UTIL_H
#define __CROS_EC_UTIL_H

#include "common.h"
#include "config.h"
#include "panic.h"

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
# ifdef CONFIG_ASSERT_HELP
#  define ASSERT(cond) do {			\
		if (!(cond))			\
			panic_assert_fail(#cond, __func__, __FILE__, \
				__LINE__);	\
	} while (0);
# else
#  define ASSERT(cond) do {			\
		if (!(cond))			\
			__asm("bkpt");		\
	} while (0);
# endif
#else
#define ASSERT(cond)
#endif


/* Standard macros / definitions */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#ifndef OFFSET_OF
#define OFFSET_OF(struc, field) ((uint32_t)&(((const struc * const)0)->field))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * macros for integer division with various rounding variants
 * default integer division rounds down.
 */
#define DIV_ROUND_UP(x, y) (((x) + ((y) - 1)) / (y))
#define DIV_ROUND_NEAREST(x, y) (((x) + ((y) / 2)) / (y))

/* Standard library functions */
int atoi(const char *nptr);
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isprint(int c);
int memcmp(const void *s1, const void *s2, int len);
void *memcpy(void *dest, const void *src, int len);
void *memset(void *dest, int c, int len);
void *memmove(void *dest, const void *src, int len);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, int size);
int strlen(const char *s);

/* Like strtol(), but for integers. */
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
