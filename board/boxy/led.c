/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Boxy specific PWM LED settings. */

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

#define PULSE_TICK (250 * MSEC)

static uint8_t pulse_request;
static uint8_t led_is_pulsing;

static int get_led_id_color(enum pwm_led_id id, int color)
{
	return color;
}

static void set_led_color(int color)
{
	/*
	 *  We must check if auto control is enabled since the LEDs may be
	 *  controlled from the AP at anytime.
	 */
	if ((led_auto_control_is_enabled(EC_LED_ID_POWER_LED)) || pulse_request)
		set_pwm_led_color(PWM_LED0, get_led_id_color(PWM_LED0, color));
}

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

static int show_chipset_state(void)
{
	/* Reflect the SoC state. */
	led_is_pulsing = 0;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		if (pulse_request &&
		    !led_auto_control_is_enabled(EC_LED_ID_POWER_LED)) {
			pulse_leds(EC_LED_COLOR_WHITE, 2, 4);
		} else {
			/* The LED must be on in the Active state. */
			set_led_color(EC_LED_COLOR_WHITE);
			pulse_request = 0;
		}
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* The power LED must pulse in the suspend state. */
		pulse_leds(EC_LED_COLOR_WHITE, 4, 8);
		pulse_request = 0;
	} else {
		/* Chipset is off, no need to show anything for this. */
		pulse_request = 0;
		return 0;
	}
	return 1;
}

static void update_leds(void)
{
	if (show_chipset_state())
		return;

	set_led_color(-1);
}
DECLARE_HOOK(HOOK_TICK, update_leds, HOOK_PRIO_DEFAULT);

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

/* One physical LED with red, green, and blue. */
struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT] = {
	/* Red, Green, Blue */
	[EC_LED_COLOR_RED] = { 100, 0, 0 },
	[EC_LED_COLOR_GREEN] = { 0, 100, 0 },
	[EC_LED_COLOR_BLUE] = { 0, 0, 100 },
	[EC_LED_COLOR_YELLOW] = { 50, 50, 0 },
	[EC_LED_COLOR_WHITE] = { 50, 50, 50 },
	[EC_LED_COLOR_AMBER] = { 70, 30, 0 },
};

/* One logical LED with red, green, and blue channels. */
struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	{
		.ch0 = PWM_CH_LED_RED,
		.ch1 = PWM_CH_LED_GREEN,
		.ch2 = PWM_CH_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
};

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, '\0',
	       sizeof(*brightness_range) * EC_LED_COLOR_COUNT);
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_YELLOW] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	enum pwm_led_id pwm_id;

	pulse_request = 0;
	/* Convert ec_led_id to pwm_led_id. */
	if (led_id == EC_LED_ID_POWER_LED)
		pwm_id = PWM_LED0;
	else
		return EC_ERROR_UNKNOWN;

	if (brightness[EC_LED_COLOR_RED])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_RED);
	else if (brightness[EC_LED_COLOR_GREEN])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_GREEN);
	else if (brightness[EC_LED_COLOR_BLUE])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_BLUE);
	else if (brightness[EC_LED_COLOR_YELLOW])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_YELLOW);
	else if (brightness[EC_LED_COLOR_WHITE])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_WHITE);
	else if (brightness[EC_LED_COLOR_AMBER])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_AMBER);
	else
		/* Otherwise, the "color" is "off". */
		set_pwm_led_color(pwm_id, -1);

	return EC_SUCCESS;
}

void board_led_init(void)
{
	led_auto_control(EC_LED_ID_POWER_LED, 0);
	pulse_request = 1;
}

DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_led_init, HOOK_PRIO_DEFAULT);

static void board_led_shutdown(void)
{
	led_auto_control(EC_LED_ID_POWER_LED, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_led_shutdown, HOOK_PRIO_DEFAULT);
