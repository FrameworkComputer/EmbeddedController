/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Kukui board.
 */

#include "battery.h"
#include "charge_state.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "led_common.h"

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

#define LED_OFF		MT6370_LED_ID_OFF
#define LED_RED		MT6370_LED_ID1
#define LED_GREEN	MT6370_LED_ID2
#define LED_BLUE	MT6370_LED_ID3

#define LED_MASK_OFF	0
#define LED_MASK_RED	MT6370_MASK_RGB_ISNK1DIM_EN
#define LED_MASK_GREEN	MT6370_MASK_RGB_ISNK2DIM_EN
#define LED_MASK_BLUE	MT6370_MASK_RGB_ISNK3DIM_EN

static void kukui_led_set_battery(void)
{
	static enum charge_state prv_chstate = PWR_STATE_UNCHANGE;
	enum charge_state chstate;
	uint8_t blue = 0, green = 0, red = 0;

	chstate = charge_get_state();

	if (prv_chstate == chstate)
		return;

	prv_chstate = chstate;

	switch (chstate) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		blue = 1;
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() <= 10)
			red = 1;
		break;
	case PWR_STATE_ERROR:
		red = 1;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		green = 1;
		break;
	default:
		/* Other states don't alter LED behavior */
		return;
	}
	mt6370_led_set_brightness(LED_RED, red);
	mt6370_led_set_brightness(LED_BLUE, blue);
	mt6370_led_set_brightness(LED_GREEN, green);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return;

	brightness_range[EC_LED_COLOR_RED] = MT6370_LED_BRIGHTNESS_MAX;
	brightness_range[EC_LED_COLOR_GREEN] = MT6370_LED_BRIGHTNESS_MAX;
	brightness_range[EC_LED_COLOR_BLUE] = MT6370_LED_BRIGHTNESS_MAX;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return EC_ERROR_INVAL;

	mt6370_led_set_brightness(LED_RED, brightness[EC_LED_COLOR_RED]);
	mt6370_led_set_brightness(LED_GREEN, brightness[EC_LED_COLOR_GREEN]);
	mt6370_led_set_brightness(LED_BLUE, brightness[EC_LED_COLOR_BLUE]);
	return EC_SUCCESS;
}

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		kukui_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

static void kukui_led_init(void)
{
	/* Enable all LEDs, and set brightness to 0. */
	mt6370_led_set_brightness(LED_RED, 0);
	mt6370_led_set_brightness(LED_GREEN, 0);
	mt6370_led_set_brightness(LED_BLUE, 0);
	mt6370_led_set_color(LED_MASK_RED | LED_MASK_GREEN | LED_MASK_BLUE);
}
DECLARE_HOOK(HOOK_INIT, kukui_led_init, HOOK_PRIO_DEFAULT);
