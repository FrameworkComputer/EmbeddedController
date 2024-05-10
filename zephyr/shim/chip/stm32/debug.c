/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include <stm32_ll_system.h>

#if defined(CONFIG_SOC_SERIES_STM32F4X) || defined(CONFIG_SOC_SERIES_STM32G4X)
#define STM32_DBGMCU_CR_LOW_PWR_FRIENDLY \
	(DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY)
#elif CONFIG_SOC_STM32H743XX
/* DBGSTBY_D3 and DBGSTOP_D3 bits have been removed in RM0433 Rev 7. */
#define STM32_DBGMCU_CR_LOW_PWR_FRIENDLY \
	(DBGMCU_CR_DBG_SLEEPD1 | DBGMCU_CR_DBG_STOPD1 | DBGMCU_CR_DBG_STANDBYD1)
#endif

#ifdef STM32_DBGMCU_CR_LOW_PWR_FRIENDLY
/*
 * This function looks for signs that a debugger was attached. If we
 * see that a debugger was attached, we know that the chip's security features
 * may function as if the debugger is still attached.
 *
 * This is important because STM32 chips will emit a bus error and hang
 * upon enabling read protection level 1 (RDP1/software-write-protect), if it
 * detects a debugger. More specifically, if any flash access is performed,
 * say an instruction read, while RDP1 is enabled and in the presence of a
 * debugger, the MCU will trigger a bus error.
 *
 * From RM0402 Rev 5 Section 3.6.3 about read protection level 1:
 * "No access (read, erase, program) to Flash memory can be performed while the
 * debug feature is connected or while booting from RAM or system memory
 * bootloader. A bus error is generated in case of read request."
 */
__override bool debugger_was_connected(void)
{
	/*
	 * The bits we are looking for are the MCU debug control register bits
	 * responsible for permitting the clocks to continue running when the
	 * MCU goes into Sleep, Stop, or Standby. This allows the debugger to
	 * still communicate and control the MCU while in low-power modes.
	 * These bits seem to always be set by debugging software
	 * (JLink and OpenOCD) and not cleaned up upon disconnected.
	 *
	 * These bits and the chip debugger status are not cleared on reset.
	 * Only power-on-reset / power-cycle.
	 */
	return READ_BIT(DBGMCU->CR, STM32_DBGMCU_CR_LOW_PWR_FRIENDLY);
}
#endif /* STM32_DBGMCU_CR_LOW_PWR_FRIENDLY */
