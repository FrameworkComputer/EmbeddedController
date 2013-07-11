/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Backlight passthru for x86 platforms */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"

/**
 * Update backlight state.
 */
static void update_backlight(void)
{
	/* Only enable the backlight if the lid is open */
	if (gpio_get_level(GPIO_PCH_BKLTEN) && lid_is_open())
		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
	else
		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_backlight, HOOK_PRIO_DEFAULT);

/**
 * Initialize backlight module.
 */
static void backlight_init(void)
{
	update_backlight();

	gpio_enable_interrupt(GPIO_PCH_BKLTEN);
}
DECLARE_HOOK(HOOK_INIT, backlight_init, HOOK_PRIO_DEFAULT);

void backlight_interrupt(enum gpio_signal signal)
{
	update_backlight();
}

/**
 * Host command to toggle backlight.
 */
static int switch_command_enable_backlight(struct host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_backlight *p = args->params;
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, p->enabled);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_BKLIGHT,
		     switch_command_enable_backlight, 0);


