/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MEC1701H-specific PWM module for Chrome EC */
#ifndef __CROS_EC_PWM_CHIP_H
#define __CROS_EC_PWM_CHIP_H

/*
 * MEC152x SZ 144-pin has 9 PWM and 4 TACH
 * MEC170x SZ 144-pin has 9 PWM and 3 TACH
 * MEC172x SZ 144-pin has 9 PWM and 4 TACH
 */
enum pwm_hw_id {
	PWM_HW_CH_0 = 0,
	PWM_HW_CH_1,
	PWM_HW_CH_2,
	PWM_HW_CH_3,
	PWM_HW_CH_4,
	PWM_HW_CH_5,
	PWM_HW_CH_6,
	PWM_HW_CH_7,
	PWM_HW_CH_8,
	PWM_HW_CH_COUNT
};

enum tach_hw_id {
	TACH_HW_CH_0 = 0,
	TACH_HW_CH_1,
	TACH_HW_CH_2,
#ifndef CHIP_FAMILY_MEC170X
	TACH_HW_CH_3,
#endif
	TACH_HW_CH_COUNT
};

/* Data structure to define PWM channels. */
struct pwm_t {
	/* PWM Channel ID */
	int channel;

	/* PWM channel flags. See include/pwm.h */
	uint32_t flags;
};

extern const struct pwm_t pwm_channels[];

void pwm_keep_awake(void);

#endif  /* __CROS_EC_PWM_CHIP_H */
