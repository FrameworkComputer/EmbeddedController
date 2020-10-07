/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MEC1701H-specific PWM module for Chrome EC */
#ifndef __CROS_EC_PWM_CHIP_H
#define __CROS_EC_PWM_CHIP_H

/* Data structure to define PWM channels. */
struct pwm_t {
	/* PWM Channel ID */
	int channel;

	/* PWM channel flags. See include/pwm.h */
	uint32_t flags;
};

extern const struct pwm_t pwm_channels[];

void pwm_keep_awake(void);
void pwm_configure(int ch, int active_low, int clock_low);
void pwm_slp_en(int pwm_id, int sleep_en);

#endif  /* __CROS_EC_PWM_CHIP_H */
