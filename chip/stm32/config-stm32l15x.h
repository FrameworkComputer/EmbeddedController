/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_PHYSICAL_SIZE 0x00020000
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0100  /* erase bank size */

/* crosbug.comb/p/9811 workaround 64-byte payload limitation */
#define CONFIG_64B_WORKAROUND

#ifdef CONFIG_64B_WORKAROUND
#define CONFIG_FLASH_WRITE_SIZE      0x0040  /* claimed minimum write size */
#define CONFIG_FLASH_REAL_WRITE_SIZE 0x0080  /* actual minimum write size */
#else
#define CONFIG_FLASH_WRITE_SIZE 0x0080
#endif

#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00004000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (64 * 1024)

#define CONFIG_FW_RO_OFF         0
#define CONFIG_FW_RO_SIZE        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_OFF         CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE        CONFIG_FW_IMAGE_SIZE

#define CONFIG_SECTION_RO_OFF    CONFIG_FW_RO_OFF
#define CONFIG_SECTION_RO_SIZE   CONFIG_FW_RO_SIZE
#define CONFIG_SECTION_RW_OFF    CONFIG_FW_RW_OFF
#define CONFIG_SECTION_RW_SIZE   CONFIG_FW_RW_SIZE
#define CONFIG_SECTION_WP_RO_OFF   CONFIG_FW_RO_OFF
#define CONFIG_SECTION_WP_RO_SIZE  CONFIG_FW_RO_SIZE

/*
 * The EC uses the top bank of flash to emulate a SPI-like write protect
 * register with persistent state.  Put that up at the top.
 */
#define CONFIG_SECTION_FLASH_PSTATE_SIZE  (1 * CONFIG_FLASH_BANK_SIZE)
#define CONFIG_SECTION_FLASH_PSTATE_OFF   (CONFIG_FLASH_SIZE \
					   - CONFIG_SECTION_FLASH_PSTATE_SIZE)

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 45
