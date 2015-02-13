/* Copyright (c) 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Software panic constants. This file must be parsable by the assembler.
 */

#ifndef __CROS_EC_SOFTWARE_PANIC_H
#define __CROS_EC_SOFTWARE_PANIC_H

/* Holds software panic reason PANIC_SW_* */
#define SOFTWARE_PANIC_REASON_REG	r4
#define SOFTWARE_PANIC_INFO_REG		r5

#define PANIC_SW_BASE		0xDEAD6660

/* Software panic reasons */
#define PANIC_SW_DIV_ZERO		(PANIC_SW_BASE + 0)
#define PANIC_SW_STACK_OVERFLOW		(PANIC_SW_BASE + 1)
#define PANIC_SW_ASSERT			(PANIC_SW_BASE + 3)
#define PANIC_SW_WATCHDOG		(PANIC_SW_BASE + 4)

#endif  /* __CROS_EC_SOFTWARE_PANIC_H */
