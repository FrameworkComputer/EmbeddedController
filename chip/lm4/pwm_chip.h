/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4-specific PWM module for Chrome EC */

#ifndef __CROS_EC_PWM_CHIP_H
#define __CROS_EC_PWM_CHIP_H

/* Data structure to define PWM channels. */
struct pwm_t {
	/* PWM channel ID */
	int channel;
	/* PWM channel flags. See include/pwm.h */
	uint32_t flags;
};

extern const struct pwm_t pwm_channels[];

#endif /* __CROS_EC_PWM_CHIP_H */
