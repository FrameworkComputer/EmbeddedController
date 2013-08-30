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
 *
 * Additionally the LCDVCC needs to be delayed on a 0->1 transition of the
 * PCH's EDP VDD enable signal to meet the panel specification.
 */

#define BL_ENABLE_DELAY_US 420000 /* 420 ms delay */
#define LCDVCC_ENABLE_DELAY_US 270000 /* 270 ms delay */

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


static int lcdvcc_en_deferred_value;

static void set_lcdvcc_en_value(void)
{
	gpio_set_level(GPIO_EC_EDP_VDD_EN, lcdvcc_en_deferred_value);
}
DECLARE_DEFERRED(set_lcdvcc_en_value);

void lcdvcc_interrupt(enum gpio_signal signal)
{
	int pch_value;

	pch_value = gpio_get_level(GPIO_PCH_EDP_VDD_EN);

	/* Immediately disable the LCDVCC when the PCH indicates as such. */
	if (!pch_value) {
		/* If there was a scheduled callback pending make sure it picks
		 * up the disabled value. */
		lcdvcc_en_deferred_value = 0;
		gpio_set_level(GPIO_EC_EDP_VDD_EN, 0);
		/* Cancel pending hook */
		hook_call_deferred(&set_lcdvcc_en_value, -1);
		return;
	}
	/* Handle a 0->1 transition by calling a deferred hook. */
	if (pch_value && !lcdvcc_en_deferred_value) {
		lcdvcc_en_deferred_value = 1;
		hook_call_deferred(&set_lcdvcc_en_value,
		                   LCDVCC_ENABLE_DELAY_US);
	}
}

/**
 * Initialize panel module.
 */
static void panel_init(void)
{
	/* Set initial deferred value and signal to the current PCH signal. */
	backlight_deferred_value = gpio_get_level(GPIO_PCH_BKLTEN);
	set_backlight_value();

	update_backlight();

	gpio_enable_interrupt(GPIO_PCH_BKLTEN);

	/* The interrupt is enabled for the GPIO_PCH_EDP_VDD_EN in the
	 * chipset_haswell.c compilation unit. Initially set the value
	 * to whatever it current is reading. */
	lcdvcc_en_deferred_value = gpio_get_level(GPIO_PCH_EDP_VDD_EN);
	set_lcdvcc_en_value();
}
DECLARE_HOOK(HOOK_INIT, panel_init, HOOK_PRIO_DEFAULT);

