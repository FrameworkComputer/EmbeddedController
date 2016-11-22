/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Pyro
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "pwm.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_PWM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

#define CRITICAL_LOW_BATTERY_PERCENTAGE 3
#define LOW_BATTERY_PERCENTAGE 10

#define LED_TOTAL_TICKS 2
#define LED_ON_TICKS 1

#define TICKS_STEP1_BRIGHTER 0
#define TICKS_STEP2_DIMMER 20
#define TICKS_STEP3_OFF 40

#define FULL_BATTERY_PERMILLAGE 875

static int led_debug;
static int ticks;

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED, EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_AMBER,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int led_set_color_battery(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_OFF);
		break;
	case LED_GREEN:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

/* Brightness vs. color, in the order of off, red */
static const uint8_t color_brightness[2] = {
	[LED_OFF]   = 0,
	[LED_RED]   = 100,
};

static void led_set_color_power(enum led_color color)
{
	pwm_set_duty(PWM_CH_LED_RED, color_brightness[color]);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		if (brightness[EC_LED_COLOR_GREEN] != 0)
			led_set_color_battery(LED_GREEN);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LED_AMBER);
		else
			led_set_color_battery(LED_OFF);
		break;
	case EC_LED_ID_POWER_LED:
		if (brightness[EC_LED_COLOR_RED] != 0)
			pwm_set_duty(PWM_CH_LED_RED, color_brightness[LED_RED]);
		else
			pwm_set_duty(PWM_CH_LED_RED, color_brightness[LED_OFF]);
		break;
	default:
		break;
	}
	return EC_SUCCESS;
}

static void led_set_battery(void)
{
	static int battery_ticks;
	int remaining_capacity;
	int full_charge_capacity;
	int permillage;

	battery_ticks++;

	remaining_capacity = *(int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	full_charge_capacity = *(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	permillage = !full_charge_capacity ? 0 :
		(1000 * remaining_capacity) / full_charge_capacity;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		led_set_color_battery(permillage <
			FULL_BATTERY_PERMILLAGE ? LED_AMBER : LED_GREEN);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		led_set_color_battery(LED_GREEN);
		break;
	default:
		/* Other states don't alter LED behavior */
		led_set_color_battery(LED_OFF);
		break;
	}
}

static void suspend_led_update_deferred(void);
DECLARE_DEFERRED(suspend_led_update_deferred);

static void suspend_led_update_deferred(void)
{
	int delay = 50 * MSEC;

	ticks++;

	/* 1s gradual on, 1s gradual off, 3s off */
	if (ticks <= TICKS_STEP2_DIMMER) {
		pwm_set_duty(PWM_CH_LED_RED, ticks*5);
	} else if (ticks <= TICKS_STEP3_OFF) {
		pwm_set_duty(PWM_CH_LED_RED, (TICKS_STEP3_OFF - ticks)*5);
	} else {
		ticks = TICKS_STEP1_BRIGHTER;
		delay = 3000 * MSEC;
	}

	hook_call_deferred(&suspend_led_update_deferred_data, delay);
}

static void suspend_led_init(void)
{
	ticks = TICKS_STEP2_DIMMER;

	hook_call_deferred(&suspend_led_update_deferred_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, suspend_led_init, HOOK_PRIO_DEFAULT);

static void suspend_led_deinit(void)
{
	hook_call_deferred(&suspend_led_update_deferred_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, suspend_led_deinit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, suspend_led_deinit, HOOK_PRIO_DEFAULT);

static void led_set_power(void)
{
	static int power_ticks;
	static int previous_state_suspend;
	static int blink_ticks;

	power_ticks++;

	/* Blink 3 times (0.25s on/0.25s off, repeat 3 times) */
	if (extpower_is_present()) {
		blink_ticks++;
		if (!previous_state_suspend)
			power_ticks = 0;

		while (blink_ticks < 7) {
			led_set_color_power(
				(power_ticks % LED_TOTAL_TICKS) < LED_ON_TICKS ?
				LED_RED : LED_OFF);

			previous_state_suspend = 1;
			return;
		}
	}
	if (!extpower_is_present())
		blink_ticks = 0;

	previous_state_suspend = 0;

	if (chipset_in_state(CHIPSET_STATE_SOFT_OFF))
		led_set_color_power(LED_OFF);
	if (chipset_in_state(CHIPSET_STATE_ON))
		led_set_color_power(LED_RED);
}

static void led_init(void)
{
	/* Configure GPIOs */
	gpio_config_module(MODULE_PWM, 1);

	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled,
	 * seems to ground the pins instead of letting them float.
	 */
	pwm_enable(PWM_CH_LED_RED, 1);

	led_set_color_power(LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every 25 ms */
static void led_tick(void)
{
	if (led_debug)
		return;

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();
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
			led_set_color_power(LED_OFF);
			led_set_color_battery(LED_OFF);
		} else if (!strcasecmp(argv[1], "red")) {
			led_set_color_power(LED_RED);
		} else if (!strcasecmp(argv[1], "green")) {
			led_set_color_battery(LED_GREEN);
		} else if (!strcasecmp(argv[1], "amber")) {
			led_set_color_battery(LED_AMBER);
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ledcolor, command_led_color,
			"[debug|red|green|amber|off]",
			"Change LED color");
