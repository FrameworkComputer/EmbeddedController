/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

/* The flash chip can differ per project. Use P25Q16 as a default option. */
#define CONFIG_SPI_FLASH_P25Q16

/* One page size for ideal write */
#define CONFIG_FLASH_WRITE_SIZE 1

/* No page mode, so use minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256

/* RO image offset inside protected storage (RO part) - make 4kB offset for
 * configuration page.
 */
#define CONFIG_RO_STORAGE_OFF 0x1000

/* RW image offset inside writable storage (RW part) */
#define CONFIG_RW_STORAGE_OFF 0x0

/* Use the smallest possible erase unit. */
#define CONFIG_FLASH_ERASE_SIZE 4096

/*
 * There is no clear answer to this, because different flash chips can be
 * connected to Focaltech SOCs and the flash protection is set according
 * to the protection table in the flash chip documentation. The per block
 * protection is not enabled, because some flash chips don't support that.
 * Set the value as the smallest possible protection range which is 1/256 of the
 * 1MB flash or 1/512 of 2MB.
 */
#define CONFIG_FLASH_BANK_SIZE 4096

#endif /* __CROS_EC_FLASH_CHIP_H */
