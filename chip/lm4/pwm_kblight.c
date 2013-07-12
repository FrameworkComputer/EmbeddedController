/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for Chromebook keyboard backlight. */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "pwm.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "thermal.h"
#include "timer.h"
#include "util.h"

/* Max PWM for controller */
#define MAX_PWM 0x1ff

#define PWMKBD_SYSJUMP_TAG 0x504b  /* "PK" */
#define PWM_HOOK_VERSION 1
/* Saved PWM state across sysjumps */
struct pwm_kbd_state {
	uint8_t kblight_en;
	uint8_t kblight_percent;
	uint8_t pad0, pad1; /* Pad to multiple of 4 bytes. */
};

void pwm_enable_keyboard_backlight(int enable)
{
	if (enable)
		LM4_FAN_FANCTL |= (1 << FAN_CH_KBLIGHT);
	else
		LM4_FAN_FANCTL &= ~(1 << FAN_CH_KBLIGHT);
}

int pwm_get_keyboard_backlight_enabled(void)
{
	return (LM4_FAN_FANCTL & (1 << FAN_CH_KBLIGHT)) ? 1 : 0;
}

int pwm_get_keyboard_backlight(void)
{
	return ((LM4_FAN_FANCMD(FAN_CH_KBLIGHT) >> 16) * 100 +
		MAX_PWM / 2) / MAX_PWM;
}

void pwm_set_keyboard_backlight(int percent)
{
	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	LM4_FAN_FANCMD(FAN_CH_KBLIGHT) = ((percent * MAX_PWM + 50) / 100) << 16;
}

/*****************************************************************************/
/* Console commands */

static int command_kblight(int argc, char **argv)
{
	if (argc >= 2) {
		char *e;
		int i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		pwm_set_keyboard_backlight(i);
	}

	ccprintf("Keyboard backlight: %d%%\n", pwm_get_keyboard_backlight());
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kblight, command_kblight,
			"percent",
			"Set keyboard backlight",
			NULL);

/*****************************************************************************/
/* Host commands */

int pwm_command_get_keyboard_backlight(struct host_cmd_handler_args *args)
{
	struct ec_response_pwm_get_keyboard_backlight *r = args->response;

	r->percent = pwm_get_keyboard_backlight();
	r->enabled = pwm_get_keyboard_backlight_enabled();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT,
		     pwm_command_get_keyboard_backlight,
		     EC_VER_MASK(0));

int pwm_command_set_keyboard_backlight(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_keyboard_backlight *p = args->params;

	pwm_set_keyboard_backlight(p->percent);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT,
		     pwm_command_set_keyboard_backlight,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Hooks */

static void pwm_kblight_init(void)
{
	const struct pwm_kbd_state *prev;
	int version, size;

	/* Enable the fan module and delay a few clocks */
	LM4_SYSTEM_RCGCFAN = 1;
	clock_wait_cycles(3);

	/* Configure GPIOs */
	configure_kblight_gpios();

	/* Disable all fans */
	LM4_FAN_FANCTL = 0;

	/*
	 * Configure keyboard backlight:
	 * 0x0000 = bit 15     = auto-restart
	 * 0x0000 = bit 14     = slow acceleration
	 * 0x0000 = bits 13:11 = no hysteresis
	 * 0x0000 = bits 10:8  = start period (2<<0) edges
	 * 0x0000 = bits 7:6   = no fast start
	 * 0x0000 = bits 5:4   = average 4 edges when calculating RPM
	 * 0x0000 = bits 3:2   = 4 pulses per revolution
	 * 0x0001 = bit 0      = manual control
	 */
	LM4_FAN_FANCH(FAN_CH_KBLIGHT) = 0x0001;

	prev = (const struct pwm_kbd_state *)
		system_get_jump_tag(PWMKBD_SYSJUMP_TAG, &version, &size);
	if (prev && version == PWM_HOOK_VERSION && size == sizeof(*prev)) {
		/* Restore previous state. */
		pwm_enable_keyboard_backlight(prev->kblight_en);
		pwm_set_keyboard_backlight(prev->kblight_percent);
	} else {
		/* Enable keyboard backlight control, turned down */
		pwm_set_keyboard_backlight(0);
		pwm_enable_keyboard_backlight(1);
	}
}
DECLARE_HOOK(HOOK_INIT, pwm_kblight_init, HOOK_PRIO_DEFAULT);

static void pwm_kblight_preserve_state(void)
{
	struct pwm_kbd_state state;

	state.kblight_en = pwm_get_keyboard_backlight_enabled();
	state.kblight_percent = pwm_get_keyboard_backlight();

	system_add_jump_tag(PWMKBD_SYSJUMP_TAG, PWM_HOOK_VERSION,
			    sizeof(state), &state);
}
DECLARE_HOOK(HOOK_SYSJUMP, pwm_kblight_preserve_state, HOOK_PRIO_DEFAULT);

static void pwm_kblight_suspend(void)
{
	pwm_set_keyboard_backlight(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pwm_kblight_suspend, HOOK_PRIO_DEFAULT);

static void pwm_kblight_shutdown(void)
{
	pwm_set_keyboard_backlight(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pwm_kblight_shutdown, HOOK_PRIO_DEFAULT);

static void pwm_kblight_lid_change(void)
{
	pwm_enable_keyboard_backlight(lid_is_open());
}
DECLARE_HOOK(HOOK_LID_CHANGE, pwm_kblight_lid_change, HOOK_PRIO_DEFAULT);
