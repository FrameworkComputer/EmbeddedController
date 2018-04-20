/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Keyboard backlight control
 */

#include "console.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "lm3509.h"
#include "pwm.h"
#include "util.h"

static void (*kblight_set)(int percent);
static int (*kblight_get)(void);
static void (*kblight_power)(int enable);

/*
 * PWM routines
 */
static void kblight_pwm_set(int percent)
{
	pwm_set_duty(PWM_CH_KBLIGHT, percent);
}

static int kblight_pwm_get(void)
{
	return pwm_get_duty(PWM_CH_KBLIGHT);
}

static void kblight_pwm_power(int enable)
{
	pwm_enable(PWM_CH_KBLIGHT, enable);
}

/*
 * I2C routines
 */
static void kblight_i2c_set(int percent)
{
	lm3509_set_brightness(percent);
}

static int kblight_i2c_get(void)
{
	int percent;
	if (lm3509_get_brightness(&percent))
		percent = 0;
	return percent;
}

static void kblight_i2c_power(int enable)
{
	lm3509_power(enable);
}

static void kblight_init(void)
{
	uint32_t oem = PROJECT_NAMI;
	uint32_t sku = 0;

	cbi_get_oem_id(&oem);
	cbi_get_sku_id(&sku);

	switch (oem) {
	default:
	case PROJECT_NAMI:
	case PROJECT_VAYNE:
	case PROJECT_PANTHEON:
		kblight_set = kblight_i2c_set;
		kblight_get = kblight_i2c_get;
		kblight_power = kblight_i2c_power;
		break;
	case PROJECT_SONA:
		if (sku == 0x3AE2)
			break;
		kblight_set = kblight_pwm_set;
		kblight_get = kblight_pwm_get;
		kblight_power = kblight_pwm_power;
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, kblight_init, HOOK_PRIO_DEFAULT);

static void kblight_suspend(void)
{
	if (kblight_power)
		kblight_power(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, kblight_suspend, HOOK_PRIO_DEFAULT);

static void kblight_resume(void)
{
	if (kblight_power)
		kblight_power(lid_is_open());
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, kblight_resume, HOOK_PRIO_DEFAULT);

static void kblight_lid_change(void)
{
	if (kblight_power)
		kblight_power(lid_is_open());
}
DECLARE_HOOK(HOOK_LID_CHANGE, kblight_lid_change, HOOK_PRIO_DEFAULT);

static int hc_set_kblight(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_keyboard_backlight *p = args->params;
	/* Assume already enabled */
	if (!kblight_set)
		return EC_RES_UNAVAILABLE;
	kblight_set(p->percent);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT,
		     hc_set_kblight, EC_VER_MASK(0));

static int hc_get_kblight(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_keyboard_backlight *r = args->response;
	if (!kblight_get)
		return EC_RES_UNAVAILABLE;
	r->percent = kblight_get();
	/* Assume always enabled */
	r->enabled = 1;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT,
		     hc_get_kblight, EC_VER_MASK(0));

static int cc_kblight(int argc, char **argv)
{
	int i;
	char *e;

	if (argc < 2) {
		if (!kblight_get)
			return EC_ERROR_UNIMPLEMENTED;
		ccprintf("%d\n", kblight_get());
		return EC_SUCCESS;
	}

	if (!kblight_set)
		return EC_ERROR_UNIMPLEMENTED;
	i = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	kblight_set(i);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kblight, cc_kblight,
			"kblight [percent]",
			"Get/set keyboard backlight brightness");
