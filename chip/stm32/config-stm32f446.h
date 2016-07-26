/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
/* Flash physical size is:		(512 * 1024)
 * We are limiting the ec.bin size to 128k to reduce flashing time.
 */
#define CONFIG_FLASH_SIZE		(128 * 1024)
#define CONFIG_FLASH_BANK_SIZE		(16 * 1024)

/*
 * 8 "erase" sectors : 16KB/16KB/16KB/16KB/64KB/128KB/128KB/128KB
 * We won't use CONFIG_FLASH_ERASE_SIZE, it will be programmatically
 * set in flash-stm32f4.c. However it must be set or the common flash
 * code won't build. So we'll set it here.
 */
#define CONFIG_FLASH_ERASE_SIZE	(16 * 1024)

/* minimum write size for 3.3V. 1 for 1.8V */
#define FLASH_WRITE_SIZE_1800	0x0001
#define FLASH_WS_DIV_1800	16000000
#define FLASH_WRITE_SIZE_3300	0x0004
#define FLASH_WS_DIV_3300	30000000
#define FLASH_WRITE_SIZE	0x0004

#define CONFIG_FLASH_WRITE_SIZE	FLASH_WRITE_SIZE

/* No page mode on STM32F, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE FLASH_WRITE_SIZE

#define CONFIG_RAM_BASE		0x20000000
#define CONFIG_RAM_SIZE		0x00020000

#define CONFIG_RO_MEM_OFF	0
#define CONFIG_RO_SIZE		(48 * 1024)
#define CONFIG_RW_MEM_OFF	(64 * 1024)
#define CONFIG_RW_SIZE		(64 * 1024)

#define CONFIG_RO_STORAGE_OFF	0
#define CONFIG_RW_STORAGE_OFF	0

#define CONFIG_EC_PROTECTED_STORAGE_OFF		0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE	CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_OFF		CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE					\
		 (CONFIG_FLASH_SIZE - CONFIG_EC_WRITABLE_STORAGE_OFF)

#define CONFIG_WP_STORAGE_OFF		CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE		CONFIG_EC_PROTECTED_STORAGE_SIZE

/* PSTATE lives in one of the smaller blocks. */
#define CONFIG_FLASH_PSTATE
#define CONFIG_FW_PSTATE_SIZE	(16 * 1024)
#define CONFIG_FW_PSTATE_OFF	(CONFIG_RO_SIZE)

#undef I2C_PORT_COUNT
#define I2C_PORT_COUNT	4


/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT	97
