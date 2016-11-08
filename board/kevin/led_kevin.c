/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power/Battery LED control for Kevin
 */

#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "registers.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_PWM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

#define LED_TOTAL_TICKS 16
#define LED_ON_TICKS 8

static int led_debug;

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED, EC_LED_ID_BATTERY_LED};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_BLUE,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

/* Brightness vs. color, in the order of off, red, green and blue */
static const uint8_t color_brightness[LED_COLOR_COUNT][3] = {
	/* {Red, Blue, Green}, */
	[LED_OFF]   = {100, 100, 100},
	[LED_RED]   = {90,  100, 100},
	[LED_GREEN] = {100, 100, 80},
	[LED_BLUE]  = {100, 80, 100},
};

/**
 * Set LED color
 *
 * @param color		Enumerated color value
 */
static void set_color(enum led_color color)
{
	pwm_set_duty(PWM_CH_LED_RED, color_brightness[color][0]);
	pwm_set_duty(PWM_CH_LED_BLUE, color_brightness[color][1]);
	pwm_set_duty(PWM_CH_LED_GREEN, color_brightness[color][2]);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	pwm_set_duty(PWM_CH_LED_RED, brightness[EC_LED_COLOR_RED]);
	pwm_set_duty(PWM_CH_LED_BLUE, brightness[EC_LED_COLOR_BLUE]);
	pwm_set_duty(PWM_CH_LED_GREEN, brightness[EC_LED_COLOR_GREEN]);
	return EC_SUCCESS;
}

static void kevin_led_set_power_battery(void)
{
	static int power_ticks;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		set_color(LED_BLUE);
		return;
	}

	/* CHIPSET_STATE_OFF */
	switch (charge_get_state()) {
	case PWR_STATE_DISCHARGE:
		if ((charge_get_flags() & CHARGE_FLAG_EXTERNAL_POWER) &&
		     charge_get_percent() >= BATTERY_LEVEL_NEAR_FULL)
			set_color(LED_GREEN);
		else
			set_color(LED_OFF);
		break;
	case PWR_STATE_CHARGE:
		set_color(LED_RED);
		break;
	case PWR_STATE_ERROR:
		power_ticks++;
		set_color(((power_ticks % LED_TOTAL_TICKS)
			  < LED_ON_TICKS) ? LED_RED : LED_GREEN);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		set_color(LED_GREEN);
		break;
	default:
		set_color(LED_RED);
		break;
	}
	if ((charge_get_state()) != PWR_STATE_ERROR)
		power_ticks = 0;
}

static void led_init(void)
{
	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled,
	 * seems to ground the pins instead of letting them float.
	 */
	pwm_enable(PWM_CH_LED_RED, 1);
	pwm_enable(PWM_CH_LED_GREEN, 1);
	pwm_enable(PWM_CH_LED_BLUE, 1);

	set_color(LED_OFF);
}
/* After pwm_pin_init() */
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/**
 * Called by hook task every 250 ms
 */
static void led_tick(void)
{
	if (led_debug)
		return;

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED) &&
	    led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		kevin_led_set_power_battery();
		return;
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

/******************************************************************/
/* Console commands */
static int command_led_color(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp(argv[1], "debug")) {
			led_debug ^= 1;
			CPRINTF("led_debug = %d\n", led_debug);
		} else if (!strcasecmp(argv[1], "off")) {
			set_color(LED_OFF);
		} else if (!strcasecmp(argv[1], "red")) {
			set_color(LED_RED);
		} else if (!strcasecmp(argv[1], "green")) {
			set_color(LED_GREEN);
		} else if (!strcasecmp(argv[1], "blue")) {
			set_color(LED_BLUE);
		} else {
			/* maybe handle charger_discharge_on_ac() too? */
			return EC_ERROR_PARAM1;
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ledcolor, command_led_color,
			"[debug|red|green|blue|off]",
			"Change LED color");
