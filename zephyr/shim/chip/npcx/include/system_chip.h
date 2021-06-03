/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SYSTEM_CHIP_H_
#define __CROS_EC_SYSTEM_CHIP_H_

/* The utilities and variables depend on npcx chip family */
#if defined(CONFIG_SOC_SERIES_NPCX5) || \
	defined(CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API)
/* Begin address for hibernate utility; defined in linker script */
extern unsigned int __flash_lpfw_start;

/* End address for hibernate utility; defined in linker script */
extern unsigned int __flash_lpfw_end;

/* Begin address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_start;

/* End address for little FW; defined in linker script */
extern unsigned int __flash_lplfw_end;
#endif

#endif // __CROS_EC_SYSTEM_CHIP_H_
