/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Fizz
 */

#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {EC_LED_ID_POWER_LED};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_AMBER,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

static int set_color_power(enum led_color color, int duty)
{
	int green = 0;
	int red = 0;

	if (duty < 0 || 100 < duty)
		return EC_ERROR_UNKNOWN;

	switch (color) {
	case LED_OFF:
		break;
	case LED_GREEN:
		green = 1;
		break;
	case LED_RED:
		red = 1;
		break;
	case LED_AMBER:
		green = 1;
		red = 1;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	if (red)
		pwm_set_duty(PWM_CH_LED_RED, duty);
	else
		pwm_set_duty(PWM_CH_LED_RED, 0);

	if (green)
		pwm_set_duty(PWM_CH_LED_GREEN, duty);
	else
		pwm_set_duty(PWM_CH_LED_GREEN, 0);

	return EC_SUCCESS;
}

static int set_color(enum ec_led_id id, enum led_color color, int duty)
{
	switch (id) {
	case EC_LED_ID_POWER_LED:
		return set_color_power(color, duty);
	default:
		return EC_ERROR_UNKNOWN;
	}
}

/* led task increments brightness by <duty_inc> every <task_frequency_us> to go
 * from 0% to 100% in <LED_PULSE_US>. Then it decreases brightness likewise in
 * <LED_PULSE_US>. So, total time for one cycle is <LED_PULSE_US> x 2. */
#define LED_PULSE_US	(2 * SECOND)
static uint32_t task_frequency_us;
static int duty_inc;

static void set_task_frequency(uint32_t usec)
{
	task_frequency_us = usec;
	duty_inc = 100 / (LED_PULSE_US / task_frequency_us);
}

static void led_set_power(void)
{
	static int duty = 0;

	if (chipset_in_state(CHIPSET_STATE_ON))
		set_color(EC_LED_ID_POWER_LED, LED_GREEN, 100);
	else if (chipset_in_state(
			CHIPSET_STATE_SUSPEND | CHIPSET_STATE_STANDBY))
		set_color(EC_LED_ID_POWER_LED, LED_AMBER, duty);
	else
		set_color(EC_LED_ID_POWER_LED, LED_OFF, 0);

	if (duty + duty_inc > 100)
		duty_inc = duty_inc * -1;
	else if (duty + duty_inc < 0)
		duty_inc = duty_inc * -1;
	duty += duty_inc;
}

void led_task(void *u)
{
	uint32_t interval;
	uint32_t start;

	while (1) {
		start = get_time().le.lo;
		if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
			led_set_power();
		interval = get_time().le.lo - start;
		if (task_frequency_us > interval)
			usleep(task_frequency_us - interval);
	}
}

static void led_init(void)
{
	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled,
	 * seems to ground the pins instead of letting them float.
	 */
	pwm_enable(PWM_CH_LED_RED, 1);
	pwm_enable(PWM_CH_LED_GREEN, 1);

	/* 40 msec for nice and smooth transition */
	set_task_frequency(40 * MSEC);

	/* From users' perspective, system-on means AP-on */
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_OFF, 0);
}
/* After pwm_pin_init() */
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

static int command_led(int argc, char **argv)
{
	enum ec_led_id id = EC_LED_ID_POWER_LED;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "debug")) {
		led_auto_control(id, !led_auto_control_is_enabled(id));
		ccprintf("o%s\n", led_auto_control_is_enabled(id) ? "ff" : "n");
	} else if (!strcasecmp(argv[1], "off")) {
		set_color(id, LED_OFF, 0);
	} else if (!strcasecmp(argv[1], "red")) {
		set_color(id, LED_RED, 100);
	} else if (!strcasecmp(argv[1], "green")) {
		set_color(id, LED_GREEN, 100);
	} else if (!strcasecmp(argv[1], "amber")) {
		set_color(id, LED_AMBER, 100);
	} else {
		char *e;
		uint32_t msec = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		set_task_frequency(msec * MSEC);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led,
			"[debug|red|green|amber|off|num]",
			"Turn on/off LED. If a number is given, it changes led"
			"task frequency (msec).");

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
}

int led_set_brightness(enum ec_led_id id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_RED])
		return set_color(id, LED_RED, brightness[EC_LED_COLOR_RED]);
	else if (brightness[EC_LED_COLOR_GREEN])
		return set_color(id, LED_GREEN, brightness[EC_LED_COLOR_GREEN]);
	else if (brightness[EC_LED_COLOR_AMBER])
		return set_color(id, LED_AMBER, brightness[EC_LED_COLOR_AMBER]);
	else
		return set_color(id, LED_OFF, 0);
}
