/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dibbi specific PWM LED settings. */

#include "common.h"
#include "ec_commands.h"
#include "pwm.h"
#include "util.h"

/* TODO(b/259467280) Implement LED logic */

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* TODO(b/259467280) check this implementation */
	memset(brightness_range, '\0',
	       sizeof(*brightness_range) * EC_LED_COLOR_COUNT);
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_YELLOW] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	/* TODO(b/259467280) fix this implementation */

	return EC_SUCCESS;
}
