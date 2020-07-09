/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM control module for keyboard backlight. */

#include "common.h"
#include "keyboard_backlight.h"
#include "pwm.h"
#include "system.h"
#include "util.h"

const enum pwm_channel kblight_pwm_ch = PWM_CH_KBLIGHT;

static int kblight_pwm_set(int percent)
{
	pwm_set_duty(kblight_pwm_ch, percent);
	return EC_SUCCESS;
}

static int kblight_pwm_get(void)
{
	return pwm_get_duty(kblight_pwm_ch);
}

static int kblight_pwm_init(void)
{
	/* dnojiri: Why do we need save/restore setting over sysjump? */
	kblight_pwm_set(0);
	pwm_enable(kblight_pwm_ch, 0);
	return EC_SUCCESS;
}

static int kblight_pwm_enable(int enable)
{
	pwm_enable(kblight_pwm_ch, enable);
	return EC_SUCCESS;
}

const struct kblight_drv kblight_pwm = {
	.init = kblight_pwm_init,
	.set = kblight_pwm_set,
	.get = kblight_pwm_get,
	.enable = kblight_pwm_enable,
};
