/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

#define CONFIG_FLASH_WRITE_SIZE		0x1  /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE	256   /* one page size for write */
#define CONFIG_FLASH_ERASE_SIZE	0x10000
#define CONFIG_FLASH_BANK_SIZE		CONFIG_FLASH_ERASE_SIZE

/* RO image resides at start of protected region, right after header */
#define CONFIG_RO_STORAGE_OFF	CONFIG_RO_HDR_SIZE

#define CONFIG_RW_STORAGE_OFF 0

/* Use 4k sector erase for NPCX monitor flash erase operations. */
#define NPCX_MONITOR_FLASH_ERASE_SIZE	0x1000

#endif /* __CROS_EC_FLASH_CHIP_H */
