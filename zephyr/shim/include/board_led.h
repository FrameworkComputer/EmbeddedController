/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_LED_H
#define __BOARD_LED_H

struct board_led_pwm_dt_channel {
	const struct device *dev;
	uint32_t channel;
	pwm_flags_t flags;
};

#define BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(node_id) \
	{ \
		.dev = DEVICE_DT_GET(DT_PWMS_CTLR(node_id)), \
		.channel = DT_PWMS_CHANNEL(node_id), \
		.flags = DT_PWMS_FLAGS(node_id), \
	}

#define BOARD_LED_HZ_TO_PERIOD_US(freq_hz) (USEC_PER_SEC / freq_hz)

#endif  /* __BOARD_LED_H */
