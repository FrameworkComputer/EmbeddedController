/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Helper macros for shared objects library.
 */
#ifndef __CROS_EC_LIBSHAREDOBJS_H
#define __CROS_EC_LIBSHAREDOBJS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_SHAREDLIB
/*
 * The shared library currently only works with those platforms in which both
 * the RO and RW images are loaded simultaneously in some executable memory.
 *
 * NOTE: I know that this doesn't cover all possible cases, but it will catch
 *       an obvious case.
 */
#if (CONFIG_RO_MEM_OFF == CONFIG_RW_MEM_OFF)
#error "The shared library is NOT compatible with this EC."
#endif

/*
 * All of the objects in the shared library will be placed into the '.roshared'
 * section.  The SHAREDLIB() macro simply adds this attribute and prevents the
 * RW image from compiling them in.
 */
#undef SHAREDLIB
#ifdef SHAREDLIB_IMAGE
#define SHAREDLIB(...) __attribute__((section(".roshared"))) __VA_ARGS__
#else /* !defined(SHAREDLIB_IMAGE) */
#define SHAREDLIB(...)
#endif /* defined(SHAREDLIB_IMAGE) */
#define SHAREDLIB_FUNC(...) \
	extern __VA_ARGS__ __attribute__((section(".roshared.text")))

#else /* !defined(CONFIG_SHAREDLIB) */

/* By default, the SHAREDLIB() macro maps to its contents. */
#define SHAREDLIB(...) __VA_ARGS__
#define SHAREDLIB_FUNC(...) __VA_ARGS__
#endif /* defined(CONFIG_SHAREDLIB) */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_LIBSHAREDOBJS_H */
