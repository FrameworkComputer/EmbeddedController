/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H
/*
 * One page program instruction allows maximum 256 bytes (a page) of data
 * to be programmed.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)
/* Erase bank size */
#define CONFIG_FLASH_ERASE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), erase_block_size)
/* Protect bank size */
#define CONFIG_FLASH_BANK_SIZE CONFIG_FLASH_ERASE_SIZE

#define CONFIG_RO_STORAGE_OFF 0x0
#define CONFIG_RW_STORAGE_OFF 0x0

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#ifdef CONFIG_PLATFORM_EC_FLASH_PSTATE_BANK
#define CONFIG_FW_PSTATE_SIZE CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF (CONFIG_RO_STORAGE_OFF + CONFIG_RO_SIZE)
#else
#define CONFIG_FW_PSTATE_SIZE 0
#endif /* CONFIG_PLATFORM_EC_FLASH_PSTATE_BANK */

#endif /* __CROS_EC_FLASH_CHIP_H */
