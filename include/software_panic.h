/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Software panic constants. This file must be parsable by the assembler. */

#ifndef __CROS_EC_SOFTWARE_PANIC_H
#define __CROS_EC_SOFTWARE_PANIC_H

/* Holds software panic reason PANIC_SW_* */
#define SOFTWARE_PANIC_REASON_REG r4
#define SOFTWARE_PANIC_INFO_REG r5

#define PANIC_SW_BASE 0xDEAD6660

/* Software panic reasons */
#define PANIC_SW_DIV_ZERO (PANIC_SW_BASE + 0)
#define PANIC_SW_STACK_OVERFLOW (PANIC_SW_BASE + 1)
#define PANIC_SW_PD_CRASH (PANIC_SW_BASE + 2)
#define PANIC_SW_ASSERT (PANIC_SW_BASE + 3)
#define PANIC_SW_WATCHDOG (PANIC_SW_BASE + 4)
#define PANIC_SW_BAD_RNG (PANIC_SW_BASE + 5)
#define PANIC_SW_PMIC_FAULT (PANIC_SW_BASE + 6)
#define PANIC_SW_EXIT (PANIC_SW_BASE + 7)
#define PANIC_SW_WATCHDOG_WARN (PANIC_SW_BASE + 8)

#ifndef __ASSEMBLER__
extern const char *const panic_sw_reasons[];
extern int panic_sw_reason_is_valid(uint32_t vec);
#endif

#endif /* __CROS_EC_SOFTWARE_PANIC_H */
