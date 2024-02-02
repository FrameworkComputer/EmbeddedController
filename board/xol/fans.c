/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Physical fans. These are logically separate from pwm_channels. */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "fan.h"
#include "fan_chip.h"
#include "hooks.h"
#include "pwm.h"

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

static const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

/*
 * TOOD(b/181271666): thermistor placement and calibration
 *
 * Prototype fan spins at about 4200 RPM at 100% PWM, this
 * is specific to board ID 2 and might also apears in later
 * boards as well.
 */
static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2200,
	.rpm_start = 2200,
	.rpm_max = 4200,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

#ifndef CONFIG_FANS

/*
 * TODO(b/181271666): use static fan speeds until fan and sensors are
 * tuned. for now, use:
 *
 *   AP off:  33%
 *   AP  on: 100%
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
	const int duty_pct = 100;

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
