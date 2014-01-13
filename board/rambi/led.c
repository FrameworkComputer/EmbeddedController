/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Rambi
 */

#include "charge_state.h"
#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {EC_LED_ID_BATTERY_LED};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_YELLOW,
	LED_GREEN,
	LED_DIM_GREEN,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

/* Brightness vs. color, for {red, green} LEDs */
static const uint8_t color_brightness[LED_COLOR_COUNT][2] = {
	{0, 0},
	{100, 0},
	{40, 80},
	{0, 100},
	{0, 10},
};

/**
 * Set LED color
 *
 * @param color		Enumerated color value
 */
static void set_color(enum led_color color)
{
	pwm_set_duty(PWM_CH_LED_RED, color_brightness[color][0]);
	pwm_set_duty(PWM_CH_LED_GREEN, color_brightness[color][1]);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	pwm_set_duty(PWM_CH_LED_RED, brightness[EC_LED_COLOR_RED]);
	pwm_set_duty(PWM_CH_LED_GREEN, brightness[EC_LED_COLOR_GREEN]);
	return EC_SUCCESS;
}

static void led_init(void)
{
	/* Configure GPIOs */
	gpio_config_module(MODULE_PWM_LED, 1);

	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled, the LM4
	 * seems to ground the pins instead of letting them float.
	 */
	pwm_enable(PWM_CH_LED_RED, 1);
	pwm_enable(PWM_CH_LED_GREEN, 1);
	set_color(LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/**
 * Called by hook task every 250 ms
 */
static void led_tick(void)
{
	static int suspended_prev;
	static unsigned ticks;
	int blink_on;

	int suspended = chipset_in_state(CHIPSET_STATE_SUSPEND);
	int chstate = charge_get_state();

	/* If we don't control the LED, nothing to do */
	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		return;

	/* If we're just suspending now, reset ticks so LED changes quickly */
	if (suspended && !suspended_prev)
		ticks = 0;
	else
		ticks++;

	suspended_prev = suspended;

	/* Blink with 25% duty cycle, 4 sec period */
	blink_on = ticks % 16 < 4;

	/* If charging error, blink red */
	if (chstate == PWR_STATE_ERROR) {
		set_color(blink_on ? LED_RED : LED_OFF);
		return;
	}

	/* If charge-force-idle, blink green, 50% duty cycle, 2 sec period */
	if (chstate == PWR_STATE_IDLE &&
	    (charge_get_flags() & CHARGE_FLAG_FORCE_IDLE)) {
		set_color((ticks & 0x4) ? LED_GREEN : LED_OFF);
		return;
	}

	/*
	 * If the system is charging, solid yellow.
	 *
	 * Note that this means you can't distinguish power states
	 * (on/suspend/off) when the system is charging.
	 */
	if (chstate == PWR_STATE_CHARGE) {
		set_color(LED_YELLOW);
		return;
	}

	/* If suspended, blink yellow */
	if (suspended) {
		set_color(blink_on ? LED_YELLOW : LED_OFF);
		return;
	}

	/* If AC connected and fully charged (or close to it), solid green */
	if (chstate == PWR_STATE_CHARGE_NEAR_FULL ||
	    chstate == PWR_STATE_IDLE) {
		set_color(LED_GREEN);
		return;
	}

	/* If powered on, dim green (just as an indicator we're on) */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		set_color(LED_DIM_GREEN);
		return;
	}

	/* Otherwise, system is off and AC not connected, LED off */
	set_color(LED_OFF);
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
