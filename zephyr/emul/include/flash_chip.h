/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_INCLUDE_FLASH_CHIP_H
#define __EMUL_INCLUDE_FLASH_CHIP_H

#define CONFIG_RO_STORAGE_OFF           0x0
#define CONFIG_RW_STORAGE_OFF           0x0
#define CONFIG_FLASH_WRITE_SIZE		0x1  /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE	256   /* one page size for write */
#define CONFIG_FLASH_ERASE_SIZE	0x10000
#define CONFIG_FLASH_BANK_SIZE		CONFIG_FLASH_ERASE_SIZE

#endif /* __EMUL_INCLUDE_FLASH_CHIP_H */
