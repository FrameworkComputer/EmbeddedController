/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/nds32/config_core.h"

/* Number of IRQ vectors on the IVIC */
#define CONFIG_IRQ_COUNT IT83XX_IRQ_COUNT

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Default PLL frequency. */
#define PLL_CLOCK 48000000

/* Number of I2C ports */
#define I2C_PORT_COUNT 6

/* I2C ports on chip
 * IT83xx - There are three i2c standard ports.
 *          There are three i2c enhanced ports.
 */
#define I2C_STANDARD_PORT_COUNT 3

/****************************************************************************/
/* Memory mapping */

#define CONFIG_RAM_BASE             0x00080000
#define CONFIG_RAM_SIZE             0x0000C000

/* System stack size */
#define CONFIG_STACK_SIZE           1024

/* non-standard task stack sizes */
#define SMALLER_TASK_STACK_SIZE     384
#define IDLE_TASK_STACK_SIZE        512
#define LARGER_TASK_STACK_SIZE      768
#define VENTI_TASK_STACK_SIZE       896

/* Default task stack size */
#define TASK_STACK_SIZE             512

#define CONFIG_PROGRAM_MEMORY_BASE  0x00000000
#define CONFIG_FLASH_BANK_SIZE      0x00000800  /* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE     0x00000400  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE     0x00000004  /* minimum write size */

/*
 * This is the block size of the ILM on the it83xx chip.
 * The ILM for static code cache, CPU fetch instruction from
 * ILM(ILM -> CPU)instead of flash(flash -> IMMU -> CPU) if enabled.
 */
#define IT83XX_ILM_BLOCK_SIZE       0x00001000

/*
 * The AAI program instruction allows continue write flash
 * until write disable instruction.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_ERASE_SIZE

/* This is the physical size of the flash on the chip. We'll reserve one bank
 * in order to emulate per-bank write-protection UNTIL REBOOT. The hardware
 * doesn't support a write-protect pin, and if we make the write-protection
 * permanent, it can't be undone easily enough to support RMA. */
#define CONFIG_FLASH_SIZE  0x00040000

/****************************************************************************/
/* Define our flash layout. */

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"

/****************************************************************************/
/* H2RAM memory mapping */

/*
 * Only it839x series and IT838x DX support mapping LPC I/O cycle 800h ~ 9FFh
 * to 0x8D800h ~ 0x8D9FFh of DLM13.
 */
#define CONFIG_H2RAM_BASE               0x0008D000
#define CONFIG_H2RAM_SIZE               0x00001000
#define CONFIG_H2RAM_HOST_LPC_IO_BASE   0x800

/****************************************************************************/
/* Customize the build */

#define CONFIG_FW_RESET_VECTOR

/* Optional features present on this chip */
#define CHIP_FAMILY_IT83XX
#define CONFIG_ADC
#define CONFIG_HOSTCMD_X86
#define CONFIG_SWITCH

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT

#define GPIO_PIN(port, index) GPIO_##port, (1 << index)
#define GPIO_PIN_MASK(port, mask) GPIO_##port, (mask)

#endif  /* __CROS_EC_CONFIG_CHIP_H */
