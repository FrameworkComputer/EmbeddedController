/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

#if defined(CHIP_FAMILY_IT8320)    /* N8 core */
#include "config_chip_it8320.h"
#elif defined(CHIP_FAMILY_IT8XXX2) /* RISCV core */
#include "config_chip_it8xxx2.h"
#else
#error "Unsupported chip family!"
#endif

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
#define I2C_ENHANCED_PORT_COUNT 3

/* System stack size */
#define CONFIG_STACK_SIZE           1024

/* non-standard task stack sizes */
#define SMALLER_TASK_STACK_SIZE     (384 + CHIP_EXTRA_STACK_SPACE)
#define IDLE_TASK_STACK_SIZE        (512 + CHIP_EXTRA_STACK_SPACE)
#define LARGER_TASK_STACK_SIZE      (768 + CHIP_EXTRA_STACK_SPACE)
#define VENTI_TASK_STACK_SIZE       (896 + CHIP_EXTRA_STACK_SPACE)
#define ULTRA_TASK_STACK_SIZE       (1056 + CHIP_EXTRA_STACK_SPACE)
#define TRENTA_TASK_STACK_SIZE      (1184 + CHIP_EXTRA_STACK_SPACE)

/* Default task stack size */
#define TASK_STACK_SIZE             (512 + CHIP_EXTRA_STACK_SPACE)

#ifdef IT83XX_CHIP_FLASH_IS_KGD
#define CONFIG_FLASH_BANK_SIZE      0x00001000  /* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE     0x00001000  /* erase bank size */
#else
#define CONFIG_FLASH_BANK_SIZE      0x00000800  /* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE     0x00000400  /* erase bank size */
#endif
#define CONFIG_FLASH_WRITE_SIZE     0x00000004  /* minimum write size */

/*
 * This is the block size of the ILM on the it83xx chip.
 * The ILM for static code cache, CPU fetch instruction from
 * ILM(ILM -> CPU)instead of flash(flash -> IMMU -> CPU) if enabled.
 */
#define IT83XX_ILM_BLOCK_SIZE       0x00001000

#ifdef IT83XX_CHIP_FLASH_IS_KGD
/*
 * One page program instruction allows maximum 256 bytes (a page) of data
 * to be programmed.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256
#else
/*
 * The AAI program instruction allows continue write flash
 * until write disable instruction.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_ERASE_SIZE
#endif

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
 *
 * IT8xxx2 series support mapping LPC/eSPI I/O cycle 800h ~ 9FFh
 * to 0x80081800 ~ 0x800819FF of DLM1.
 */
#define CONFIG_H2RAM_BASE               (CHIP_H2RAM_BASE)
#define CONFIG_H2RAM_SIZE               0x00001000
#define CONFIG_H2RAM_HOST_LPC_IO_BASE   0x800

/****************************************************************************/
/* Customize the build */

#define CONFIG_FW_RESET_VECTOR

/* Optional features present on this chip */
#define CHIP_FAMILY_IT83XX
#define CONFIG_ADC
#define CONFIG_SWITCH

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT

#define GPIO_PIN(port, index) GPIO_##port, BIT(index)
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)

#endif  /* __CROS_EC_CONFIG_CHIP_H */
