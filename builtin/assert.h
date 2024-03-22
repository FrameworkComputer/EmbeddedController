/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ASSERT_H__
#define __CROS_EC_ASSERT_H__

/* Include CONFIG definitions for EC sources. */
#ifndef THIRD_PARTY
#include "common.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DEBUG_ASSERT
#ifdef CONFIG_DEBUG_ASSERT_REBOOTS

#ifdef CONFIG_DEBUG_ASSERT_BRIEF
__noreturn void panic_assert_fail(const char *fname, int linenum);
#define ASSERT(cond)                                           \
	do {                                                   \
		if (!(cond))                                   \
			panic_assert_fail(__FILE__, __LINE__); \
	} while (0)

#else /* !CONFIG_DEBUG_ASSERT_BRIEF */

__noreturn void panic_assert_fail(const char *msg, const char *func,
				  const char *fname, int linenum);
#define ASSERT(cond)                                                 \
	do {                                                         \
		if (!(cond))                                         \
			panic_assert_fail(#cond, __func__, __FILE__, \
					  __LINE__);                 \
	} while (0)
#endif /* CONFIG_DEBUG_ASSERT_BRIEF */

#else /* !CONFIG_DEBUG_ASSERT_REBOOTS */

#if defined(__arm__)
#define ARCH_SOFTWARE_BREAKPOINT __asm("bkpt")
#elif defined(__nds32__)
#define ARCH_SOFTWARE_BREAKPOINT __asm("break 0")
#elif defined(__riscv)
#define ARCH_SOFTWARE_BREAKPOINT __asm("ebreak")
#elif defined(VIF_BUILD)
/* The genvif utility compiles usb_pd_policy.c and needs an empty definition. */
#define ARCH_SOFTWARE_BREAKPOINT
#else
#error "CONFIG_DEBUG_ASSERT_REBOOTS must be defined on this architecture"
#endif

#define ASSERT(cond)                              \
	do {                                      \
		if (!(cond)) {                    \
			ARCH_SOFTWARE_BREAKPOINT; \
			__builtin_unreachable();  \
		}                                 \
	} while (0)
#endif /* CONFIG_DEBUG_ASSERT_REBOOTS */

#else /* !CONFIG_DEBUG_ASSERT */
#define ASSERT(cond)
#endif /* CONFIG_DEBUG_ASSERT */

/* This collides with cstdlib, so exclude it where cstdlib is supported. */
#ifndef assert
#define assert(x...) ASSERT(x)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ASSERT_H__ */
