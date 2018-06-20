/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handy clever tricks */

#ifndef __CROS_EC_COMPILE_TIME_MACROS_H
#define __CROS_EC_COMPILE_TIME_MACROS_H

/* Test an important condition at compile time, not run time */
#define _BA1_(cond, line) \
	extern int __build_assertion_ ## line[1 - 2*!(cond)] \
	__attribute__ ((unused))
#define _BA0_(c, x) _BA1_(c, x)
#define BUILD_ASSERT(cond) _BA0_(cond, __LINE__)

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

#endif /* __CROS_EC_COMPILE_TIME_MACROS_H */
