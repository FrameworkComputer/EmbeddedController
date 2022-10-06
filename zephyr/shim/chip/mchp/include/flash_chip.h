/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

/* TODO(b/226599277): need to check if SST25PF040 is compatible W25X40
 * MEC1727 uses SST25PF040.
 * Similar to W25X40, both only have one status reg
 */
#define CONFIG_SPI_FLASH_W25X40 /* Internal SPI flash type. */
#define CONFIG_FLASH_WRITE_SIZE 0x1 /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256 /* one page size for write */
#define CONFIG_FLASH_ERASE_SIZE 0x1000
#define CONFIG_FLASH_BANK_SIZE CONFIG_FLASH_ERASE_SIZE

/* RO image resides at 4KB offset in protected region
 * The first 4KB in the protected region starting at offset 0 contains
 * the Boot-ROM TAGs and Boot-ROM Header for EC_RO. These objects are
 * not loaded into RAM.
 * RW image is never loaded by the Boot-ROM therefore no TAG or Header
 * is needed. RW starts at offset 0 in RW storage region.
 */
#define CONFIG_RO_STORAGE_OFF 0x1000
#define CONFIG_RW_STORAGE_OFF 0

#endif /* __CROS_EC_FLASH_CHIP_H */
