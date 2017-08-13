/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE       (2048 * 1024)

/* 3 regions type: 32K, 128K and 256K */
#define SIZE_32KB (32 * 1024)
#define SIZE_128KB (128 * 1024)
#define SIZE_256KB (256 * 1024)
#define CONFIG_FLASH_REGION_TYPE_COUNT 3
#define CONFIG_FLASH_MULTIPLE_REGION \
	(5 + (CONFIG_FLASH_SIZE - SIZE_256KB) / SIZE_256KB)

/* Erasing 256K can take up to 2s, need to defer erase. */
#define CONFIG_FLASH_DEFERRED_ERASE

/* minimum write size for 3.3V. 1 for 1.8V */
#define STM32_FLASH_WRITE_SIZE_1800 1
#define STM32_FLASH_WS_DIV_1800 16000000
#define STM32_FLASH_WRITE_SIZE_3300 4
#define STM32_FLASH_WS_DIV_3300 30000000

/* No page mode on STM32F, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_WRITE_SIZE

/* DTCM-RAM: 128kB 0x20000000 - 0x2001FFFF*/
/* SRAM1:    368kB 0x20020000 - 0x2007BFFF */
/* SRAM2:     16kB 0x2007C000 - 0x2007FFFF */
#define CONFIG_RAM_BASE		0x20000000
#define CONFIG_RAM_SIZE		0x00080000

#define CONFIG_RO_MEM_OFF	0
#define CONFIG_RO_SIZE		(1024 * 1024)
#define CONFIG_RW_MEM_OFF	(1024 * 1024)
#define CONFIG_RW_SIZE		(1024 * 1024)

#define CONFIG_RO_STORAGE_OFF	0
#define CONFIG_RW_STORAGE_OFF	0

#define CONFIG_EC_PROTECTED_STORAGE_OFF		0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE	CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_OFF		CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE					\
		 (CONFIG_FLASH_SIZE - CONFIG_EC_WRITABLE_STORAGE_OFF)

#define CONFIG_WP_STORAGE_OFF		CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE		CONFIG_EC_PROTECTED_STORAGE_SIZE

#undef I2C_PORT_COUNT
#define I2C_PORT_COUNT	4

/* Use PSTATE embedded in the RO image, not in its own erase block */
#define CONFIG_FLASH_PSTATE
#undef CONFIG_FLASH_PSTATE_BANK

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT	109
