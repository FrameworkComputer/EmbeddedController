/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

/* The flash chip can differ per project. Use P25Q16 as a default option. */
#define CONFIG_SPI_FLASH_P25Q16

#define CONFIG_FLASH_WRITE_SIZE 0x1 /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256 /* one page size for write */

/* Offsets in program memory and storage are the same. */
#define CONFIG_RO_STORAGE_OFF CONFIG_CROS_EC_RO_MEM_OFF

/* RW image offset inside writable storage (RW part) */
#define CONFIG_RW_STORAGE_OFF 0x0

/* Use the smallest possible erase unit. */
#define CONFIG_FLASH_ERASE_SIZE 4096

/*
 * There is no clear answer to this, because flash protection is set according
 * to the protection table in the flash chip documentation. The per block
 * protection is not enabled, because some chips don't support that.
 * Set the value as the smallest possible protection range which is 1/256 of the
 * 1MB flash or 1/512 of 2MB.
 */
#define CONFIG_FLASH_BANK_SIZE 4096

#endif /* __CROS_EC_FLASH_CHIP_H */
