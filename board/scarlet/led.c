/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Scarlet board.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "hooks.h"
#include "led_common.h"
#include "lid_switch.h"
#include "pwm.h"
#include "util.h"

/* LEDs on Scarlet are active low. */
#define BAT_LED_ON 0
#define BAT_LED_OFF 1

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_AMBER,
	LED_GREEN,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		break;
	case LED_RED:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		break;
	case LED_GREEN:
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static void scarlet_led_set_battery(void)
{
	static int battery_second;
	uint32_t chflags = charge_get_flags();

	battery_second++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() < 3)
			bat_led_set_color((battery_second & 1)
					? LED_OFF : LED_AMBER);
		else if (charge_get_percent() < 10)
			bat_led_set_color((battery_second & 3)
					? LED_OFF : LED_AMBER);
		else if (charge_get_percent() >= BATTERY_LEVEL_NEAR_FULL &&
		    (chflags & CHARGE_FLAG_EXTERNAL_POWER))
			bat_led_set_color(LED_GREEN);
		else
			bat_led_set_color(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		bat_led_set_color(LED_RED);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set_color(LED_GREEN);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			bat_led_set_color(
				(battery_second & 0x2) ? LED_GREEN : LED_AMBER);
		else
			bat_led_set_color(LED_GREEN);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
		brightness_range[EC_LED_COLOR_GREEN] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		gpio_set_level(GPIO_BAT_LED_RED,
			       (brightness[EC_LED_COLOR_RED] != 0) ?
				BAT_LED_ON : BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_GREEN,
			       (brightness[EC_LED_COLOR_GREEN] != 0) ?
				BAT_LED_ON : BAT_LED_OFF);
		return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		scarlet_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
	    (led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_auto_control(EC_LED_ID_BATTERY_LED, 1);
		return;
	}

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);
	bat_led_set_color(state ? LED_AMBER : LED_OFF);
}
