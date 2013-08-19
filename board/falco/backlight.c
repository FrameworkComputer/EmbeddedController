/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"

/*
 * Falco needs a 420ms delay for a 0->1 transition of the PCH's backlight
 * enable signal. The reason is that Falco has a LVDS bridge which controls
 * all the other signals for the panel except the backlight. In order to
 * meet the penel power sequencing requirements a delay needs to be added.
 */

#define BL_ENABLE_DELAY_US 420000 /* 420 ms delay */

static int backlight_deferred_value;

static void set_backlight_value(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, backlight_deferred_value);
}
DECLARE_DEFERRED(set_backlight_value);

/**
 * Update backlight state.
 */
static void update_backlight(void)
{
	int pch_value;

	pch_value = gpio_get_level(GPIO_PCH_BKLTEN);

	/* Immediately disable the backlight when the lid is closed or the PCH
	 * is instructing the backlight to be disabled. */
	if (!lid_is_open() || !pch_value) {
		/* If there was a scheduled callback pending make sure it picks
		 * up the disabled value. */
		backlight_deferred_value = 0;
		gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
		/* Cancel pending hook */
		hook_call_deferred(&set_backlight_value, -1);
		return;
	}
	/* Handle a 0->1 transition by calling a deferred hook. */
	if (pch_value && !backlight_deferred_value) {
		backlight_deferred_value = 1;
		hook_call_deferred(&set_backlight_value, BL_ENABLE_DELAY_US);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_backlight, HOOK_PRIO_DEFAULT);

/**
 * Initialize backlight module.
 */
static void backlight_init(void)
{
	/* Set initial deferred value and signal to the current PCH signal. */
	backlight_deferred_value = gpio_get_level(GPIO_PCH_BKLTEN);
	set_backlight_value();

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


