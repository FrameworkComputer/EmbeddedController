/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power LED control for Brask.
 * Solid white - active power
 * White flashing - suspended
 * Red flashing - alert
 * Solid red - critical
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

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ##args)

/*
 * Due to the CSME-Lite processing, upon startup the CPU transitions through
 * S0->S3->S5->S3->S0, causing the LED to turn on/off/on, so
 * delay turning off the LED during suspend/shutdown.
 */
#define LED_CPU_DELAY_MS (2000 * MSEC)

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_POWER_LED };
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_WHITE,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

static int set_color_power(enum led_color color, int duty)
{
	int white = 0;
	int red = 0;

	if (duty < 0 || 100 < duty)
		return EC_ERROR_UNKNOWN;

	switch (color) {
	case LED_OFF:
		break;
	case LED_WHITE:
		white = 1;
		break;
	case LED_RED:
		red = 1;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	if (red)
		pwm_set_duty(PWM_CH_LED_RED, duty);
	else
		pwm_set_duty(PWM_CH_LED_RED, 0);

	if (white)
		pwm_set_duty(PWM_CH_LED_WHITE, duty);
	else
		pwm_set_duty(PWM_CH_LED_WHITE, 0);

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

#define LED_PULSE_US (2 * SECOND)
/* 40 msec for nice and smooth transition. */
#define LED_PULSE_TICK_US (40 * MSEC)

/*
 * When pulsing is enabled, brightness is incremented by <duty_inc> every
 * <interval> usec from 0 to 100% in LED_PULSE_US usec. Then it's decremented
 * likewise in LED_PULSE_US usec.
 */
static struct {
	uint32_t interval;
	int duty_inc;
	enum led_color color;
	int duty;
} led_pulse;

#define CONFIG_TICK(interval, color) \
	config_tick((interval), 100 / (LED_PULSE_US / (interval)), (color))

static void config_tick(uint32_t interval, int duty_inc, enum led_color color)
{
	led_pulse.interval = interval;
	led_pulse.duty_inc = duty_inc;
	led_pulse.color = color;
	led_pulse.duty = 0;
}

static void pulse_power_led(enum led_color color)
{
	set_color(EC_LED_ID_POWER_LED, color, led_pulse.duty);
	if (led_pulse.duty + led_pulse.duty_inc > 100)
		led_pulse.duty_inc = led_pulse.duty_inc * -1;
	else if (led_pulse.duty + led_pulse.duty_inc < 0)
		led_pulse.duty_inc = led_pulse.duty_inc * -1;
	led_pulse.duty += led_pulse.duty_inc;
}

static void led_tick(void);
DECLARE_DEFERRED(led_tick);
static void led_tick(void)
{
	uint32_t elapsed;
	uint32_t next = 0;
	uint32_t start = get_time().le.lo;

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		pulse_power_led(led_pulse.color);
	elapsed = get_time().le.lo - start;
	next = led_pulse.interval > elapsed ? led_pulse.interval - elapsed : 0;
	hook_call_deferred(&led_tick_data, next);
}

static void led_suspend(void)
{
	CONFIG_TICK(LED_PULSE_TICK_US, LED_WHITE);
	led_tick();
}
DECLARE_DEFERRED(led_suspend);

static void led_shutdown(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_OFF, 0);
}
DECLARE_DEFERRED(led_shutdown);

static void led_shutdown_hook(void)
{
	hook_call_deferred(&led_tick_data, -1);
	hook_call_deferred(&led_suspend_data, -1);
	hook_call_deferred(&led_shutdown_data, LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, led_shutdown_hook, HOOK_PRIO_DEFAULT);

static void led_suspend_hook(void)
{
	hook_call_deferred(&led_shutdown_data, -1);
	hook_call_deferred(&led_suspend_data, LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, led_suspend_hook, HOOK_PRIO_DEFAULT);

static void led_resume(void)
{
	/*
	 * Assume there is no race condition with led_tick, which also
	 * runs in hook_task.
	 */
	hook_call_deferred(&led_tick_data, -1);
	/*
	 * Avoid invoking the suspend/shutdown delayed hooks.
	 */
	hook_call_deferred(&led_suspend_data, -1);
	hook_call_deferred(&led_shutdown_data, -1);
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_WHITE, 100);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, led_resume, HOOK_PRIO_DEFAULT);

void board_led_auto_control(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		led_resume();
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		led_suspend_hook();
	else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		led_shutdown_hook();
}

void led_alert(int enable)
{
	if (enable) {
		/* Overwrite the current signal */
		config_tick(1 * SECOND, 100, LED_RED);
		led_tick();
	} else {
		/* Restore the previous signal */
		if (chipset_in_state(CHIPSET_STATE_ON))
			led_resume();
		else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
			led_suspend_hook();
		else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			led_shutdown_hook();
	}
}

void show_critical_error(void)
{
	hook_call_deferred(&led_tick_data, -1);
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_RED, 100);
}

static int command_led(int argc, const char **argv)
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
	} else if (!strcasecmp(argv[1], "white")) {
		set_color(id, LED_WHITE, 100);
	} else if (!strcasecmp(argv[1], "alert")) {
		led_alert(1);
	} else if (!strcasecmp(argv[1], "crit")) {
		show_critical_error();
	} else {
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led, "[debug|red|white|off|alert|crit]",
			"Turn on/off LED.");

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
}

int led_set_brightness(enum ec_led_id id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_RED])
		return set_color(id, LED_RED, brightness[EC_LED_COLOR_RED]);
	else if (brightness[EC_LED_COLOR_WHITE])
		return set_color(id, LED_WHITE, brightness[EC_LED_COLOR_WHITE]);
	else
		return set_color(id, LED_OFF, 0);
}
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/* Blink alert if insufficient power per system_can_boot_ap(). */
	int insufficient_power =
		(charge_ma * charge_mv) <
		(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON * 1000);
	led_alert(insufficient_power);
}
