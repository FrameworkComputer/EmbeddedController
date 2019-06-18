/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* STM32-specific PWM module for Chrome EC */

#ifndef __CROS_EC_PWM_CHIP_H
#define __CROS_EC_PWM_CHIP_H

/* Data structure to define PWM channels. */
struct pwm_t {
	/*
	 * Timer powering the PWM channel. Must use STM32_TIM(x) to
	 * initialize
	 */
	struct {
		int id;
		uintptr_t base;
	} tim;
	/* Channel ID within the timer */
	int channel;
	/* PWM channel flags. See include/pwm.h */
	uint32_t flags;
	/* PWM frequency (Hz) */
	int frequency;
};

extern const struct pwm_t pwm_channels[];

/* Macro to fill in both timer ID and register base */
#define STM32_TIM(x) {x, STM32_TIM_BASE(x)}

/* Plain ID mapping for readability */
#define STM32_TIM_CH(x) (x)

#endif /* __CROS_EC_PWM_CHIP_H */
