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

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT |
	       EC_FLASH_PROTECT_ALL_AT_BOOT |
	       EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * RW at-boot state can be set if WP GPIO is asserted and can always
	 * be cleared.
	 */
	if (cur_flags & (EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_AT_BOOT;

	return ret;
}
