/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handy clever tricks */

#ifndef __CROS_EC_COMPILE_TIME_MACROS_H
#define __CROS_EC_COMPILE_TIME_MACROS_H

/* Test an important condition at compile time, not run time */
#define _BA1_(cond, file, line, msg) \
	_Static_assert(cond, file ":" #line ": " msg)
#define _BA0_(c, f, l, msg) _BA1_(c, f, l, msg)
/* Pass in an option message to display after condition */
#define BUILD_ASSERT(cond, ...) _BA0_(cond, __FILE__, __LINE__, __VA_ARGS__)

/*
 * Test an important condition inside code path at run time, taking advantage of
 * -Werror=div-by-zero.
 */
#define BUILD_CHECK_INLINE(value, cond_true) ((value) / (!!(cond_true)))

/* Number of elements in an array */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Make for loops that iterate over pointers to array entries more readable */
#define ARRAY_BEGIN(array) (array)
#define ARRAY_END(array) ((array) + ARRAY_SIZE(array))

/* Just in case - http://gcc.gnu.org/onlinedocs/gcc/Offsetof.html */
#ifndef offsetof
#define offsetof(type, member)  __builtin_offsetof(type, member)
#endif

#define member_size(type, member) sizeof(((type *)0)->member)

/*
 * Bit operation macros.
 */
#define BIT(nr)			(1U << (nr))
#define BIT_ULL(nr)		(1ULL << (nr))

#endif /* __CROS_EC_COMPILE_TIME_MACROS_H */
