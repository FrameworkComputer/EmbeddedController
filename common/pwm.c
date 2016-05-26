/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "pwm.h"
#include "util.h"

#ifdef CONFIG_PWM

/*
 * Get target channel based on type / index host command parameters.
 * Returns 0 if a valid channel is selected, -1 on error.
 */
static int get_target_channel(enum pwm_channel *channel, int type, int index)
{
	switch (type) {
	case EC_PWM_TYPE_GENERIC:
		*channel = index;
		break;
#ifdef CONFIG_PWM_KBLIGHT
	case EC_PWM_TYPE_KB_LIGHT:
		*channel = PWM_CH_KBLIGHT;
		break;
#endif
#ifdef CONFIG_PWM_DISPLIGHT
	case EC_PWM_TYPE_DISPLAY_LIGHT:
		*channel = PWM_CH_DISPLIGHT;
		break;
#endif
	default:
		return -1;
	}

	return *channel >= PWM_CH_COUNT;
}

/*
 * TODO(crbug.com/615109): These host commands use 16 bit duty cycle, but
 * all of our internal code uses percent on [0, 100]. Convert internal
 * functions to use 16 bit duty and remove the conversions below.
 */
static int host_command_pwm_set_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_duty *p = args->params;
	enum pwm_channel channel;
	int percent;

	/* Convert 16 bit duty to percent on [0, 100] */
	percent = DIV_ROUND_NEAREST(p->duty * 100, EC_PWM_MAX_DUTY);

	if (get_target_channel(&channel, p->pwm_type, p->index))
		return EC_RES_INVALID_PARAM;

	pwm_set_duty(channel, percent);
	pwm_enable(channel, percent > 0);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_DUTY,
		     host_command_pwm_set_duty,
		     EC_VER_MASK(0));

static int host_command_pwm_get_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_get_duty *p = args->params;
	struct ec_response_pwm_get_duty *r = args->response;

	enum pwm_channel channel;

	if (get_target_channel(&channel, p->pwm_type, p->index))
		return EC_RES_INVALID_PARAM;

	/* Convert percent on [0, 100] to 16 bit duty */
	r->duty = pwm_get_duty(channel) * EC_PWM_MAX_DUTY / 100;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_DUTY,
		     host_command_pwm_get_duty,
		     EC_VER_MASK(0));

/**
 * Print status of a PWM channel.
 *
 * @param ch		Channel to print.
 */
static void print_channel(enum pwm_channel ch)
{
	if (pwm_get_enabled(ch))
		ccprintf("  %d: %d%%\n", ch, pwm_get_duty(ch));
	else
		ccprintf("  %d: disabled\n", ch);
}

static int cc_pwm_duty(int argc, char **argv)
{
	int percent = 0;
	int ch;
	char *e;

	if (argc < 2) {
		ccprintf("PWM channels:\n");
		for (ch = 0; ch < PWM_CH_COUNT; ch++)
			print_channel(ch);
		return EC_SUCCESS;
	}

	ch = strtoi(argv[1], &e, 0);
	if (*e || ch < 0 || ch >= PWM_CH_COUNT)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		percent = strtoi(argv[2], &e, 0);
		if (*e || percent > 100) {
			/* Bad param */
			return EC_ERROR_PARAM1;
		} else if (percent < 0) {
			/* Negative = disable */
			pwm_enable(ch, 0);
		} else {
			ccprintf("Setting channel %d to %d%%\n", ch, percent);
			pwm_enable(ch, 1);
			pwm_set_duty(ch, percent);
		}
	}

	print_channel(ch);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pwmduty, cc_pwm_duty,
			"[channel [<percent> | -1=disable]]",
			"Get/set PWM duty cycles ",
			NULL);
#endif /* CONFIG_PWM */

/* Initialize all PWM pins as functional */
static void pwm_pin_init(void)
{
	gpio_config_module(MODULE_PWM, 1);
}
/* HOOK_PRIO_INIT_PWM may be used for chip PWM unit init, so use PRIO + 1 */
DECLARE_HOOK(HOOK_INIT, pwm_pin_init, HOOK_PRIO_INIT_PWM + 1);
