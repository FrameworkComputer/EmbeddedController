/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "console.h"
#include "system.h"
#include "uart.h"
#include "util.h"
#include "vboot.h"


#define SCRATCHPAD_EMPTY       0
#define SCRATCHPAD_REQUEST_A   0xb00daaaa
#define SCRATCHPAD_REQUEST_B   0xb00dbbbb
#define SCRATCHPAD_SELECTED_A  0x0000d1da
#define SCRATCHPAD_SELECTED_B  0x0000d1db
#define SCRATCHPAD_SELECTED_RO 0x0000d1d0
#define SCRATCHPAD_FAILED_A    0x0000eeea
#define SCRATCHPAD_FAILED_B    0x0000eeeb


/* Jumps to one of the RW images if necessary. */
static void jump_to_other_image(void)
{
	int s;

	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return;  /* Not in RO firmware, so ignore scratchpad */

	if (system_get_reset_cause() != SYSTEM_RESET_SOFT_COLD) {
		/* In RO firmware, but not because of a warm boot.
		 * Stay in RO regardless of scratchpad, and clear it
		 * so we don't use it on the next boot. */
		system_set_scratchpad(SCRATCHPAD_EMPTY);
		return;
	}

	/* TODO: check recovery button; if it's pressed, stay in RO */

	/* Check for a scratchpad value we recognize.  Clear the
	 * scratchpad before jumping, so we only do this once. */
	s = system_get_scratchpad();
	if (s == SCRATCHPAD_REQUEST_A) {
		system_set_scratchpad(SCRATCHPAD_SELECTED_A);
		system_run_image_copy(SYSTEM_IMAGE_RW_A);
		/* Shouldn't normally return; if we did, flag error */
		system_set_scratchpad(SCRATCHPAD_FAILED_A);
	} else if (s == SCRATCHPAD_REQUEST_B) {
		system_set_scratchpad(SCRATCHPAD_SELECTED_B);
		system_run_image_copy(SYSTEM_IMAGE_RW_B);
		/* Shouldn't normally return; if we did, flag error */
		system_set_scratchpad(SCRATCHPAD_FAILED_B);
	} else {
		system_set_scratchpad(SCRATCHPAD_EMPTY);
	}
}


/*****************************************************************************/
/* Console commands */

static int command_reboot(int argc, char **argv)
{
	/* Handle request to boot to a specific image */
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "a")) {
			uart_puts("Rebooting to image A!\n\n\n");
			system_set_scratchpad(SCRATCHPAD_REQUEST_A);
		} else if (!strcasecmp(argv[1], "b")) {
			uart_puts("Rebooting to image B!\n\n\n");
			system_set_scratchpad(SCRATCHPAD_REQUEST_B);
		} else {
			uart_puts("Usage: reboot [ A | B ]\n");
			return EC_ERROR_UNKNOWN;
		}
	} else {
		uart_puts("Rebooting to RO!\n\n\n");
	}

        uart_flush_output();
        /* TODO - param to specify warm/cold */
        system_reset(1);
        return EC_SUCCESS;
}

static const struct console_command console_commands[] = {
        {"reboot", command_reboot},
};

static const struct console_group command_group = {
        "Verified boot", console_commands, ARRAY_SIZE(console_commands)
};

/*****************************************************************************/
/* Initialization */

int vboot_pre_init(void)
{
	/* Jump to a different image if necessary; this may not return */
	jump_to_other_image();
	return EC_SUCCESS;
}


int vboot_init(void)
{
        /* Register our internal commands */
        console_register_commands(&command_group);
        return EC_SUCCESS;
}
