/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_PWM_PWM_H_
#define ZEPHYR_SHIM_INCLUDE_PWM_PWM_H_

#include <device.h>
#include <devicetree.h>

#if DT_NODE_EXISTS(DT_PATH(named_pwms))

#define PWM_CHANNEL(id) DT_CAT(PWM_, id)
#define PWM_CHANNEL_WITH_COMMA(id) PWM_CHANNEL(id),

enum pwm_channel {
	DT_FOREACH_CHILD(DT_PATH(named_pwms), PWM_CHANNEL_WITH_COMMA)
		PWM_CH_COUNT,
};

#define NAMED_PWM(name) PWM_CHANNEL(DT_PATH(named_pwms, name))

#endif /* named_pwms */

#endif /* ZEPHYR_SHIM_INCLUDE_PWM_PWM_H_ */
