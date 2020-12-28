/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Katsu board.
 */

#include "battery.h"
#include "charge_state.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "led_common.h"
#include "power.h"

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

#define LED_OFF		MT6370_LED_ID_OFF
#define LED_AMBER	MT6370_LED_ID1
#define LED_WHITE	MT6370_LED_ID2

#define LED_MASK_OFF	0
#define LED_MASK_AMBER	MT6370_MASK_RGB_ISNK1DIM_EN
#define LED_MASK_WHITE	MT6370_MASK_RGB_ISNK2DIM_EN

static void katsu_led_set_battery(void)
{
	enum charge_state chstate;
	enum power_state powerstate;
	static uint8_t prv_white, prv_amber;
	static uint8_t time_cnt;
	uint8_t br[EC_LED_COLOR_COUNT] = { 0 };

	chstate = charge_get_state();
	powerstate = power_get_state();

	switch (chstate) {
	case PWR_STATE_CHARGE:
	case PWR_STATE_CHARGE_NEAR_FULL:
		if (charge_get_percent() < 94) {
			br[EC_LED_COLOR_AMBER] = 1;
			br[EC_LED_COLOR_WHITE] = 0;
			break;
		}
		br[EC_LED_COLOR_WHITE] = 1;
		br[EC_LED_COLOR_AMBER] = 0;
		break;
	case PWR_STATE_DISCHARGE:
		if (powerstate == POWER_S0) {
			/* display SoC 10% = real battery SoC 13%*/
			if (charge_get_percent() < 14) {
				if (time_cnt < 1) {
					time_cnt++;
					br[EC_LED_COLOR_WHITE] = 0;
					br[EC_LED_COLOR_AMBER] = 1;
				} else {
					time_cnt++;
					br[EC_LED_COLOR_WHITE] = 0;
					br[EC_LED_COLOR_AMBER] = 0;
					if (time_cnt > 3)
						time_cnt = 0;
				}
				break;
			}
			br[EC_LED_COLOR_WHITE] = 1;
			br[EC_LED_COLOR_AMBER] = 0;
			break;
		} else if (powerstate == POWER_S3) {
			if (time_cnt < 2) {
				time_cnt++;
				br[EC_LED_COLOR_WHITE] = 1;
				br[EC_LED_COLOR_AMBER] = 0;
			} else {
				time_cnt++;
				br[EC_LED_COLOR_WHITE] = 0;
				br[EC_LED_COLOR_AMBER] = 0;
				if (time_cnt > 3)
					time_cnt = 0;
			}
			break;
		} else if (powerstate == POWER_S5 || powerstate == POWER_G3) {
			br[EC_LED_COLOR_WHITE] = 0;
			br[EC_LED_COLOR_AMBER] = 0;
		}
		break;
	case PWR_STATE_ERROR:
		if (powerstate == POWER_S0) {
			if (time_cnt < 1) {
				time_cnt++;
				br[EC_LED_COLOR_WHITE] = 0;
				br[EC_LED_COLOR_AMBER] = 1;
			} else {
				time_cnt++;
				br[EC_LED_COLOR_WHITE] = 0;
				br[EC_LED_COLOR_AMBER] = 0;
				if (time_cnt > 1)
					time_cnt = 0;
			}
			break;
		} else if (powerstate == POWER_S3) {
			if (time_cnt < 2) {
				time_cnt++;
				br[EC_LED_COLOR_WHITE] = 1;
				br[EC_LED_COLOR_AMBER] = 0;
			} else {
				time_cnt++;
				br[EC_LED_COLOR_WHITE] = 0;
				br[EC_LED_COLOR_AMBER] = 0;
				if (time_cnt > 3)
					time_cnt = 0;
			}
			break;
		} else if (powerstate == POWER_S5 || powerstate == POWER_G3) {
			br[EC_LED_COLOR_WHITE] = 0;
			br[EC_LED_COLOR_AMBER] = 0;
		}
		break;
	default:
		/* Other states don't alter LED behavior */
		return;
	}

	if (prv_white == br[EC_LED_COLOR_WHITE] &&
	    prv_amber == br[EC_LED_COLOR_AMBER])
		return;

	prv_white = br[EC_LED_COLOR_WHITE];
	prv_amber = br[EC_LED_COLOR_AMBER];
	led_set_brightness(EC_LED_ID_BATTERY_LED, br);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return;

	brightness_range[EC_LED_COLOR_WHITE] = MT6370_LED_BRIGHTNESS_MAX;
	brightness_range[EC_LED_COLOR_AMBER] = MT6370_LED_BRIGHTNESS_MAX;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	uint8_t white, amber;

	if (led_id != EC_LED_ID_BATTERY_LED)
		return EC_ERROR_INVAL;

	white = brightness[EC_LED_COLOR_WHITE];
	amber = brightness[EC_LED_COLOR_AMBER];

	mt6370_led_set_brightness(LED_WHITE, white);
	mt6370_led_set_brightness(LED_AMBER, amber);

	/* Enables LED sink power if necessary. */
	mt6370_led_set_color((white ? LED_MASK_WHITE : 0) |
			     (amber ? LED_MASK_AMBER : 0));
	return EC_SUCCESS;
}

static void katsu_led_init(void)
{
	const enum mt6370_led_dim_mode dim = MT6370_LED_DIM_MODE_PWM;
	const enum mt6370_led_pwm_freq freq = MT6370_LED_PWM_FREQ1000;

	mt6370_led_set_color(0);
	mt6370_led_set_dim_mode(LED_WHITE, dim);
	mt6370_led_set_dim_mode(LED_AMBER, dim);
	mt6370_led_set_pwm_frequency(LED_WHITE, freq);
	mt6370_led_set_pwm_frequency(LED_AMBER, freq);
	mt6370_led_set_pwm_dim_duty(LED_WHITE, 255);
	mt6370_led_set_pwm_dim_duty(LED_AMBER, 255);
}
DECLARE_HOOK(HOOK_INIT, katsu_led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		katsu_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

__override void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	uint8_t br[EC_LED_COLOR_COUNT] = { 0 };

	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
	    (led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_auto_control(EC_LED_ID_BATTERY_LED, 1);
		return;
	}

	if (state)
		br[EC_LED_COLOR_WHITE] = 1;

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);
	led_set_brightness(EC_LED_ID_BATTERY_LED, br);
}
