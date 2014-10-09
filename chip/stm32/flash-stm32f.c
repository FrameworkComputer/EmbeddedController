/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC */

#include "common.h"
#include "flash.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "panic.h"

/* Flag indicating whether we have locked down entire flash */
static int entire_flash_locked;

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */
#define FLASH_HOOK_VERSION 1
/* The previous write protect state before sys jump */
/*
 * TODO(crosbug.com/p/23798): check if STM32L code works here too - that is,
 * check if entire flash is locked by attempting to lock it rather than keeping
 * a global variable.
 */
struct flash_wp_state {
	int entire_flash_locked;
};

/*****************************************************************************/
/* Physical layer APIs */

int flash_physical_get_protect(int block)
{
	return entire_flash_locked || !(STM32_FLASH_WRPR & (1 << block));
}

uint32_t flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	/* Read all-protected state from our shadow copy */
	if (entire_flash_locked)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	return flags;
}

int flash_physical_protect_now(int all)
{
	if (all) {
		/*
		 * Lock by writing a wrong key to FLASH_KEYR. This triggers a
		 * bus fault, so we need to disable bus fault handler while
		 * doing this.
		 */
		ignore_bus_fault(1);
		STM32_FLASH_KEYR = 0xffffffff;
		ignore_bus_fault(0);

		entire_flash_locked = 1;

		return EC_SUCCESS;
	} else {
		/* No way to protect just the RO flash until next boot */
		return EC_ERROR_INVAL;
	}
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
	if (reset_flags & RESET_FLAG_SYSJUMP) {
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
