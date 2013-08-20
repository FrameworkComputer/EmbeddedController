/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Kirby LED driver.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "pwm.h"
#include "util.h"

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
