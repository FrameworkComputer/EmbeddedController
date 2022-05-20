/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Ghost specific PWM LED settings.
 *
 * Early boards have 2 PWM LEDs which we simply treat as power
 * indicators.
 */

#include <stdint.h>

#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, '\0',
	       sizeof(*brightness_range) * EC_LED_COLOR_COUNT);
	brightness_range[EC_LED_COLOR_WHITE] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	uint8_t duty_percent;

	if (led_id != EC_LED_ID_POWER_LED)
		return EC_ERROR_UNKNOWN;

	duty_percent = brightness[EC_LED_COLOR_WHITE];
	if (duty_percent > 100)
		duty_percent = 100;
	pwm_set_duty(PWM_CH_LED1, duty_percent);
	pwm_set_duty(PWM_CH_LED2, duty_percent);

	return EC_SUCCESS;
}
