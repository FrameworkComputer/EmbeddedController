/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "console.h"
#include "gpio.h"
#include "keyboard_scan.h"
#include "system.h"
#include "util.h"
#include "vboot.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)


/* Might I want to jump to one of the RW images? */
static int maybe_jump_to_other_image(void)
{
	/* Not all boards even have RW EC firmware. I think it's just Link at
	 * the moment. */
#ifndef BOARD_link
	/* TODO: (crosbug.com/p/8561) once daisy can warm-boot to another
	 * image, enable it here too. */
	CPUTS("[Vboot staying in RO because that's all there is]\n");
	return 0;
#endif

	/* We'll only jump to another image if we're currently in RO */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return 0;

#ifdef CONFIG_TASK_KEYSCAN
	/* Don't jump if recovery requested */
	if (keyboard_scan_recovery_pressed()) {
		CPUTS("[Vboot staying in RO because recovery key pressed]\n");
		return 0;
	}
#endif

	/* Don't jump if we're in RO becuase we jumped there (this keeps us
	 * from jumping to RO only to jump right back). */
	if (system_jumped_to_this_image())
		return 0;

#if !defined(BOARD_daisy)
	/* TODO: (crosbug.com/p/8572) Daisy doesn't define a GPIO
	 * for the recovery signal from servo, so can't check it. */
	if (gpio_get_level(GPIO_RECOVERYn) == 0) {
		CPUTS("[Vboot staying in RO due to recovery signal]\n");
		return 0;
	}
#endif

	/* Okay, we might want to jump to a RW image. */
	return 1;
}

/*****************************************************************************/
/* Initialization */

int vboot_pre_init(void)
{
	/* FIXME(wfrichar): crosbug.com/p/7453: should protect flash */
	return EC_SUCCESS;
}


int vboot_init(void)
{
	/* nothing to do, so do nothing */
	if (!maybe_jump_to_other_image())
		return EC_SUCCESS;

	/* FIXME(wfrichar): placeholder for full verified boot implementation.
	 * TBD exactly how, but we may want to continue in RO firmware, jump
	 * directly to one of the RW firmwares, etc. */
	CPRINTF("[ROOT_KEY is at 0x%x, size 0x%x]\n",
		CONFIG_VBOOT_ROOTKEY_OFF, CONFIG_VBOOT_ROOTKEY_SIZE);
	CPRINTF("[FW_MAIN_A is at 0x%x, size 0x%x]\n",
		CONFIG_FW_A_OFF, CONFIG_FW_A_SIZE);
	CPRINTF("[VBLOCK_A is at 0x%x, size 0x%x]\n",
		CONFIG_VBLOCK_A_OFF, CONFIG_VBLOCK_A_SIZE);
	CPRINTF("[FW_MAIN_B is at 0x%x, size 0x%x]\n",
		CONFIG_FW_B_OFF, CONFIG_FW_B_SIZE);
	CPRINTF("[VBLOCK_B is at 0x%x, size 0x%x]\n",
		CONFIG_VBLOCK_B_OFF, CONFIG_VBLOCK_B_SIZE);

	system_run_image_copy(SYSTEM_IMAGE_RW_A, 0);
	return EC_SUCCESS;
}
