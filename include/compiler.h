/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_COMPILER_H
#define __CROS_EC_COMPILER_H

/*
 * See https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
 */
#define GCC_VERSION \
	(__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

/*
 * The EC codebase assumes that typeof() is available but it is not in Zephyr.
 * We use an #ifdef since arch/arm/include/aarch32/cortex_m/cmse.h defines this
 * macro.
 */
#ifndef typeof
#define typeof(x)	__typeof__(x)
#endif

#endif /* __CROS_EC_COMPILER_H */
