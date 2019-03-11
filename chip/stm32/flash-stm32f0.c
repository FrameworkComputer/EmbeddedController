/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "common.h"
#include "flash.h"
#include "registers.h"
#include "util.h"

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_get_protect(int block)
{
	return !(STM32_FLASH_WRPR & BIT(block));
}

/*
 * Note: This does not need to update _NOW flags, as get_protect_flags
 * in common code already does so.
 */
uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;
	uint32_t wrp01 = REG32(STM32_OPTB_BASE + STM32_OPTB_WRP01);
#if CONFIG_FLASH_SIZE > 64 * 1024
	uint32_t wrp23 = REG32(STM32_OPTB_BASE + STM32_OPTB_WRP23);
#endif

	/*
	 * We only need to return detailed flags if we want to protect RW or
	 * ROLLBACK independently (EC_FLASH_PROTECT_RO_AT_BOOT should be set
	 * by pstate logic).
	 */
#if defined(CONFIG_FLASH_PROTECT_RW) || defined(CONFIG_ROLLBACK)
	/* Flags that must be set for each region. */
	const int mask_flags[] = {
		[FLASH_REGION_RW] = EC_FLASH_PROTECT_RW_AT_BOOT,
		[FLASH_REGION_RO] = EC_FLASH_PROTECT_RO_AT_BOOT,
#ifdef CONFIG_ROLLBACK
		[FLASH_REGION_ROLLBACK] = EC_FLASH_PROTECT_ROLLBACK_AT_BOOT,
#endif
	};

	/*
	 * Sets up required mask for wrp01/23 registers: for protection to be
	 * set, values set in the mask must be zeros, values in the mask << 8
	 * must be ones.
	 *
	 * Note that these masks are actually static, and could be precomputed
	 * at build time to save flash space.
	 */
	uint32_t wrp_mask[FLASH_REGION_COUNT][2];
	int i;
	int shift = 0;
	int reg = 0;

	memset(wrp_mask, 0, sizeof(wrp_mask));

	/* Scan flash protection */
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		/* Default: RW. */
		int region = FLASH_REGION_RW;

		if (i >= WP_BANK_OFFSET &&
		    i < WP_BANK_OFFSET + WP_BANK_COUNT)
			region = FLASH_REGION_RO;
#ifdef CONFIG_ROLLBACK
		if (i >= ROLLBACK_BANK_OFFSET &&
		    i < ROLLBACK_BANK_OFFSET + ROLLBACK_BANK_COUNT)
			region = FLASH_REGION_ROLLBACK;
#endif

		switch (i) {
		case 8:
#if CONFIG_FLASH_SIZE > 64 * 1024
		case 24:
#endif
			shift += 8;
			break;
#if CONFIG_FLASH_SIZE > 64 * 1024
		case 16:
			reg = 1;
			shift = 0;
			break;
#endif
		}

		wrp_mask[region][reg] |= 1 << shift;
		shift++;
	}

	for (i = 0; i < FLASH_REGION_COUNT; i++) {
		if (!(wrp01 & wrp_mask[i][0]) &&
			(wrp01 & wrp_mask[i][0] << 8) == (wrp_mask[i][0] << 8))
#if CONFIG_FLASH_SIZE > 64 * 1024
			if (!(wrp23 & wrp_mask[i][1]) &&
			     (wrp23 & wrp_mask[i][1] << 8) ==
							 (wrp_mask[i][1] << 8))
#endif
				flags |= mask_flags[i];
	}
#endif /* CONFIG_FLASH_PROTECT_RW || CONFIG_ROLLBACK */

	if (wrp01 == 0xff00ff00)
#if CONFIG_FLASH_SIZE > 64 * 1024
		if (wrp23 == 0xff00ff00)
#endif
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
	       EC_FLASH_PROTECT_RO_NOW |
#ifdef CONFIG_FLASH_PROTECT_RW
	       EC_FLASH_PROTECT_RW_AT_BOOT |
	       EC_FLASH_PROTECT_RW_NOW |
#endif
#ifdef CONFIG_ROLLBACK
	       EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
	       EC_FLASH_PROTECT_ROLLBACK_NOW |
#endif
	       EC_FLASH_PROTECT_ALL_AT_BOOT |
	       EC_FLASH_PROTECT_ALL_NOW;
}

uint32_t flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * ALL/RW at-boot state can be set if WP GPIO is asserted and can always
	 * be cleared.
	 */
	if (cur_flags & (EC_FLASH_PROTECT_ALL_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_AT_BOOT;

#ifdef CONFIG_FLASH_PROTECT_RW
	if (cur_flags & (EC_FLASH_PROTECT_RW_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_RW_AT_BOOT;
#endif

#ifdef CONFIG_ROLLBACK
	if (cur_flags & (EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
			 EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
#endif

	return ret;
}
