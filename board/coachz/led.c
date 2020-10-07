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

#define LED_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_BLUE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static void led_set_color(enum led_color color)
{
	gpio_set_level(GPIO_EC_CHG_LED_Y_C0,
		(color == LED_AMBER) ? BAT_LED_ON : BAT_LED_OFF);
	gpio_set_level(GPIO_EC_CHG_LED_W_C0,
		(color == LED_BLUE) ? BAT_LED_ON : BAT_LED_OFF);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_AMBER] = 1;
	brightness_range[EC_LED_COLOR_BLUE] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color(LED_BLUE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color(LED_AMBER);
	else
		led_set_color(LED_OFF);

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
		if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
			/* Discharging in S3: Amber 1 sec, off 3 sec */
			period = (1 + 3) * LED_ONE_SEC;
			battery_ticks = battery_ticks % period;
			if (battery_ticks < 1 * LED_ONE_SEC)
				color = LED_AMBER;
			else
				color = LED_OFF;
		} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/* Discharging in S5: off */
			color = LED_OFF;
		} else if (chipset_in_state(CHIPSET_STATE_ON)) {
			/* Discharging in S0: Blue on */
			color = LED_BLUE;
		}
		break;
	case PWR_STATE_ERROR:
		/* Battery error: Amber 1 sec, off 1 sec */
		period = (1 + 1) * LED_ONE_SEC;
		battery_ticks = battery_ticks % period;
		if (battery_ticks < 1 * LED_ONE_SEC)
			color = LED_AMBER;
		else
			color = LED_OFF;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		/* Full Charged: Blue on */
		color = LED_BLUE;
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE) {
			/* Factory mode: Blue 2 sec, Amber 2 sec */
			period = (2 + 2) * LED_ONE_SEC;
			battery_ticks = battery_ticks % period;
			if (battery_ticks < 2 * LED_ONE_SEC)
				color = LED_BLUE;
			else
				color = LED_AMBER;
		} else
			color = LED_BLUE;
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	led_set_color(color);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		board_led_set_battery();
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

	color = state ? LED_BLUE : LED_OFF;

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);

	led_set_color(color);
}
