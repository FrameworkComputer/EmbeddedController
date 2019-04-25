/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Flapjack board.
 */

#include "battery.h"
#include "charge_state.h"
#include "console.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "led_common.h"
#include "util.h"

/* Define this to enable led command and debug LED */
#undef DEBUG_LED

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

#define LED_OFF		MT6370_LED_ID_OFF
#define LED_RED		MT6370_LED_ID1
#define LED_GRN		MT6370_LED_ID2
#define LED_BLU		MT6370_LED_ID3

#define LED_MASK_OFF	0
#define LED_MASK_RED	MT6370_MASK_RGB_ISNK1DIM_EN
#define LED_MASK_GRN	MT6370_MASK_RGB_ISNK2DIM_EN
#define LED_MASK_BLU	MT6370_MASK_RGB_ISNK3DIM_EN

static enum charge_state chstate;

static void led_set_battery(void)
{
	static enum charge_state prev = PWR_STATE_UNCHANGE;
	static int battery_second;
	int red = 0, grn = 0, blu = 0;

	battery_second++;
#ifndef DEBUG_LED
	chstate = charge_get_state();
#endif
	if (chstate == prev)
		return;
	prev = chstate;

	/*
	 * Full      White
	 * Charging  Amber
	 * Error     Red
	 */
	switch (chstate) {
	case PWR_STATE_CHARGE:
		/* RGB(current, duty) = (4mA,10/32) (4mA,1/32) (0mA,) */
		red = 1;
		grn = 1;
		mt6370_led_set_pwm_dim_duty(LED_RED, 9);
		mt6370_led_set_pwm_dim_duty(LED_GRN, 0);
		break;
	case PWR_STATE_DISCHARGE:
		/* RGB(current, duty) = (0mA,) (0mA,) (0mA,) */
		break;
	case PWR_STATE_ERROR:
		/* RGB(current, duty) = (4mA,8/32) (0mA,) (0mA,) */
		red = 1;
		mt6370_led_set_pwm_dim_duty(LED_RED, 7);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		/* RGB(current, duty) = (8mA,2/32) (8mA,1/32) (4mA,1/32) */
		red = 2;
		grn = 2;
		blu = 1;
		mt6370_led_set_pwm_dim_duty(LED_RED, 1);
		mt6370_led_set_pwm_dim_duty(LED_GRN, 0);
		mt6370_led_set_pwm_dim_duty(LED_BLU, 0);
		break;
	default:
		/* Other states don't alter LED behavior */
		return;
	}

	mt6370_led_set_brightness(LED_RED, red);
	mt6370_led_set_brightness(LED_GRN, grn);
	mt6370_led_set_brightness(LED_BLU, blu);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* Current is fixed at 4mA. Brightness is controlled by duty only. */
	const uint8_t max = MT6370_LED_PWM_DIMDUTY_MAX;
	if (led_id != EC_LED_ID_BATTERY_LED)
		return;
	brightness_range[EC_LED_COLOR_GREEN] = max;
	brightness_range[EC_LED_COLOR_RED] = max;
	brightness_range[EC_LED_COLOR_BLUE] = max;
}

static void set_current_and_pwm_duty(uint8_t brightness,
				     enum mt6370_led_index color)
{
	if (brightness) {
		/* Current is fixed at 4mA. Brightness is controlled by duty. */
		mt6370_led_set_brightness(color, 1);
		mt6370_led_set_pwm_dim_duty(color, brightness);
	} else {
		/* off */
		mt6370_led_set_brightness(color, 0);
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id != EC_LED_ID_BATTERY_LED)
		return EC_ERROR_INVAL;
	set_current_and_pwm_duty(brightness[EC_LED_COLOR_RED], LED_RED);
	set_current_and_pwm_duty(brightness[EC_LED_COLOR_GREEN], LED_GRN);
	set_current_and_pwm_duty(brightness[EC_LED_COLOR_BLUE], LED_BLU);
	return EC_SUCCESS;
}

static void flapjack_led_init(void)
{
	const enum mt6370_led_dim_mode dim = MT6370_LED_DIM_MODE_PWM;
	const enum mt6370_led_pwm_freq freq = MT6370_LED_PWM_FREQ1000;
	mt6370_led_set_color(LED_MASK_RED | LED_MASK_GRN | LED_MASK_BLU);
	mt6370_led_set_dim_mode(LED_RED, dim);
	mt6370_led_set_dim_mode(LED_GRN, dim);
	mt6370_led_set_dim_mode(LED_BLU, dim);
	mt6370_led_set_pwm_frequency(LED_RED, freq);
	mt6370_led_set_pwm_frequency(LED_GRN, freq);
	mt6370_led_set_pwm_frequency(LED_BLU, freq);
}
DECLARE_HOOK(HOOK_INIT, flapjack_led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

#ifdef DEBUG_LED
static int command_led(int argc, char **argv)
{
	int val;
	char *e;

	mt6370_led_set_color(LED_MASK_RED|LED_MASK_GRN|LED_MASK_BLU);

	if (argc == 2) {
		val = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		chstate = val;
		return EC_SUCCESS;
	}

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	val = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;
	mt6370_led_set_brightness(LED_RED, val);

	val = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;
	mt6370_led_set_brightness(LED_GRN, val);

	val = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;
	mt6370_led_set_brightness(LED_BLU, val);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led, "<chg_state> or <R> <G> <B>", NULL);
#endif
