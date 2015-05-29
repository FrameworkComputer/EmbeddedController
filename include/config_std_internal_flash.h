/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_STD_INTERNAL_FLASH_H
#define __CROS_EC_CONFIG_STD_INTERNAL_FLASH_H

/*
 * Standard memory-mapped flash layout:
 * - RO image starts at the beginning of flash.
 * - PSTATE immediately follows the RO image.
 * - RW image starts at the second half of flash.
 * - WP region consists of the first half of flash (RO + PSTATE).
 */

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#define CONFIG_FW_PSTATE_SIZE	CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF	(CONFIG_FW_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE	((CONFIG_FLASH_PHYSICAL_SIZE - \
				  CONFIG_SHAREDLIB_SIZE) / 2)
#define CONFIG_FLASH_SIZE	CONFIG_FLASH_PHYSICAL_SIZE

/*
 * By default, there is no shared objects library.  However, if configured, the
 * shared objects library will be placed after the RO image.
 */
#define CONFIG_SHAREDLIB_MEM_OFF	(CONFIG_RO_MEM_OFF + \
					 CONFIG_FW_IMAGE_SIZE)
#define CONFIG_SHAREDLIB_STORAGE_OFF	(CONFIG_RO_STORAGE_OFF + \
					 CONFIG_FW_IMAGE_SIZE)
#define CONFIG_SHAREDLIB_SIZE	0

#define CONFIG_RO_MEM_OFF	0
#define CONFIG_RO_STORAGE_OFF	0
#define CONFIG_RO_SIZE		(CONFIG_FW_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_RW_MEM_OFF	(CONFIG_SHAREDLIB_MEM_OFF + \
				 CONFIG_SHAREDLIB_SIZE)
#define CONFIG_RW_STORAGE_OFF	(CONFIG_SHAREDLIB_STORAGE_OFF + \
				 CONFIG_SHAREDLIB_SIZE)
#define CONFIG_RW_SIZE		CONFIG_FW_IMAGE_SIZE

#define CONFIG_WP_OFF		CONFIG_RO_STORAGE_OFF
#define CONFIG_WP_SIZE		CONFIG_FW_IMAGE_SIZE

#endif /* __CROS_EC_CONFIG_STD_INTERNAL_FLASH_H */
