/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_pwm_leds

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

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
