/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Backlight control based on lid and optional request signal from AP */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"


/**
 * Activate/Deactivate the backlight GPIO pin considering active high or low.
 */
void enable_backlight(int enabled)
{
#ifdef CONFIG_BACKLIGHT_LID_ACTIVE_LOW
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, !enabled);
#else
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, enabled);
#endif
}

/**
 * Update backlight state.
 */
static void update_backlight(void)
{
#ifdef CONFIG_BACKLIGHT_REQ_GPIO
	/* Enable the backlight if lid is open AND requested by AP */
	enable_backlight(lid_is_open() &&
		gpio_get_level(CONFIG_BACKLIGHT_REQ_GPIO));
#else
	/*
	 * Enable backlight if lid is open; this is AND'd with the request from
	 * the AP in hardware.
	 */
	enable_backlight(lid_is_open());
#endif
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_backlight, HOOK_PRIO_DEFAULT);

/**
 * Initialize backlight module.
 */
static void backlight_init(void)
{
	update_backlight();

#ifdef CONFIG_BACKLIGHT_REQ_GPIO
	gpio_enable_interrupt(CONFIG_BACKLIGHT_REQ_GPIO);
#endif
}
DECLARE_HOOK(HOOK_INIT, backlight_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_BACKLIGHT_REQ_GPIO
void backlight_interrupt(enum gpio_signal signal)
{
	update_backlight();
}
#endif

/**
 * Host command to toggle backlight.
 *
 * The requested state will persist until the next lid-switch or request-gpio
 * transition.
 */
static enum ec_status
switch_command_enable_backlight(struct host_cmd_handler_args *args)
{
	const struct ec_params_switch_enable_backlight *p = args->params;

	enable_backlight(p->enabled);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SWITCH_ENABLE_BKLIGHT,
		     switch_command_enable_backlight,
		     EC_VER_MASK(0));


