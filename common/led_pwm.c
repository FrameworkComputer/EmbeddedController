/* Copyright 2018 The ChromiumOS Authors
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
#include "charge_manager.h"
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

#ifndef CONFIG_LED_PWM_TASK_DISABLED
static uint8_t led_is_pulsing;
#endif /* CONFIG_LED_PWM_TASK_DISABLED */

static int get_led_id_color(enum pwm_led_id id, int color)
{
#ifdef CONFIG_LED_PWM_ACTIVE_CHARGE_PORT_ONLY
	int active_chg_port = charge_manager_get_active_charge_port();

	/* We should always be able to turn off a LED. */
	if (color == -1)
		return -1;

	if (led_is_pulsing)
		return color;

	/* The inactive charge port LEDs should be off. */
	if ((int)id != active_chg_port)
		return -1;
#endif /* CONFIG_LED_PWM_ACTIVE_CHARGE_PORT_ONLY */
	return color;
}

void set_pwm_led_color(enum pwm_led_id id, int color)
{
	struct pwm_led_color_map duty = { 0 };
	const struct pwm_led *led = &pwm_leds[id];

	if ((id >= CONFIG_LED_PWM_COUNT) || (id < 0) ||
	    (color >= EC_LED_COLOR_COUNT) || (color < -1))
		return;

	if (color != -1) {
		duty.ch0 = led_color_map[color].ch0;
		duty.ch1 = led_color_map[color].ch1;
		duty.ch2 = led_color_map[color].ch2;
	}

	if (led->ch0 != PWM_LED_NO_CHANNEL)
		led->set_duty(led->ch0, duty.ch0);
	if (led->ch1 != PWM_LED_NO_CHANNEL)
		led->set_duty(led->ch1, duty.ch1);
	if (led->ch2 != PWM_LED_NO_CHANNEL)
		led->set_duty(led->ch2, duty.ch2);
}

static void set_led_color(int color)
{
	/*
	 *  We must check if auto control is enabled since the LEDs may be
	 *  controlled from the AP at anytime.
	 */
	if ((led_auto_control_is_enabled(EC_LED_ID_POWER_LED)) ||
	    (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)))
		set_pwm_led_color(PWM_LED0, get_led_id_color(PWM_LED0, color));

#if CONFIG_LED_PWM_COUNT >= 2
	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		set_pwm_led_color(PWM_LED1, get_led_id_color(PWM_LED1, color));
#endif /* CONFIG_LED_PWM_COUNT >= 2 */
}

static void set_pwm_led_enable(enum pwm_led_id id, int enable)
{
#ifndef CONFIG_ZEPHYR
	const struct pwm_led *led = &pwm_leds[id];

	if ((id >= CONFIG_LED_PWM_COUNT) || (id < 0))
		return;

	if (led->ch0 != PWM_LED_NO_CHANNEL)
		led->enable(led->ch0, enable);
	if (led->ch1 != PWM_LED_NO_CHANNEL)
		led->enable(led->ch1, enable);
	if (led->ch2 != PWM_LED_NO_CHANNEL)
		led->enable(led->ch2, enable);
#endif
}

static void init_leds_off(void)
{
	/* Turn off LEDs such that they are in a known state with zero duty. */
	set_led_color(-1);

	/* Enable pwm modules for each channels of LEDs */
	set_pwm_led_enable(PWM_LED0, 1);

#if CONFIG_LED_PWM_COUNT >= 2
	set_pwm_led_enable(PWM_LED1, 1);
#endif /* CONFIG_LED_PWM_COUNT >= 2 */
}
DECLARE_HOOK(HOOK_INIT, init_leds_off, HOOK_PRIO_POST_PWM);

