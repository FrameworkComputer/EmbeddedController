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
#include "lid_switch.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED, EC_LED_ID_POWER_LED};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_ORANGE,
	LED_GREEN,
};

/**
 * Set battery LED color
 *
 * @param color		Enumerated color value
 */
static void set_battery_led_color(enum led_color color)
{
	pwm_set_duty(PWM_CH_LED_BATTERY_ORANGE, color == LED_ORANGE ? 100 : 0);
	pwm_set_duty(PWM_CH_LED_BATTERY_GREEN, color == LED_GREEN ? 100 : 0);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_GREEN] = 100;
	} else {
		brightness_range[EC_LED_COLOR_RED] = 100;
		brightness_range[EC_LED_COLOR_GREEN] = 100;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_POWER_LED) {
		pwm_set_duty(PWM_CH_LED_POWER_GREEN,
			     brightness[EC_LED_COLOR_GREEN]);
	} else {
		pwm_set_duty(PWM_CH_LED_BATTERY_ORANGE,
			     brightness[EC_LED_COLOR_RED]);
		pwm_set_duty(PWM_CH_LED_BATTERY_GREEN,
			     brightness[EC_LED_COLOR_GREEN]);
	}
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
	pwm_enable(PWM_CH_LED_BATTERY_ORANGE, 1);
	pwm_enable(PWM_CH_LED_BATTERY_GREEN, 1);
	pwm_enable(PWM_CH_LED_POWER_GREEN, 1);
	pwm_set_duty(PWM_CH_LED_POWER_GREEN, 0);
	set_battery_led_color(LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/**
 * Return new duty cycle for power LED (0-100).
 */
static int new_power_led_brightness(void)
{
	static unsigned ticks;
	static int suspended_prev;

	int suspended = chipset_in_state(CHIPSET_STATE_SUSPEND);

	/* If we're just suspending now, reset ticks so LED changes quickly */
	if (suspended && !suspended_prev)
		ticks = 0;
	else
		ticks++;

	suspended_prev = suspended;

	/* If lid is closed, LED is off in all chipset states */
	if (!lid_is_open())
		return 0;

	/* If chipset is on, LED is on */
	if (chipset_in_state(CHIPSET_STATE_ON))
		return 100;

	/* If chipset isn't on or suspended, it's off; LED is off */
	if (!chipset_in_state(CHIPSET_STATE_SUSPEND))
		return 0;

	/* Suspended.  Blink with 25% duty cycle, 2 sec period */
	return (ticks % 8 < 2) ? 100 : 0;
}

/**
 * Return new color for battery LED.
 */
static enum led_color new_battery_led_color(void)
{
	static unsigned ticks;

	int chstate = charge_get_state();

	ticks++;

	/* If charging error, blink orange, 50% duty cycle, 0.5 sec period */
	if (chstate == PWR_STATE_ERROR)
		return (ticks & 0x1) ? LED_ORANGE : LED_OFF;

	/* If charge-force-idle, blink green, 50% duty cycle, 2 sec period */
	if (chstate == PWR_STATE_IDLE &&
	    (charge_get_flags() & CHARGE_FLAG_FORCE_IDLE))
		return (ticks & 0x4) ? LED_GREEN : LED_OFF;

	/* If the system is charging, orange under 95%; green if over */
	if (chstate == PWR_STATE_CHARGE)
		return charge_get_percent() < 95 ? LED_ORANGE : LED_GREEN;

	/* If AC connected and fully charged (or close to it), solid green */
	if (chstate == PWR_STATE_CHARGE_NEAR_FULL ||
	    chstate == PWR_STATE_IDLE) {
		return LED_GREEN;
	}

	/* Otherwise, discharging; flash orange if less than 10% power */
	if (charge_get_percent() < 10)
		return (ticks % 8 < 2) ? LED_ORANGE : LED_OFF;

	/* Discharging and greater than 10% power, so off */
	return LED_OFF;
}

/**
 * Called by hook task every 250 ms
 */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		pwm_set_duty(PWM_CH_LED_POWER_GREEN,
			     new_power_led_brightness());

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		set_battery_led_color(new_battery_led_color());
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
