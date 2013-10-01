/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Kirby LED driver.
 */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "pwm.h"
#include "util.h"

/* Brightness of each color. Range = 0 - 100. */
#define BRIGHTNESS_RED    50
#define BRIGHTNESS_GREEN  25
#define BRIGHTNESS_YELLOW 50

static int led_auto_control = 1;

void led_set_color(uint8_t red, uint8_t green, uint8_t yellow)
{
	if (!yellow)
		pwm_enable(PWM_CH_CHG_Y, 0);
	if (!green)
		pwm_enable(PWM_CH_CHG_G, 0);
	if (!red)
		pwm_enable(PWM_CH_CHG_R, 0);

	/* Only allow one color of LED */
	if (yellow) {
		pwm_enable(PWM_CH_CHG_Y, 1);
		pwm_set_duty(PWM_CH_CHG_Y, yellow);
	} else if (green) {
		pwm_enable(PWM_CH_CHG_G, 1);
		pwm_set_duty(PWM_CH_CHG_G, green);
	} else if (red) {
		pwm_enable(PWM_CH_CHG_R, 1);
		pwm_set_duty(PWM_CH_CHG_R, red);
	} else {
		gpio_config_module(MODULE_LED_KIRBY, 0);
		gpio_set_level(GPIO_CHG_LED_Y, 0);
		gpio_set_level(GPIO_CHG_LED_G, 0);
		gpio_set_level(GPIO_CHG_LED_R, 0);
	}
}

static void led_update_color(void)
{
	enum power_state state = charge_get_state();

	if (!led_auto_control)
		return;

	/* check ac. no ac -> off */
	if (!extpower_is_present()) {
		led_set_color(0, 0, 0);
		return;
	}

	switch (state) {
	case PWR_STATE_CHARGE:
		led_set_color(0, 0, BRIGHTNESS_YELLOW);
		break;
	case PWR_STATE_IDLE:
	case PWR_STATE_CHARGE_NEAR_FULL:
		led_set_color(0, BRIGHTNESS_GREEN, 0);
		break;
	case PWR_STATE_ERROR:
		led_set_color(BRIGHTNESS_RED, 0, 0);
		break;
	case PWR_STATE_INIT:
	case PWR_STATE_UNCHANGE:
	case PWR_STATE_IDLE0:
	case PWR_STATE_REINIT:
	case PWR_STATE_DISCHARGE:
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, led_update_color, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, led_update_color, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHARGE_STATE_CHANGE, led_update_color, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Host commands */

static int led_command_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_led_control *p = args->params;
	struct ec_response_led_control *r = args->response;
	int i;
	uint8_t clipped[EC_LED_COLOR_COUNT];

	/* Only support battery LED control */
	if (p->led_id != EC_LED_ID_BATTERY_LED)
		return EC_RES_INVALID_PARAM;

	if (p->flags & EC_LED_FLAGS_AUTO) {
		led_auto_control = 1;
		led_update_color();
	} else if (!(p->flags & EC_LED_FLAGS_QUERY)) {
		for (i = 0; i < EC_LED_COLOR_COUNT; ++i)
			clipped[i] = MIN(p->brightness[i], 100);
		led_auto_control = 0;
		led_set_color(clipped[EC_LED_COLOR_RED],
			      clipped[EC_LED_COLOR_GREEN],
			      clipped[EC_LED_COLOR_YELLOW]);
	}

	r->brightness_range[EC_LED_COLOR_RED] = 100;
	r->brightness_range[EC_LED_COLOR_GREEN] = 100;
	r->brightness_range[EC_LED_COLOR_BLUE] = 0;
	r->brightness_range[EC_LED_COLOR_YELLOW] = 100;
	r->brightness_range[EC_LED_COLOR_WHITE] = 0;
	args->response_size = sizeof(struct ec_response_led_control);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LED_CONTROL,
		     led_command_control,
		     EC_VER_MASK(1));

/*****************************************************************************/
/* Console commands */

static int command_led(int argc, char **argv)
{
	char *e;
	uint8_t brightness;

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	brightness = strtoi(argv[2], &e, 0);
	if ((e && *e) || brightness < 0 || brightness > 100)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[1], "r"))
		led_set_color(brightness, 0, 0);
	else if (!strcasecmp(argv[1], "g"))
		led_set_color(0, brightness, 0);
	else if (!strcasecmp(argv[1], "y"))
		led_set_color(0, 0, brightness);
	else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led,
			"<r | g | y> <brightness>",
			"Set the color and brightness of the LED",
			NULL);
