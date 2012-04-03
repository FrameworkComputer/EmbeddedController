/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "gpio.h"
#include "keyboard_scan.h"
#include "system.h"
#include "uart.h"
#include "util.h"
#include "vboot.h"


/* Jumps to one of the RW images if necessary. */
static void jump_to_other_image(void)
{
	/* Only jump to another image if we're currently in RO */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return;

	/* Don't jump if recovery requested */
	if (keyboard_scan_recovery_pressed()) {
		uart_puts("Vboot staying in RO because key pressed.\n");
		return;
	}

	/* Don't jump if we're in RO becuase we jumped there (this keeps us
	 * from jumping to RO only to jump right back). */
	if (system_jumped_to_this_image())
		return;

#if !defined(BOARD_daisy) && !defined(BOARD_discovery) && !defined(BOARD_adv)
	/* TODO: (crosbug.com/p/8572) Daisy and discovery don't define a GPIO
	 * for the recovery signal from servo, so can't check it. */
	if (gpio_get_level(GPIO_RECOVERYn) == 0) {
		uart_puts("Vboot staying in RO due to recovery signal.\n");
		return;
	}
#endif


#ifdef BOARD_link
	/* TODO: (crosbug.com/p/8561) once daisy can warm-boot to another
	 * image, enable this there too. */
	/* TODO: real verified boot (including recovery reason); for now, just
	 * jump to image A. */
	system_run_image_copy(SYSTEM_IMAGE_RW_A);
#endif
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
	/* FIXME(wfrichar): placeholder for full verified boot implementation.
	 * TBD exactly how, but we may want to continue in RO firmware, jump
	 * directly to one of the RW firmwares, etc. */
	jump_to_other_image();
	return EC_SUCCESS;
}
