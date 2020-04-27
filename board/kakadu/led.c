/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for kakadu board.
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

#define LED_MASK_OFF	0
#define LED_MASK_RED	MT6370_MASK_RGB_ISNK1DIM_EN
#define LED_MASK_GREEN	MT6370_MASK_RGB_ISNK2DIM_EN

static void kakadu_led_set_battery(void)
{
	enum charge_state chstate;
	static uint8_t prv_r, prv_g;
	uint8_t br[EC_LED_COLOR_COUNT] = { 0 };

	chstate = charge_get_state();

	if (prv_chstate == chstate &&
		chstate != PWR_STATE_DISCHARGE)
		return;

	prv_chstate = chstate;

	switch (chstate) {
	case PWR_STATE_CHARGE:
		br[EC_LED_COLOR_GREEN] = 255;
		br[EC_LED_COLOR_RED] = 255;
		break;
	case PWR_STATE_DISCHARGE:
		/* real battery SoC 5%*/
		if (charge_get_percent() <= 5)
			br[EC_LED_COLOR_RED] = 255;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		br[EC_LED_COLOR_GREEN] = 255;
		break;
	case PWR_STATE_ERROR:
		br[EC_LED_COLOR_RED] = 255;
		break;
	default:
		/* Other states don't alter LED behavior */
		return;
	}

	if (prv_r == br[EC_LED_COLOR_RED] &&
	    prv_g == br[EC_LED_COLOR_GREEN])
		return;

	prv_r = br[EC_LED_COLOR_RED];
	prv_g = br[EC_LED_COLOR_GREEN];
	led_set_brightness(EC_LED_ID_BATTERY_LED, br);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return;

	brightness_range[EC_LED_COLOR_RED] = MT6370_LED_BRIGHTNESS_MAX;
	brightness_range[EC_LED_COLOR_GREEN] = MT6370_LED_BRIGHTNESS_MAX;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	uint8_t red, green;

	if (led_id != EC_LED_ID_BATTERY_LED)
		return EC_ERROR_INVAL;

	red = brightness[EC_LED_COLOR_RED];
	green = brightness[EC_LED_COLOR_GREEN];

	mt6370_led_set_brightness(LED_RED, red);
	mt6370_led_set_brightness(LED_GREEN, green);

	/* Enables LED sink power if necessary. */
	mt6370_led_set_color((red ? LED_MASK_RED : 0) |
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

static void kakadu_led_init(void)
{
	const enum mt6370_led_dim_mode dim = MT6370_LED_DIM_MODE_PWM;
	const enum mt6370_led_pwm_freq freq = MT6370_LED_PWM_FREQ1000;

	mt6370_led_set_color(0);
	mt6370_led_set_dim_mode(LED_RED, dim);
	mt6370_led_set_dim_mode(LED_GREEN, dim);
	mt6370_led_set_pwm_frequency(LED_RED, freq);
	mt6370_led_set_pwm_frequency(LED_GREEN, freq);
	mt6370_led_set_pwm_dim_duty(LED_RED, 12);
	mt6370_led_set_pwm_dim_duty(LED_GREEN, 31);
}
DECLARE_HOOK(HOOK_INIT, kakadu_led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		kakadu_led_set_battery();
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
