/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CHIP_CONFIG_H
#define __CROS_EC_CHIP_CONFIG_H

/* 16.000 Mhz internal oscillator frequency (PIOSC) */
#define INTERNAL_CLOCK 16000000

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 132

/* Use a bigger console output buffer */
#define CONFIG_UART_TX_BUF_SIZE 1024

/****************************************************************************/
/* Memory mapping */

#define CONFIG_RAM_BASE             0x20000000
#define CONFIG_RAM_SIZE             0x00008000

/* System stack size */
#define CONFIG_STACK_SIZE           4096

#define CONFIG_FLASH_BASE           0x00000000
#define CONFIG_FLASH_BANK_SIZE      0x00000800  /* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE     0x00000400  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE     0x00000004  /* minimum write size */

/* This is the physical size of the flash on the chip. We'll reserve one bank
 * in order to emulate per-bank write-protection UNTIL REBOOT. The hardware
 * doesn't support a write-protect pin, and if we make the write-protection
 * permanent, it can't be undone easily enough to support RMA. */
#define CONFIG_FLASH_PHYSICAL_SIZE  0x00040000

/* This is the size that we pretend we have. This is what flashrom expects,
 * what the FMAP reports, and what size we build images for. */
#define CONFIG_FLASH_SIZE (CONFIG_FLASH_PHYSICAL_SIZE - CONFIG_FLASH_BANK_SIZE)

/****************************************************************************/
/* Define our flash layout. */

/*
 * The EC uses the top bank of flash to emulate a SPI-like write protect
 * register with persistent state.  Put that up at the top.
 */
#define CONFIG_SECTION_FLASH_PSTATE_SIZE  (1 * CONFIG_FLASH_BANK_SIZE)
#define CONFIG_SECTION_FLASH_PSTATE_OFF   (CONFIG_FLASH_PHYSICAL_SIZE \
					   - CONFIG_SECTION_FLASH_PSTATE_SIZE)

/* Then there are the two major sections. */
/* TODO: Increase to 128KB, or shrink to 64KB? */
#define CONFIG_SECTION_RO_SIZE	    (40 * CONFIG_FLASH_BANK_SIZE)
#define CONFIG_SECTION_RO_OFF       CONFIG_FLASH_BASE

#define CONFIG_SECTION_RW_SIZE       (40 * CONFIG_FLASH_BANK_SIZE)
#define CONFIG_SECTION_RW_OFF        (CONFIG_SECTION_RO_OFF \
					+ CONFIG_SECTION_RO_SIZE)

#define CONFIG_FW_RO_OFF            CONFIG_SECTION_RO_OFF
#define CONFIG_FW_RO_SIZE           CONFIG_SECTION_RO_SIZE
#define CONFIG_FW_RW_OFF            CONFIG_SECTION_RW_OFF
#define CONFIG_FW_RW_SIZE           CONFIG_SECTION_RW_SIZE

/****************************************************************************/
/* Customize the build */

/* Build with assertions and debug messages */
#define CONFIG_DEBUG

/* Optional features present on this chip */
#define CONFIG_ADC
#define CONFIG_EEPROM
#define CONFIG_FLASH
#define CONFIG_FPU
#define CONFIG_I2C

/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#endif  /* __CROS_EC_CHIP_CONFIG_H */