#ifndef CONFIG_LED_PWM_TASK_DISABLED
static uint8_t pulse_period;
static uint8_t pulse_ontime;
static enum ec_led_colors pulse_color;
static void update_leds(void);
static void pulse_leds_deferred(void);
DECLARE_DEFERRED(pulse_leds_deferred);
static void pulse_leds_deferred(void)
{
	static uint8_t tick_count;

	if (!led_is_pulsing) {
		tick_count = 0;
		/*
		 * Since we're not pulsing anymore, turn the colors off in case
		 * we were in the "on" time.
		 */
		set_led_color(-1);
		/* Then show the desired state. */
		update_leds();
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

#ifdef CONFIG_BATTERY
static int show_charge_state(void)
{
	enum led_pwr_state chg_st = led_pwr_get_state();

	/*
	 * The colors listed below are the default, but can be overridden.
	 *
	 * Solid Amber == Charging
	 * Solid Green == Charging (near full)
	 * Fast Flash Red == Charging error or battery not present
	 */
	if (chg_st == LED_PWRS_CHARGE) {
		led_is_pulsing = 0;
		set_led_color(CONFIG_LED_PWM_CHARGE_COLOR);
	} else if (chg_st == LED_PWRS_CHARGE_NEAR_FULL ||
		   chg_st == LED_PWRS_DISCHARGE_FULL) {
		led_is_pulsing = 0;
		set_led_color(CONFIG_LED_PWM_NEAR_FULL_COLOR);
	} else if ((battery_is_present() != BP_YES) ||
		   (chg_st == LED_PWRS_ERROR)) {
		/* Ontime and period in PULSE_TICK units. */
		pulse_leds(CONFIG_LED_PWM_CHARGE_ERROR_COLOR,
			   LED_CHARGER_ERROR_ON_TIME, LED_CHARGER_ERROR_PERIOD);
	} else {
		/* Discharging or not charging. */
#ifdef CONFIG_LED_PWM_CHARGE_STATE_ONLY
		/*
		 * If we only show the charge state, the only reason we
		 * would pulse the LEDs is if we had an error.  If it no longer
		 * exists, stop pulsing the LEDs.
		 */
		led_is_pulsing = 0;
#endif /* CONFIG_LED_PWM_CHARGE_STATE_ONLY */
		return 0;
	}
	return 1;
}
#endif /* CONFIG_BATTERY */

#ifndef CONFIG_LED_PWM_CHARGE_STATE_ONLY
#ifdef CONFIG_BATTERY
static int show_battery_state(void)
{
	int batt_percentage = charge_get_percent();

	/*
	 * The colors listed below are the default, but can be overridden.
	 *
	 * Fast Flash Amber == Critical Battery
	 * Slow Flash Amber == Low Battery
	 */
	if (batt_percentage < CRITICAL_LOW_BATTERY_PERCENTAGE) {
		/* Flash amber faster (1 second period, 50% duty cycle) */
		pulse_leds(CONFIG_LED_PWM_LOW_BATT_COLOR, 2, 4);
	} else if (batt_percentage < LOW_BATTERY_PERCENTAGE) {
		/* Flash amber (4 second period, 50% duty cycle) */
		pulse_leds(CONFIG_LED_PWM_LOW_BATT_COLOR, 8, 16);
	} else {
		/* Sufficient charge, no need to show anything for this. */
		return 0;
	}
	return 1;
}
#endif /* CONFIG_BATTERY */

static int show_chipset_state(void)
{
	/* Reflect the SoC state. */
	led_is_pulsing = 0;
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* The LED must be on in the Active state. */
		set_led_color(CONFIG_LED_PWM_SOC_ON_COLOR);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
#ifdef CONFIG_LED_PWM_OFF_IN_SUSPEND
		/*
		 * New devices from 2022 onwards require LED to be off during
		 * suspend. Older devices follow the old standard of pulsing.
		 */
		set_led_color(-1);
#else
		/* The power LED must pulse in the suspend state. */
		pulse_leds(CONFIG_LED_PWM_SOC_SUSPEND_COLOR, 4, 16);
#endif /* CONFIG_POWER_LED_OFF_IN_SUSPEND */
	} else {
		/* Chipset is off, no need to show anything for this. */
		return 0;
	}
	return 1;
}
#endif /* CONFIG_LED_PWM_CHARGE_STATE_ONLY */

static void update_leds(void)
{
#ifdef CONFIG_BATTERY
	/* Reflecting the charge state is the highest priority. */
	if (show_charge_state())
		return;
#endif /* CONFIG_BATTERY */

#ifndef CONFIG_LED_PWM_CHARGE_STATE_ONLY
#ifdef CONFIG_BATTERY
	if (show_battery_state())
		return;
#endif /* CONFIG_BATTERY */

	if (show_chipset_state())
		return;
#endif /* CONFIG_LED_PWM_CHARGE_STATE_ONLY */

	set_led_color(-1);
}
DECLARE_HOOK(HOOK_TICK, update_leds, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_LED_PWM_TASK_DISABLED */

#ifdef CONFIG_CMD_LEDTEST
static int command_ledtest(int argc, const char **argv)
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
		ccprintf("PWM LED %d: led_id=%d, auto_control=%d\n", pwm_led_id,
			 led_id, led_auto_control_is_enabled(led_id) != 0);
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
