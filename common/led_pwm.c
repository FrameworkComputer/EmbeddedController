/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM LED control to conform to Chrome OS LED behaviour specification. */

/*
 * This assumes that a single logical LED is shared between both power and
 * charging/battery status.  If multiple logical LEDs are present, they all
 * follow the same patterns.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "led_common.h"
#include "led_pwm.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

/* Battery percentage thresholds to blink at different rates. */
#define CRITICAL_LOW_BATTERY_PERCENTAGE 3
#define LOW_BATTERY_PERCENTAGE 10

#define PULSE_TICK (250 * MSEC)

void set_pwm_led_color(enum pwm_led_id id, int color)
{
	struct pwm_led duty = { 0 };

	if ((id > CONFIG_LED_PWM_COUNT) || (id < 0) ||
	    (color > EC_LED_COLOR_COUNT) || (color < -1))
		return;

	if (color != -1) {
		duty.ch0 = led_color_map[color].ch0;
		duty.ch1 = led_color_map[color].ch1;
		duty.ch2 = led_color_map[color].ch2;
	}

	if (pwm_leds[id].ch0 != PWM_LED_NO_CHANNEL)
		pwm_set_duty(pwm_leds[id].ch0, duty.ch0);
	if (pwm_leds[id].ch1 != PWM_LED_NO_CHANNEL)
		pwm_set_duty(pwm_leds[id].ch1, duty.ch1);
	if (pwm_leds[id].ch2 != PWM_LED_NO_CHANNEL)
		pwm_set_duty(pwm_leds[id].ch2, duty.ch2);
}

static void set_led_color(int color)
{
	/*
	 *  We must check if auto control is enabled since the LEDs may be
	 *  controlled from the AP at anytime.
	 */
	if ((led_auto_control_is_enabled(EC_LED_ID_POWER_LED)) ||
	    (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)))
		set_pwm_led_color(PWM_LED0, color);

#if CONFIG_LED_PWM_COUNT >= 2
	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		set_pwm_led_color(PWM_LED1, color);
#endif /* CONFIG_LED_PWM_COUNT >= 2 */
}

static uint8_t led_is_pulsing;
static uint8_t pulse_period;
static uint8_t pulse_ontime;
static enum ec_led_colors pulse_color;
static void pulse_leds_deferred(void);
DECLARE_DEFERRED(pulse_leds_deferred);
static void pulse_leds_deferred(void)
{
	static uint8_t tick_count;

	if (!led_is_pulsing) {
		tick_count = 0;
		return;
	}

	if (tick_count < pulse_ontime)
		set_led_color(pulse_color);
	else
		set_led_color(-1);

	tick_count = (tick_count + 1) % pulse_period;
	hook_call_deferred(&pulse_leds_deferred_data, PULSE_TICK);
}

static void pulse_leds(enum ec_led_colors color, int ontime, int period)
{
	pulse_color = color;
	pulse_ontime = ontime;
	pulse_period = period;
	led_is_pulsing = 1;
	pulse_leds_deferred();
}

