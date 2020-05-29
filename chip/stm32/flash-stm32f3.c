/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for stm32f3 and stm32f4 */

#include <stdbool.h>
#include "common.h"
#include "flash.h"
#include "flash-f.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "panic.h"

/*****************************************************************************/
/* Physical layer APIs */
#ifdef CHIP_VARIANT_STM32F76X
/*
 * 8 "erase" sectors : 32KB/32KB/32KB/32KB/128KB/256KB/256KB/256KB
 */
struct ec_flash_bank const flash_bank_array[] = {
	{
		.count = 4,
		.size_exp = __fls(SIZE_32KB),
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.erase_size_exp = __fls(SIZE_32KB),
		.protect_size_exp = __fls(SIZE_32KB),
	},
	{
		.count = 1,
		.size_exp = __fls(SIZE_128KB),
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.erase_size_exp = __fls(SIZE_128KB),
		.protect_size_exp = __fls(SIZE_128KB),
	},
	{
		.count = (CONFIG_FLASH_SIZE - SIZE_256KB) / SIZE_256KB,
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.size_exp = __fls(SIZE_256KB),
		.erase_size_exp = __fls(SIZE_256KB),
		.protect_size_exp = __fls(SIZE_256KB),
	},
};
#elif defined(CHIP_FAMILY_STM32F4)
/*
 * STM32F412xE has 512 KB flash
 *   8 "erase" sectors (512 KB) : 16KB/16KB/16KB/16KB/64KB/128KB/128KB/128KB
 *
 * STM32F412xG has 1 MB flash
 *   12 "erase" sectors (1024 KB) :
 *           16KB/16KB/16KB/16KB/64KB/128KB/128KB/128KB/128KB/128KB/128KB/128KB
 *
 * https://www.st.com/resource/en/datasheet/stm32f412cg.pdf
 */
struct ec_flash_bank const flash_bank_array[] = {
	{
		.count = 4,
		.size_exp = __fls(SIZE_16KB),
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.erase_size_exp = __fls(SIZE_16KB),
		.protect_size_exp = __fls(SIZE_16KB),
	},
	{
		.count = 1,
		.size_exp = __fls(SIZE_64KB),
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.erase_size_exp = __fls(SIZE_64KB),
		.protect_size_exp = __fls(SIZE_64KB),
	},
	{
		.count = (CONFIG_FLASH_SIZE - SIZE_128KB) / SIZE_128KB,
		.write_size_exp = __fls(CONFIG_FLASH_WRITE_SIZE),
		.size_exp = __fls(SIZE_128KB),
		.erase_size_exp = __fls(SIZE_128KB),
		.protect_size_exp = __fls(SIZE_128KB),
	},
};
#endif

/* Flag indicating whether we have locked down entire flash */
static int entire_flash_locked;

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1

/* The previous write protect state before sys jump */
struct flash_wp_state {
	int entire_flash_locked;
};

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_get_protect(int block)
{
	return (entire_flash_locked ||
#if defined(CHIP_FAMILY_STM32F3)
		!(STM32_FLASH_WRPR & BIT(block))
#elif defined(CHIP_FAMILY_STM32F4)
		!(STM32_OPTB_WP & STM32_OPTB_nWRP(block))
#endif
	       );
}

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	/* Read all-protected state from our shadow copy */
	if (entire_flash_locked)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

#if defined(CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE)
	if (is_flash_rdp_enabled())
		flags |= EC_FLASH_PROTECT_RO_AT_BOOT;
#endif

	return flags;
}

int flash_physical_protect_now(int all)
{
	if (all) {
		disable_flash_control_register();
		entire_flash_locked = 1;

		return EC_SUCCESS;
	}

	disable_flash_option_bytes();

	return EC_SUCCESS;
}

uint32_t flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT |
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
	 * If entire flash isn't protected at this boot, it can be enabled if
	 * the WP GPIO is asserted.
	 */
	if (!(cur_flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    (cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

int flash_physical_restore_state(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	int version, size;
	const struct flash_wp_state *prev;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection. Nothing additional needs to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP) {
		prev = (const struct flash_wp_state *)system_get_jump_tag(
				FLASH_SYSJUMP_TAG, &version, &size);
		if (prev && version == FLASH_HOOK_VERSION &&
		    size == sizeof(*prev))
			entire_flash_locked = prev->entire_flash_locked;
		return 1;
	}

	return 0;
}

/*****************************************************************************/
/* Hooks */

static void flash_preserve_state(void)
{
	struct flash_wp_state state;

	state.entire_flash_locked = entire_flash_locked;

	system_add_jump_tag(FLASH_SYSJUMP_TAG, FLASH_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, flash_preserve_state, HOOK_PRIO_DEFAULT);
