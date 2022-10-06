/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handy clever tricks */

#ifndef __CROS_EC_COMPILE_TIME_MACROS_H
#define __CROS_EC_COMPILE_TIME_MACROS_H

#if defined(__cplusplus) && !defined(CONFIG_ZEPHYR)
#include <type_traits>
#endif

/* sys/util.h in zephyr provides equivalents to most of these macros */
#ifdef CONFIG_ZEPHYR
#include <zephyr/sys/util.h>
#endif

#ifdef __cplusplus
#define _STATIC_ASSERT static_assert
#else
#define _STATIC_ASSERT _Static_assert
#endif

/* Test an important condition at compile time, not run time */
#define _BA1_(cond, file, line, msg) \
	_STATIC_ASSERT(cond, file ":" #line ": " msg)
#define _BA0_(c, f, l, msg) _BA1_(c, f, l, msg)
/* Pass in an option message to display after condition */

#ifndef CONFIG_ZEPHYR
#define BUILD_ASSERT(cond, ...) _BA0_(cond, __FILE__, __LINE__, __VA_ARGS__)
#endif

/*
 * Test an important condition inside code path at run time, taking advantage of
 * -Werror=div-by-zero.
 */
#define BUILD_CHECK_INLINE(value, cond_true) ((value) / (!!(cond_true)))

/* Check that the value is an array (not a pointer) */
#ifdef __cplusplus
#define _IS_ARRAY(arr) (std::is_array<decltype(arr)>::value)
#else
#define _IS_ARRAY(arr) \
	!__builtin_types_compatible_p(typeof(arr), typeof(&(arr)[0]))
#endif

/**
 * ARRAY_SIZE - Number of elements in an array.
 *
 * This version is type-safe and will not allow pointers, causing a
 * compile-time divide by zero error if a pointer is passed.
 */
#ifndef CONFIG_ZEPHYR
#define ARRAY_SIZE(arr) \
	BUILD_CHECK_INLINE(sizeof(arr) / sizeof((arr)[0]), _IS_ARRAY(arr))
#endif

/* Make for loops that iterate over pointers to array entries more readable */
#define ARRAY_BEGIN(array)                                                   \
	({                                                                   \
		BUILD_ASSERT(_IS_ARRAY(array),                               \
			     "ARRAY_BEGIN is only compatible with arrays."); \
		(array);                                                     \
	})
#define ARRAY_END(array) ((array) + ARRAY_SIZE(array))

/* Just in case - http://gcc.gnu.org/onlinedocs/gcc/Offsetof.html */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define member_size(type, member) sizeof(((type *)0)->member)

/*
 * Bit operation macros.
 */
#ifndef CONFIG_ZEPHYR
#define BIT(nr) (1U << (nr))
/*
 * Set or clear <bit> of <var> depending on <set>.
 * It also supports setting and clearing (e.g. SET_BIT, CLR_BIT) macros.
 */
#define WRITE_BIT(var, bit, set) \
	((var) = (set) ? ((var) | BIT(bit)) : ((var) & ~BIT(bit)))
#endif
#define BIT_ULL(nr) (1ULL << (nr))

/*
 * Create a bit mask from least significant bit |l|
 * to bit |h|, inclusive.
 *
 * Examples:
 * GENMASK(31, 0) ==> 0xFF_FF_FF_FF
 * GENMASK(3, 0)  ==> 0x00_00_00_0F
 * GENMASK(7, 4)  ==> 0x00_00_00_F0
 * GENMASK(b, b)  ==> BIT(b)
 *
 * Note that we shift after using BIT() to avoid compiler
 * warnings for BIT(31+1).
 */
#ifndef CONFIG_ZEPHYR
#define GENMASK(h, l) (((BIT(h) << 1) - 1) ^ (BIT(l) - 1))
#define GENMASK_ULL(h, l) (((BIT_ULL(h) << 1) - 1) ^ (BIT_ULL(l) - 1))
#endif

#endif /* __CROS_EC_COMPILE_TIME_MACROS_H */
