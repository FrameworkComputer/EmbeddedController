/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED and Power LED control for Rowan Board.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "util.h"
#include "system.h"

#define CRITICAL_LOW_BATTERY_PERMILLAGE 71
#define LOW_BATTERY_PERMILLAGE 137
#define FULL_BATTERY_PERMILLAGE 937

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	BAT_LED_RED = 0,
	BAT_LED_GREEN,
	LED_COLOR_COUNT		/* Number of colors, not a color itself */
};

static int bat_led_set(enum led_color color, int on)
{
	switch (color) {
	case BAT_LED_RED:
		gpio_set_level(GPIO_BAT_LED0, on); /* BAT_LED_RED */
		break;
	case BAT_LED_GREEN:
		gpio_set_level(GPIO_BAT_LED1, on); /* BAT_LED_GREEN */
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* Ignoring led_id as both leds support the same colors */
	brightness_range[EC_LED_COLOR_RED] = 1;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (EC_LED_ID_BATTERY_LED == led_id) {
		if (brightness[EC_LED_COLOR_RED] != 0) {
			bat_led_set(BAT_LED_RED, 1);
			bat_led_set(BAT_LED_GREEN, 0);
		} else if (brightness[EC_LED_COLOR_GREEN] != 0) {
			bat_led_set(BAT_LED_RED, 0);
			bat_led_set(BAT_LED_GREEN, 1);
		} else {
			bat_led_set(BAT_LED_RED, 0);
			bat_led_set(BAT_LED_GREEN, 0);
		}
		return EC_SUCCESS;
	} else {
		return EC_ERROR_UNKNOWN;
	}
}

static unsigned blink_second;

static void rowan_led_set_battery(void)
{
	/*
	 * BAT LED behavior:
	 * - Fully charged / normal idle: Blue ON
	 * - Charging: Orange ON
	 * - Battery discharging capacity<10%, Orange blink(1:3)
	 *				 < 3%, Orange blink(1:1)
	 * - Battery error: Orange blink(1:1)
	 * - Factory force idle: Blue 2 sec, Orange 2 sec
	 */
	uint32_t charge_flags = charge_get_flags();
	int remaining_capacity;
	int full_charge_capacity;
	int permillage;

	/* Make the percentage approximate to UI shown */
	remaining_capacity = *(int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	full_charge_capacity = *(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	permillage = !full_charge_capacity ? 0 :
		(1000 * remaining_capacity) / full_charge_capacity;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		if (permillage < FULL_BATTERY_PERMILLAGE) {
			bat_led_set(BAT_LED_RED, 0);
			bat_led_set(BAT_LED_GREEN, 1);
		} else {
			bat_led_set(BAT_LED_RED, 1);
			bat_led_set(BAT_LED_GREEN, 0);
		}
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set(BAT_LED_RED, 1);
		bat_led_set(BAT_LED_GREEN, 0);
		break;
	case PWR_STATE_DISCHARGE:
		bat_led_set(BAT_LED_RED, 0);
		if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
		    permillage <= CRITICAL_LOW_BATTERY_PERMILLAGE)
			bat_led_set(BAT_LED_GREEN,
				    (blink_second & 1) ? 0 : 1);
		else if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
			 permillage <= LOW_BATTERY_PERMILLAGE)
			bat_led_set(BAT_LED_GREEN,
				    (blink_second & 3) ? 0 : 1);
		else
			bat_led_set(BAT_LED_GREEN, 0);
		break;
	case PWR_STATE_ERROR:
		bat_led_set(BAT_LED_RED, 0);
		bat_led_set(BAT_LED_GREEN, (blink_second & 1) ? 0 : 1);
		break;
	case PWR_STATE_IDLE: /* Ext. power connected in IDLE. */
		if (charge_flags & CHARGE_FLAG_FORCE_IDLE) {
			bat_led_set(BAT_LED_RED, (blink_second & 2) ? 0 : 1);
			bat_led_set(BAT_LED_GREEN, (blink_second & 2) ? 1 : 0);
		} else {
			bat_led_set(BAT_LED_RED, 1);
			bat_led_set(BAT_LED_GREEN, 0);
		}
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

/**
 * Called by hook task every 1 sec
 */
static void led_second(void)
{
	blink_second++;

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		rowan_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
