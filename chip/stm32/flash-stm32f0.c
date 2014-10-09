/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "common.h"
#include "flash.h"
#include "registers.h"

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_get_protect(int block)
{
	return !(STM32_FLASH_WRPR & (1 << block));
}

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;
	uint32_t wrp01 = REG32(STM32_OPTB_BASE + STM32_OPTB_WRP01);
	uint32_t wrp23 = REG32(STM32_OPTB_BASE + STM32_OPTB_WRP23);

	if (STM32_FLASH_WRPR == 0)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	if (wrp01 == 0xff00ff00 && wrp23 == 0xff00ff00)
		flags |= EC_FLASH_PROTECT_ALL_AT_BOOT;

	return flags;
}

int flash_physical_protect_now(int all)
{
	return EC_ERROR_INVAL;
}

int flash_physical_restore_state(void)
{
	/* Nothing to restore */
	return 0;
}
