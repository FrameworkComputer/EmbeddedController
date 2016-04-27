/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MEC1322-specific PWM module for Chrome EC */
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

/*
 * Returns PWMs that must remain active in low-power idle -
 * MEC1322_PCR_EC_SLP_EN bit mask.
 */
inline uint32_t pwm_get_keep_awake_mask(void);
#endif  /* __CROS_EC_PWM_CHIP_H */
