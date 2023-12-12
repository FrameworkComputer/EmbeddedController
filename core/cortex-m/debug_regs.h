/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DEBUG_REGS_H
#define __CROS_EC_DEBUG_REGS_H

#include "common.h"

/* For Cortex-M0, see "C1.6.3 Debug Halting Control and Status Register, DHCSR"
 * in the ARMv6-M Architecture Reference Manual.
 *
 * For other Cortex-M, see
 * "C1.6.2 Debug Halting Control and Status Register, DHCSR" in the ARMv7-M
 * Architecture Reference Manual or
 * https://developer.arm.com/documentation/ddi0337/e/core-debug/core-debug-registers/debug-halting-control-and-status-register.
 */
#define CPU_DHCSR REG32(0xE000EDF0)
#define DHCSR_C_DEBUGEN BIT(0)
#define DHCSR_C_HALT BIT(1)
#define DHCSR_C_STEP BIT(2)
#define DHCSR_C_MASKINTS BIT(3)
#ifndef CHIP_CORE_CORTEX_M0
#define DHCSR_C_SNAPSTALL BIT(5) /* Not available on Cortex-M0 */
#endif
#define DHCSR_S_REGRDY BIT(16)
#define DHCSR_S_HALT BIT(17)
#define DHCSR_S_SLEEP BIT(18)
#define DHCSR_S_LOCKUP BIT(19)
#define DHCSR_S_RETIRE_ST BIT(24)
#define DHCSR_S_RESET_ST BIT(25)

#endif /* __CROS_EC_DEBUG_REGS_H */
