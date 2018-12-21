/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/minute-ia/config_core.h"

/* Number of IRQ vectors on the ISH */
#define CONFIG_IRQ_COUNT	(VEC_TO_IRQ(255) + 1)

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE	2048

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS	250
#define HOOK_TICK_INTERVAL	(HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT	8

/* this macro causes 'pause' and reduces loop counts inside loop. */
#define CPU_RELAX() asm volatile("rep; nop" ::: "memory")

/****************************************************************************/
/* Memory mapping */
/****************************************************************************/

/* Define our SRAM layout. */
#define CONFIG_ISH_SRAM_BASE_START	0xFF000000
#define CONFIG_ISH_SRAM_BASE_END	0xFF0A0000
#define CONFIG_ISH_SRAM_SIZE		(CONFIG_ISH_SRAM_BASE_END - \
					CONFIG_ISH_SRAM_BASE_START)

/* Required for panic_output */
#define CONFIG_RAM_SIZE			CONFIG_ISH_SRAM_SIZE
#define CONFIG_RAM_BASE			CONFIG_ISH_SRAM_BASE_START

/* System stack size */
#define CONFIG_STACK_SIZE		1024

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE		640
#define LARGER_TASK_STACK_SIZE		1024
#define HUGE_TASK_STACK_SIZE		2048
/* Default task stack size */
#define TASK_STACK_SIZE			640

/****************************************************************************/
/* Define our flash layout. */
/* Note: The 4 macros below are unnecesasry for the ISH chip. However they are
 * referenced in common files and hence retained to avoid build errors.
 */

/* Protect bank size 4K bytes */
#define CONFIG_FLASH_BANK_SIZE		0x00001000
/* Sector erase size 4K bytes */
#define CONFIG_FLASH_ERASE_SIZE		0x00000000
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE		0x00000000
/* Program memory base address */
#define CONFIG_PROGRAM_MEMORY_BASE	0x00100000

#include "config_flash_layout.h"

/****************************************************************************/
/* Customize the build */
/* Optional features present on this chip */

/* Note: ISH does not use the LPC bus but the protocol. */
/* #define CONFIG_HOSTCMD_LPC */

/* Macro used with gpio.inc, ISH only has port 0 */
#define GPIO_PIN(index) 0, (1 << (index))
#define GPIO_PIN_MASK(m) .port = 0, .mask = (m)

#endif  /* __CROS_EC_CONFIG_CHIP_H */
