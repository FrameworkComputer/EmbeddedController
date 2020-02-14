/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/minute-ia/config_core.h"

#ifndef __ASSEMBLER__
/* Needed for PANIC_DATA_BASE */
#include "ish_persistent_data.h"
#endif

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

/*****************************************************************************/
/*                               Memory Layout                               */
/*****************************************************************************/

#ifdef CHIP_VARIANT_ISH5P4
#define CONFIG_RAM_BASE		0xFF200000
#else
#define CONFIG_RAM_BASE		0xFF000000
#endif
#define CONFIG_RAM_SIZE		0x000A0000
#ifdef CHIP_VARIANT_ISH5P4
#define CONFIG_RAM_BANK_SIZE	0x00010000
#else
#define CONFIG_RAM_BANK_SIZE	0x00008000
#endif

#if defined(CHIP_FAMILY_ISH3)
/* On ISH3, there is no separate AON memory; use last 4KB of SRAM */
#define CONFIG_AON_RAM_BASE		0xFF09F000
#define CONFIG_AON_RAM_SIZE		0x00001000
#elif defined(CHIP_FAMILY_ISH4)
#define CONFIG_AON_RAM_BASE		0xFF800000
#define CONFIG_AON_RAM_SIZE		0x00001000
#elif defined(CHIP_FAMILY_ISH5)
#define CONFIG_AON_RAM_BASE		0xFF800000
#define CONFIG_AON_RAM_SIZE		0x00002000
#else
#error "CHIP_FAMILY_ISH(3|4|5) must be defined"
#endif

/* The end of the AON memory is reserved for read-only use */
#define CONFIG_AON_PERSISTENT_SIZE	0x180
#define CONFIG_AON_PERSISTENT_BASE	(CONFIG_AON_RAM_BASE	\
					 + CONFIG_AON_RAM_SIZE	\
					 - CONFIG_AON_PERSISTENT_SIZE)

/* Store persistent panic data in AON memory. */
#define CONFIG_PANIC_DATA_BASE		(&(ish_persistent_data.panic_data))

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

/*****************************************************************************/
/*                        Watchdog Timer Configuration                       */
/*****************************************************************************/
#if defined(CHIP_FAMILY_ISH3) || defined(CHIP_FAMILY_ISH5)
#define WDT_CLOCK_HZ		(120000000) /* 120 MHz */
#elif defined(CHIP_FAMILY_ISH4)
#define WDT_CLOCK_HZ		(100000000) /* 100 MHz */
#else
#error "CHIP_FAMILY_ISH(3|4|5) must be defined"
#endif

/* Provide WDT vec number to Minute-IA core implementation */
#undef CONFIG_MIA_WDT_VEC
#define CONFIG_MIA_WDT_VEC	ISH_WDT_VEC

/****************************************************************************/
/* Customize the build */
/* Optional features present on this chip */

/* ISH uses 64-bit hardware timer */
#define CONFIG_HWTIMER_64BIT

/* Macro used with gpio.inc, ISH only has port 0 */
#define GPIO_PIN(index) 0, (1 << (index))
#define GPIO_PIN_MASK(m) .port = 0, .mask = (m)

#ifdef CHIP_VARIANT_ISH5P4
/* Use combined ISR for ipc communication between host and ISH */
#define CONFIG_ISH_HOST2ISH_COMBINED_ISR
/* Use Synopsys Designware uart */
#define CONFIG_ISH_DW_UART
#else
/* Need to clear ISH fabric error */
#define CONFIG_ISH_CLEAR_FABRIC_ERRORS
#endif

#endif  /* __CROS_EC_CONFIG_CHIP_H */
