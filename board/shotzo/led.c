/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Power LED control for Shotzo.
 * Solid white - active power
 * 25% duty cycle white, 1s on and 3s off- suspend
 * Blinking quicky white, 0.5s on and 0.5s off - alert
 * 2 long 2 short white, long for 1s, short for 0.5s and interval
 * is 0.5s - critical
 * Off - shut down
 */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "timer.h"
#include "util.h"

/*
 * Due to the CSME-Lite processing, upon startup the CPU transitions through
 * S0->S3->S5->S3->S0, causing the LED to turn on/off/on, so
 * delay turning off the LED during suspend/shutdown.
 */
#define LED_CPU_DELAY_MS (2000 * MSEC)

#define POWER_LED_ON 0
#define POWER_LED_OFF 1

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_POWER_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

static int led_set_color_power(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_PWR_LED_WHITE_L, POWER_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_PWR_LED_WHITE_L, POWER_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int led_set_color(enum ec_led_id led_id, enum led_color color)
{
	int rv;

	switch (led_id) {
	case EC_LED_ID_POWER_LED:
		rv = led_set_color_power(color);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return rv;
}

/* When blinking is enabled, led will blinking according to led_blinking_array.
 * 1 means led on, 0 means led off, restart from head after reaching the tail.
 * The interval is LED_BLINKING_MS.
 */
#define LED_BLINKING_MS (500 * MSEC)
static int *led_blinking_array;
static int led_blinking_count;
static int led_blinking_index;
static void led_blinking(void);
DECLARE_DEFERRED(led_blinking);
static void led_blinking(void)
{
	uint32_t elapsed;
	uint32_t next = 0;
	uint32_t start = get_time().le.lo;
	int signal;

	if (led_blinking_array == NULL)
		return;

	if (led_blinking_index > (led_blinking_count - 1))
		led_blinking_index = 0;

	signal = *(led_blinking_array + led_blinking_index);

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED)) {
		switch (signal) {
		case 0:
			led_set_color(EC_LED_ID_POWER_LED, LED_OFF);
			led_blinking_index += 1;
			break;
		case 1:
			led_set_color(EC_LED_ID_POWER_LED, LED_WHITE);
			led_blinking_index += 1;
			break;
		default:
			led_blinking_index = 0;
		}
	}

	elapsed = get_time().le.lo - start;
	next = elapsed < LED_BLINKING_MS ? LED_BLINKING_MS - elapsed : 0;
	hook_call_deferred(&led_blinking_data, next);
}

static int led_suspend_array[] = { 1, 1, 0, 0, 0, 0, 0, 0 };
const int led_suspend_count = ARRAY_SIZE(led_suspend_array);
static void led_suspend(void)
{
	led_blinking_array = led_suspend_array;
	led_blinking_count = led_suspend_count;
	led_blinking_index = 0;
	led_blinking();
}
DECLARE_DEFERRED(led_suspend);

static void led_shutdown(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_color(EC_LED_ID_POWER_LED, LED_OFF);
}
DECLARE_DEFERRED(led_shutdown);

static void led_suspend_hook(void)
{
	hook_call_deferred(&led_blinking_data, -1);
	hook_call_deferred(&led_shutdown_data, -1);
	hook_call_deferred(&led_suspend_data, LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, led_suspend_hook, HOOK_PRIO_DEFAULT);

static void led_shutdown_hook(void)
{
	hook_call_deferred(&led_blinking_data, -1);
	hook_call_deferred(&led_suspend_data, -1);
	hook_call_deferred(&led_shutdown_data, LED_CPU_DELAY_MS);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, led_shutdown_hook, HOOK_PRIO_DEFAULT);

static void led_resume_hook(void)
{
	hook_call_deferred(&led_blinking_data, -1);
	hook_call_deferred(&led_suspend_data, -1);
	hook_call_deferred(&led_shutdown_data, -1);

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_color(EC_LED_ID_POWER_LED, LED_WHITE);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, led_resume_hook, HOOK_PRIO_DEFAULT);

static int led_alert_array[] = { 1, 0 };
const int led_alert_count = ARRAY_SIZE(led_alert_array);
void led_alert(int enable)
{
	if (enable) {
		/* Overwrite the current signal */
		hook_call_deferred(&led_blinking_data, -1);
		led_blinking_array = led_alert_array;
		led_blinking_count = led_alert_count;
		led_blinking_index = 0;
		led_blinking();
	} else {
		/* Restore the previous signal */
		if (chipset_in_state(CHIPSET_STATE_ON))
			led_resume_hook();
		else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
			led_suspend_hook();
		else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			led_shutdown_hook();
	}
}

static int led_critical_array[] = { 1, 1, 0, 1, 1, 0, 1, 0, 1, 0 };
const int led_critical_count = ARRAY_SIZE(led_critical_array);
void show_critical_error(void)
{
	hook_call_deferred(&led_blinking_data, -1);
	led_blinking_array = led_critical_array;
	led_blinking_count = led_critical_count;
	led_blinking_index = 0;
	led_blinking();
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	switch (led_id) {
	case EC_LED_ID_POWER_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		break;
	default:
		break;
	}
}

int led_set_brightness(enum ec_led_id id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_WHITE] != 0)
		led_set_color(id, LED_WHITE);
	else
		led_set_color(id, LED_OFF);

	return EC_SUCCESS;
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
		led_set_color(id, LED_OFF);
	} else if (!strcasecmp(argv[1], "white")) {
		led_set_color(id, LED_WHITE);
	} else if (!strcasecmp(argv[1], "alert")) {
		led_alert(1);
	} else if (!strcasecmp(argv[1], "crit")) {
		show_critical_error();
	} else if (!strcasecmp(argv[1], "resume")) {
		led_resume_hook();
	} else {
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led, "[debug|white|off|alert|crit|resume]",
			"Turn on/off LED.");
