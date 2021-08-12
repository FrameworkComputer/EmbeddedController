/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "system.h"
#include "util.h"
#include "extpower.h"

#define LED_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)
/* Battery LED blinks every per 400ms */
#define LED_HALF_ONE_SEC (500 / HOOK_TICK_INTERVAL_MS)

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static void led_set_color_battery(enum led_color color)
{
	gpio_set_level(GPIO_EC_CHG_LED_R_C0,
		(color == LED_RED) ? BAT_LED_ON : BAT_LED_OFF);
	gpio_set_level(GPIO_EC_CHG_LED_G_C0,
		(color == LED_GREEN) ? BAT_LED_ON : BAT_LED_OFF);
	if (color == LED_AMBER) {
		gpio_set_level(GPIO_EC_CHG_LED_R_C0, BAT_LED_ON);
		gpio_set_level(GPIO_EC_CHG_LED_G_C0, BAT_LED_ON);
	}
}

static void led_set_color_power(enum led_color color)
{
	gpio_set_level(GPIO_EC_PWRBTN_LED,
		(color == LED_WHITE) ? BAT_LED_ON : BAT_LED_OFF);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
		brightness_range[EC_LED_COLOR_GREEN] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_battery(LED_RED);
		else if (brightness[EC_LED_COLOR_GREEN] != 0)
			led_set_color_battery(LED_GREEN);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LED_AMBER);
		else
			led_set_color_battery(LED_OFF);
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_power(LED_WHITE);
		else
			led_set_color_power(LED_OFF);
	}

	return EC_SUCCESS;
}

static void board_led_set_battery(void)
{
	static int battery_ticks;
	int color = LED_OFF;
	int period = 0;
	int percent = DIV_ROUND_NEAREST(charge_get_display_charge(), 10);
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
	case PWR_STATE_CHARGE_NEAR_FULL:
		if (chipset_in_state(CHIPSET_STATE_ON |
					CHIPSET_STATE_ANY_SUSPEND |
					CHIPSET_STATE_ANY_OFF)) {
			if (percent <= BATTERY_LEVEL_CRITICAL) {
				/* battery capa <= 5%, Red */
				color = LED_RED;
			} else if (percent > BATTERY_LEVEL_CRITICAL &&
					percent < BATTERY_LEVEL_NEAR_FULL) {
				/* 5% < battery capa < 97%, Orange */
				color = LED_AMBER;
			} else {
				/* battery capa >= 97%, Green */
				color = LED_GREEN;
			}
		}
		break;
	case PWR_STATE_DISCHARGE:
		color = LED_OFF;
		break;
	case PWR_STATE_ERROR:
		/* Battery error, Red on 1sec off 1sec */
		period = (1 + 1) * LED_ONE_SEC;
		battery_ticks = battery_ticks % period;
		if (battery_ticks < 1 * LED_ONE_SEC)
			color = LED_RED;
		else
			color = LED_OFF;
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE) {
			/* Factory mode, Red 2 sec, green 2 sec */
			period = (2 + 2) * LED_ONE_SEC;
			battery_ticks = battery_ticks % period;
			if (battery_ticks < 2 * LED_ONE_SEC)
				color = LED_RED;
			else
				color = LED_GREEN;
		} else
			color = LED_RED;
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	led_set_color_battery(color);
}

static void board_led_set_power(void)
{
	static int power_ticks;
	int color = LED_OFF;
	int period = 0;

	power_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
	case PWR_STATE_CHARGE_NEAR_FULL:
	case PWR_STATE_DISCHARGE:
		if (chipset_in_state(CHIPSET_STATE_ON)) {
			/* S0, White (soild on) */
			color = LED_WHITE;
		} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
			/* S3, white (3s on 500ms off) */
			period = 1 * LED_HALF_ONE_SEC + 3 * LED_ONE_SEC;
			power_ticks = power_ticks % period;
			if (power_ticks < 3 * LED_ONE_SEC)
				color = LED_WHITE;
			else
				color = LED_OFF;
		} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/* S5, off */
			color = LED_OFF;
		}
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	led_set_color_power(color);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		board_led_set_battery();
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		board_led_set_power();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	enum led_color color;

	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
		(led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_auto_control(EC_LED_ID_BATTERY_LED, 1);
		board_led_set_battery();
		return;
	}

	color = state ? LED_RED : LED_OFF;

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);

	led_set_color_battery(color);
}
