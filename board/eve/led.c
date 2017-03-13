/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power/Battery LED control for Eve
 */

#include "charge_manager.h"
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
	EC_LED_ID_LEFT_LED, EC_LED_ID_RIGHT_LED};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_BLUE,
	LED_WHITE,
	LED_AMBER,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

enum led_side {
	LED_LEFT = 0,
	LED_RIGHT,
	LED_BOTH
};

/* Brightness vs. color, in the order of off, red, green and blue */
#define PWM_CHAN_PER_LED 3
static const uint8_t color_brightness[LED_COLOR_COUNT][PWM_CHAN_PER_LED] = {
	/* {Red, Green, Blue}, */
	[LED_OFF]   = {100, 100, 100},
	[LED_RED]   = {20,  100, 100},
	[LED_GREEN] = {100, 20, 100},
	[LED_BLUE]  = {100, 100, 20},
	[LED_WHITE]  = {0, 0, 0},
	[LED_AMBER]  = {0, 87, 100},
};

/**
 * Set LED color
 *
 * @param color	Enumerated color value
 * @param side		Left LED, Right LED, or both LEDs
 */
static void set_color(enum led_color color, enum led_side side)
{
	int i;
	static uint8_t saved_duty[LED_BOTH][PWM_CHAN_PER_LED];

	/* Set color for left LED */
	if (side == LED_LEFT || side == LED_BOTH) {
		for (i = 0; i < PWM_CHAN_PER_LED; i++) {
			if (saved_duty[LED_LEFT][i] !=
			    color_brightness[color][i]) {
				pwm_set_duty(PWM_CH_LED_L_RED + i,
					     color_brightness[color][i]);
				saved_duty[LED_LEFT][i] =
					color_brightness[color][i];
			}
		}
	}

	/* Set color for right LED */
	if (side == LED_RIGHT || side == LED_BOTH) {
		for (i = 0; i < PWM_CHAN_PER_LED; i++) {
			if (saved_duty[LED_RIGHT][i] !=
			    color_brightness[color][i]) {
				pwm_set_duty(PWM_CH_LED_R_RED + i,
					     color_brightness[color][i]);
				saved_duty[LED_RIGHT][i] =
					color_brightness[color][i];
			}
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		/* Set brightness for left LED */
		pwm_set_duty(PWM_CH_LED_L_RED,
			     100 - brightness[EC_LED_COLOR_RED]);
		pwm_set_duty(PWM_CH_LED_L_BLUE,
			     100 - brightness[EC_LED_COLOR_BLUE]);
		pwm_set_duty(PWM_CH_LED_L_GREEN,
			     100 - brightness[EC_LED_COLOR_GREEN]);
		break;
	case EC_LED_ID_RIGHT_LED:
		/* Set brightness for right LED */
		pwm_set_duty(PWM_CH_LED_R_RED,
			     100 - brightness[EC_LED_COLOR_RED]);
		pwm_set_duty(PWM_CH_LED_R_BLUE,
			     100 - brightness[EC_LED_COLOR_BLUE]);
		pwm_set_duty(PWM_CH_LED_R_GREEN,
			     100 - brightness[EC_LED_COLOR_GREEN]);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static void eve_led_set_power_battery(void)
{
	static int power_ticks;
	enum charge_state chg_state = charge_get_state();
	int side;

	/* Get active charge port which maps directly to left/right LED */
	side = charge_manager_get_active_charge_port();
	/* Ensure that side can be safely used as an index */
	if (side < 0 || side >= CONFIG_USB_PD_PORT_COUNT)
		side = LED_BOTH;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		enum led_side blueside = LED_BOTH;

		if (chg_state == PWR_STATE_CHARGE) {
			set_color(LED_AMBER, side);
			blueside = !side;
		} else if (chg_state == PWR_STATE_IDLE || chg_state ==
			   PWR_STATE_CHARGE_NEAR_FULL) {
			set_color(LED_GREEN, side);
			blueside = !side;
		} else if (chg_state == PWR_STATE_DISCHARGE_FULL &&
			   extpower_is_present()) {
			set_color(LED_GREEN, side);
			blueside = !side;
		}
		set_color(LED_BLUE, blueside);
		return;
	}

	/* Flash red on critical battery, which usually inhibits AP power-on. */
	if (battery_is_present() != BP_YES ||
	    charge_get_percent() < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		set_color(((power_ticks++ % LED_TOTAL_TICKS) < LED_ON_TICKS) ?
			  LED_RED : LED_OFF, LED_BOTH);
		return;
	}

	/* Suspend or Standby state */
	if (chipset_in_state(CHIPSET_STATE_SUSPEND) ||
	    chipset_in_state(CHIPSET_STATE_STANDBY)) {
		enum led_side blinkside = LED_BOTH;

		if (chg_state == PWR_STATE_CHARGE_NEAR_FULL ||
		    chg_state == PWR_STATE_IDLE) {
			set_color(LED_GREEN, side);
			blinkside = !side;
		} else if (chg_state == PWR_STATE_CHARGE) {
			set_color(LED_AMBER, side);
			blinkside = !side;
		} else if (chg_state == PWR_STATE_DISCHARGE_FULL &&
			   extpower_is_present()) {
			set_color(LED_GREEN, side);
			blinkside = !side;
		}
		if (chg_state == PWR_STATE_DISCHARGE ||
		    chg_state == PWR_STATE_DISCHARGE_FULL ||
		    chg_state == PWR_STATE_CHARGE ||
		    chg_state == PWR_STATE_CHARGE_NEAR_FULL ||
		    chg_state == PWR_STATE_IDLE) {
			/*
			 * If in S3/S0iX and not in some error
			 * state, then flash non-charging LEDs white.
			 */
			set_color(((power_ticks++ % LED_TOTAL_TICKS) <
				   LED_ON_TICKS) ?
				  LED_WHITE : LED_OFF, blinkside);
			return;
		}
	}

	/* CHIPSET_STATE_OFF */
	switch (chg_state) {
	case PWR_STATE_DISCHARGE_FULL:
		set_color(LED_OFF, LED_BOTH);
		if (extpower_is_present())
			set_color(LED_GREEN, side);
		break;
	case PWR_STATE_DISCHARGE:
		set_color(LED_OFF, LED_BOTH);
		break;
	case PWR_STATE_CHARGE:
		set_color(LED_OFF, LED_BOTH);
		set_color(LED_AMBER, side);
		break;
	case PWR_STATE_ERROR:
		set_color(((power_ticks++ % LED_TOTAL_TICKS)
			   < LED_ON_TICKS) ? LED_RED : LED_GREEN, LED_BOTH);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		set_color(LED_OFF, LED_BOTH);
		set_color(LED_GREEN, side);
		break;
	default:
		set_color(LED_RED, LED_BOTH);
		break;
	}
	if (chg_state != PWR_STATE_ERROR)
		power_ticks = 0;
}

static void led_init(void)
{
	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled,
	 * seems to ground the pins instead of letting them float.
	 */
	/* Initialize PWM channels for left LED */
	pwm_enable(PWM_CH_LED_L_RED, 1);
	pwm_enable(PWM_CH_LED_L_GREEN, 1);
	pwm_enable(PWM_CH_LED_L_BLUE, 1);

	/* Initialize PWM channels for right LED */
	pwm_enable(PWM_CH_LED_R_RED, 1);
	pwm_enable(PWM_CH_LED_R_GREEN, 1);
	pwm_enable(PWM_CH_LED_R_BLUE, 1);

	set_color(LED_OFF, LED_BOTH);
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
		eve_led_set_power_battery();
		return;
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

/******************************************************************/
/* Console commands */
static int command_led(int argc, char **argv)
{
	int side = LED_BOTH;
	char *e;

	if (argc > 1) {
		if (argc > 2) {
			side = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
			if (side > 1)
				return EC_ERROR_PARAM2;
		}

		if (!strcasecmp(argv[1], "debug")) {
			led_debug ^= 1;
			CPRINTF("led_debug = %d\n", led_debug);
		} else if (!strcasecmp(argv[1], "off")) {
			set_color(LED_OFF, side);
		} else if (!strcasecmp(argv[1], "red")) {
			set_color(LED_RED, side);
		} else if (!strcasecmp(argv[1], "green")) {
			set_color(LED_GREEN, side);
		} else if (!strcasecmp(argv[1], "blue")) {
			set_color(LED_BLUE, side);
		} else if (!strcasecmp(argv[1], "white")) {
			set_color(LED_WHITE, side);
		} else if (!strcasecmp(argv[1], "amber")) {
			set_color(LED_AMBER, side);
		} else {
			/* maybe handle charger_discharge_on_ac() too? */
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led,
			"[debug|red|green|blue|white|amber|off <0|1>]",
			"Change LED color");
