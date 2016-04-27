/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific PWM module for Chrome EC */

#ifndef __CROS_EC_PWM_CHIP_H
#define __CROS_EC_PWM_CHIP_H

/* Data structure to define PWM channels. */
struct pwm_t {
	/* PWM channel ID */
	int channel;
	/* PWM channel flags. See include/pwm.h */
	uint32_t flags;
	/* PWM freq. */
	uint32_t freq;
};

extern const struct pwm_t pwm_channels[];
void pwm_config(enum pwm_channel ch);

#endif /* __CROS_EC_PWM_CHIP_H */