static void update_leds(void)
{
	enum charge_state chg_st = charge_get_state();
	int batt_percentage = charge_get_percent();

	/*
	 * Reflecting the charge state is the highest priority.
	 *
	 * The colors listed below are the default, but can be overridden.
	 *
	 * Solid Amber == Charging
	 * Solid Green == Charging (near full)
	 * Fast Flash Red == Charging error or battery not present
	 * Slow Flash Amber == Low Battery
	 * Fast Flash Amber == Critical Battery
	 */
	if (chg_st == PWR_STATE_CHARGE) {
		led_is_pulsing = 0;
		set_led_color(CONFIG_LED_PWM_CHARGE_COLOR);
	} else if (chg_st == PWR_STATE_CHARGE_NEAR_FULL) {
		led_is_pulsing = 0;
		set_led_color(CONFIG_LED_PWM_NEAR_FULL_COLOR);
	} else if ((battery_is_present() != BP_YES) ||
		   (chg_st == PWR_STATE_ERROR)) {
		/* 500 ms period, 50% duty cycle. */
		pulse_leds(CONFIG_LED_PWM_CHARGE_ERROR_COLOR, 1, 2);
	} else if (batt_percentage < CRITICAL_LOW_BATTERY_PERCENTAGE) {
		/* Flash amber faster (1 second period, 50% duty cycle) */
		pulse_leds(CONFIG_LED_PWM_LOW_BATT_COLOR, 2, 4);
	} else if (batt_percentage < LOW_BATTERY_PERCENTAGE) {
		/* Flash amber (4 second period, 50% duty cycle) */
		pulse_leds(CONFIG_LED_PWM_LOW_BATT_COLOR, 8, 16);
	} else {
		/* Discharging or not charging. Reflect the SoC state. */
		led_is_pulsing = 0;
		if (chipset_in_state(CHIPSET_STATE_ON)) {
			/* The LED must be on in the Active state. */
			set_led_color(CONFIG_LED_PWM_SOC_ON_COLOR);
		} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
			/* The power LED must pulse in the suspend state. */
			pulse_leds(CONFIG_LED_PWM_SOC_SUSPEND_COLOR, 4, 16);
		} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/* The LED must be off in the Deep Sleep state. */
			set_led_color(-1);
		}
	}
}
DECLARE_HOOK(HOOK_TICK, update_leds, HOOK_PRIO_DEFAULT);

static void init_leds_off(void)
{
	/* Turn off LEDs such that they are in a known state. */
	set_led_color(-1);
}
DECLARE_HOOK(HOOK_INIT, init_leds_off, HOOK_PRIO_INIT_PWM + 1);

#ifdef CONFIG_CMD_LEDTEST
int command_ledtest(int argc, char **argv)
{
	int enable;
	int pwm_led_id;
	int led_id;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	pwm_led_id = atoi(argv[1]);
	if ((pwm_led_id < 0) || (pwm_led_id >= CONFIG_LED_PWM_COUNT))
		return EC_ERROR_PARAM1;
	led_id = supported_led_ids[pwm_led_id];

	if (argc == 2) {
		ccprintf("PWM LED %d: led_id=%d, auto_control=%d\n",
			 pwm_led_id, led_id,
			 led_auto_control_is_enabled(led_id) != 0);
		return EC_SUCCESS;
	}
	if (!parse_bool(argv[2], &enable))
		return EC_ERROR_PARAM2;

	/* Inverted because this drives auto control. */
	led_auto_control(led_id, !enable);

	if (argc == 4) {
		/* Set the color. */
		if (!strncmp(argv[3], "red", 3))
			set_pwm_led_color(pwm_led_id, EC_LED_COLOR_RED);
		else if (!strncmp(argv[3], "green", 5))
			set_pwm_led_color(pwm_led_id, EC_LED_COLOR_GREEN);
		else if (!strncmp(argv[3], "amber", 5))
			set_pwm_led_color(pwm_led_id, EC_LED_COLOR_AMBER);
		else if (!strncmp(argv[3], "blue", 4))
			set_pwm_led_color(pwm_led_id, EC_LED_COLOR_BLUE);
		else if (!strncmp(argv[3], "white", 5))
			set_pwm_led_color(pwm_led_id, EC_LED_COLOR_WHITE);
		else if (!strncmp(argv[3], "yellow", 6))
			set_pwm_led_color(pwm_led_id, EC_LED_COLOR_YELLOW);
		else if (!strncmp(argv[3], "off", 3))
			set_pwm_led_color(pwm_led_id, -1);
		else
			return EC_ERROR_PARAM3;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ledtest, command_ledtest,
			"<pwm led idx> <enable|disable> [color|off]", "");
#endif /* defined(CONFIG_CMD_LEDTEST) */
