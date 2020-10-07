/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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

#define LED_ON_LVL 1
#define LED_OFF_LVL 0

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static void led_set_color_battery(enum led_color color)
{
	gpio_set_level(GPIO_EC_CHG_LED_Y_C0,
		(color == LED_AMBER) ? LED_ON_LVL : LED_OFF_LVL);
	gpio_set_level(GPIO_EC_CHG_LED_W_C0,
		(color == LED_WHITE) ? LED_ON_LVL : LED_OFF_LVL);
}

void led_set_color_power(enum led_color color)
{
	gpio_set_level(GPIO_EC_PWR_LED_W,
		(color == LED_WHITE) ? LED_ON_LVL : LED_OFF_LVL);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(LED_WHITE);
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
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate amber on when charging. */
		color = LED_AMBER;
		break;
	case PWR_STATE_DISCHARGE:
		/* Discharging : off */
		color = LED_OFF;
		break;
	case PWR_STATE_ERROR:
		/* Battery error: Amber 1 sec, off 1 sec */
		period = (1 + 1);
		battery_ticks = battery_ticks % period;
		if (battery_ticks < 1)
			color = LED_AMBER;
		else
			color = LED_OFF;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		/* Full Charged: White on */
		color = LED_WHITE;
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE) {
			/* Factory mode: White 2 sec, Amber 2 sec */
			period = (2 + 2);
			battery_ticks = battery_ticks % period;
			if (battery_ticks < 2)
				color = LED_WHITE;
			else
				color = LED_AMBER;
		} else
			color = LED_WHITE;
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

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* S3: On 1 sec, Off 3 sec */
		period = (1 + 3);
		power_ticks = power_ticks % period;
		if (power_ticks < 1)
			color = LED_WHITE;
		else
			color = LED_OFF;
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		/* S5: LED off */
		color = LED_OFF;
	} else if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* S0: LED on */
		color = LED_WHITE;
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
DECLARE_HOOK(HOOK_SECOND, led_tick, HOOK_PRIO_DEFAULT);

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

	color = state ? LED_WHITE : LED_OFF;

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);

	led_set_color_battery(color);
}
