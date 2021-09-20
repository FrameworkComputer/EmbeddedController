/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_pwm_leds

#include <string.h>
#include <devicetree.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#include "led_pwm.h"
#include "pwm.h"

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(cros_ec_pwm_leds) <= 1,
	     "Multiple CrOS EC PWM LED instances defined");
BUILD_ASSERT(DT_INST_PROP_LEN(0, leds) <= 2,
	     "Unsupported number of LEDs defined");

#define PWM_CHANNEL_BY_IDX(node_id, prop, idx, led_ch)          \
	PWM_CHANNEL(DT_PWMS_CTLR_BY_IDX(                        \
		DT_PHANDLE_BY_IDX(node_id, prop, idx), led_ch))

#define PWM_LED_INIT(node_id, prop, idx) \
	[PWM_LED##idx] = { \
		.ch0 = PWM_CHANNEL_BY_IDX(node_id, prop, idx, 0), \
		.ch1 = PWM_CHANNEL_BY_IDX(node_id, prop, idx, 1), \
		.ch2 = PWM_CHANNEL_BY_IDX(node_id, prop, idx, 2), \
		.enable = &pwm_enable, \
		.set_duty = &pwm_set_duty, \
	},

struct pwm_led pwm_leds[] = {
	DT_INST_FOREACH_PROP_ELEM(0, leds, PWM_LED_INIT)
};

struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT] = {
	[EC_LED_COLOR_RED]    = DT_INST_PROP(0, color_map_red),
	[EC_LED_COLOR_GREEN]  = DT_INST_PROP(0, color_map_green),
	[EC_LED_COLOR_BLUE]   = DT_INST_PROP(0, color_map_blue),
	[EC_LED_COLOR_YELLOW] = DT_INST_PROP(0, color_map_yellow),
	[EC_LED_COLOR_WHITE]  = DT_INST_PROP(0, color_map_white),
	[EC_LED_COLOR_AMBER]  = DT_INST_PROP(0, color_map_amber),
};

BUILD_ASSERT(DT_INST_PROP_LEN(0, brightness_range) == EC_LED_COLOR_COUNT,
	     "brightness_range must have exactly EC_LED_COLOR_COUNT values");

static const uint8_t dt_brigthness_range[EC_LED_COLOR_COUNT] = DT_INST_PROP(
		0, brightness_range);

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* led_id is ignored, same ranges for all LEDs */
	memcpy(brightness_range, dt_brigthness_range,
	       sizeof(dt_brigthness_range));
}

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
