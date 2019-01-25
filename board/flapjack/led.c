/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Flapjack board.
 */

#include "battery.h"
#include "charge_state.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "led_common.h"

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

#define LED_OFF		MT6370_LED_ID_OFF
#define LED_GREEN	MT6370_LED_ID1
#define LED_RED		MT6370_LED_ID2

static void led_set_battery(void)
{
	static int battery_second;
	uint32_t chflags = charge_get_flags();

	battery_second++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		mt6370_led_set_color(LED_RED);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() <= 10)
			mt6370_led_set_color(
			   (battery_second & 0x4) ? LED_RED : LED_OFF);
		else
			mt6370_led_set_color(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		mt6370_led_set_color((battery_second & 0x2) ?
				LED_RED : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		mt6370_led_set_color(LED_GREEN);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		if (chflags & CHARGE_FLAG_FORCE_IDLE) {
			mt6370_led_set_color(LED_RED);
			mt6370_led_set_dim_mode(LED_RED,
						MT6370_LED_DIM_MODE_BREATH);
		} else {
			mt6370_led_set_color(LED_GREEN);
			mt6370_led_set_dim_mode(LED_GREEN,
						MT6370_LED_DIM_MODE_BREATH);
		}
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return;

	brightness_range[EC_LED_COLOR_GREEN] = MT6370_LED_BRIGHTNESS_MAX;
	brightness_range[EC_LED_COLOR_RED] = MT6370_LED_BRIGHTNESS_MAX;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return EC_ERROR_INVAL;

	mt6370_led_set_brightness(LED_GREEN, brightness[EC_LED_COLOR_GREEN]);
	mt6370_led_set_brightness(LED_RED, brightness[EC_LED_COLOR_RED]);
	return EC_SUCCESS;
}

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
