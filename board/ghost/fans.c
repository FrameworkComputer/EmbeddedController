/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan_chip.h"
#include "fan.h"
#include "hooks.h"
#include "pwm.h"

#ifndef CONFIG_FANS

/*
 * TODO(b/233126129): use static fan speeds until fan and sensors are
 * tuned. for now, use:
 *
 *   AP off:  33%
 *   AP  on:  50%
 */

static void fan_slow(void)
{
	const int duty_pct = 33;

	ccprints("%s: speed %d%%", __func__, duty_pct);

	pwm_enable(PWM_CH_FAN, 1);
	pwm_set_duty(PWM_CH_FAN, duty_pct);
}

static void fan_max(void)
{
	const int duty_pct = 50;

	ccprints("%s: speed %d%%", __func__, duty_pct);

	pwm_enable(PWM_CH_FAN, 1);
	pwm_set_duty(PWM_CH_FAN, duty_pct);
}

DECLARE_HOOK(HOOK_INIT, fan_slow, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, fan_slow, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, fan_slow, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESET, fan_max, HOOK_PRIO_FIRST);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, fan_max, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_FANS */
