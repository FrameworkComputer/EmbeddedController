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

static enum charge_state prv_chstate = PWR_STATE_INIT;

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
	enum charge_state chstate;
	uint8_t br[EC_LED_COLOR_COUNT] = { 0 };

	chstate = charge_get_state();

	if (prv_chstate == chstate)
		return;

	prv_chstate = chstate;

	switch (chstate) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		br[EC_LED_COLOR_BLUE] = 1;
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() <= 10)
			br[EC_LED_COLOR_RED] = 1;
		break;
	case PWR_STATE_ERROR:
		br[EC_LED_COLOR_RED] = 1;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		br[EC_LED_COLOR_GREEN] = 1;
		break;
	default:
		/* Other states don't alter LED behavior */
		return;
	}

	led_set_brightness(EC_LED_ID_BATTERY_LED, br);
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
	uint8_t red, green, blue;

	if (led_id != EC_LED_ID_BATTERY_LED)
		return EC_ERROR_INVAL;

	red = brightness[EC_LED_COLOR_RED];
	green = brightness[EC_LED_COLOR_GREEN];
	blue = brightness[EC_LED_COLOR_BLUE];

	mt6370_led_set_brightness(LED_RED, red);
	mt6370_led_set_brightness(LED_GREEN, green);
	mt6370_led_set_brightness(LED_BLUE, blue);

	/* Enables LED sink power if necessary. */
	mt6370_led_set_color((red ? LED_MASK_RED : 0) |
			     (blue ? LED_MASK_BLUE : 0) |
			     (green ? LED_MASK_GREEN : 0));
	return EC_SUCCESS;
}

/*
 * Reset prv_chstate so that led can be updated immediately once
 * auto-controlled.
 */
static void led_reset_auto_control(void)
{
	prv_chstate = PWR_STATE_INIT;
}

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		kukui_led_set_battery();
	else
		led_reset_auto_control();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

__override void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	uint8_t br[EC_LED_COLOR_COUNT] = { 0 };

	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
	    (led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_reset_auto_control();
		led_auto_control(EC_LED_ID_BATTERY_LED, 1);
		return;
	}

	if (state)
		br[EC_LED_COLOR_GREEN] = 1;

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);
	led_set_brightness(EC_LED_ID_BATTERY_LED, br);
}
