/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_backlight.h"
#include "lid_switch.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

static struct kblight_conf kblight;
static int current_percent;

void __attribute__((weak)) board_kblight_init(void)
{ }

static int kblight_init(void)
{
	if (!kblight.drv || !kblight.drv->init)
		return EC_ERROR_UNIMPLEMENTED;
	return kblight.drv->init();
}

static void kblight_set_deferred(void)
{
	if (!kblight.drv || !kblight.drv->set)
		return;
	kblight.drv->set(current_percent);
}
DECLARE_DEFERRED(kblight_set_deferred);

/*
 * APIs
 */
int kblight_set(int percent)
{
	if (percent < 0 || 100 < percent)
		return EC_ERROR_INVAL;
	current_percent = percent;
	/* Need to defer i2c in case it's called from an interrupt handler. */
	hook_call_deferred(&kblight_set_deferred_data, 0);
	return EC_SUCCESS;
}

int kblight_get(void)
{
	return current_percent;
}

int kblight_enable(int enable)
{
	if (!kblight.drv || !kblight.drv->enable)
		return -1;
	return kblight.drv->enable(enable);
}

int kblight_register(const struct kblight_drv *drv)
{
	kblight.drv = drv;
	CPRINTS("kblight registered");
	return EC_SUCCESS;
}

/*
 * Hooks
 */
static void keyboard_backlight_init(void)
{
	/* Uses PWM by default. Can be customized by board_kblight_init */
#ifdef CONFIG_PWM_KBLIGHT
	kblight_register(&kblight_pwm);
#endif
	board_kblight_init();
	if (kblight_init())
		CPRINTS("kblight init failed");
}
DECLARE_HOOK(HOOK_INIT, keyboard_backlight_init, HOOK_PRIO_DEFAULT);

static void kblight_suspend(void)
{
	kblight_enable(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, kblight_suspend, HOOK_PRIO_DEFAULT);

static void kblight_resume(void)
{
	if (lid_is_open() && current_percent) {
		kblight_enable(1);
		kblight_set(current_percent);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, kblight_resume, HOOK_PRIO_DEFAULT);

static void kblight_lid_change(void)
{
	if (lid_is_open() && current_percent)
		kblight_resume();
	else
		kblight_enable(0);
}
DECLARE_HOOK(HOOK_LID_CHANGE, kblight_lid_change, HOOK_PRIO_DEFAULT);

/*
 * Console and host commands
 */
static int cc_kblight(int argc, char **argv)
{
	if (argc >= 2) {
		char *e;
		int i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		if (kblight_set(i))
			return EC_ERROR_PARAM1;
		if (kblight_enable(i > 0))
			return EC_ERROR_PARAM1;
	}
	ccprintf("Keyboard backlight: %d%%\n", kblight_get());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kblight, cc_kblight,
			"percent",
			"Get/set keyboard backlight");

enum ec_status hc_get_keyboard_backlight(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_keyboard_backlight *r = args->response;

	r->percent = kblight_get();
	r->enabled = 1;			/* Deprecated */
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT,
		     hc_get_keyboard_backlight,
		     EC_VER_MASK(0));

enum ec_status hc_set_keyboard_backlight(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_keyboard_backlight *p = args->params;

	if (kblight_set(p->percent))
		return EC_RES_ERROR;
	if (kblight_enable(p->percent > 0))
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT,
		     hc_set_keyboard_backlight,
		     EC_VER_MASK(0));
