/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

/*
 * Flash size of IT81202 is 1MB.
 * We use only half space of flash to save time of erasing RW image from flash.
 */
#define CONFIG_FLASH_SIZE_BYTES         (DT_REG_SIZE(DT_NODELABEL(flash0)) / 2)
/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE      DT_REG_ADDR(DT_NODELABEL(flash0))
/*
 * One page program instruction allows maximum 256 bytes (a page) of data
 * to be programmed.
 */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE   256
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE         DT_PROP(DT_INST(0, soc_nv_flash), \
						write_block_size)
/* Erase bank size */
#define CONFIG_FLASH_ERASE_SIZE         DT_PROP(DT_INST(0, soc_nv_flash), \
						erase_block_size)
/* Protect bank size */
#define CONFIG_FLASH_BANK_SIZE          CONFIG_FLASH_ERASE_SIZE

#define CONFIG_RO_STORAGE_OFF           0x0
#define CONFIG_RW_STORAGE_OFF           0x0

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#define CONFIG_FW_PSTATE_SIZE           CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF            (CONFIG_FLASH_SIZE_BYTES / 2 - \
						CONFIG_FW_PSTATE_SIZE)

#endif /* __CROS_EC_FLASH_CHIP_H */
