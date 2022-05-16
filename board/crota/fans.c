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
#include "thermal.h"
#include "util.h"

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
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

static const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3500,
	.rpm_start = 3500,
	.rpm_max = 4300,
};

static const struct fan_rpm fan_rpm_1 = {
	.rpm_min = 4300,
	.rpm_start = 4300,
	.rpm_max = 4700,
};

struct fan_t fans[FAN_CH_COUNT] = {
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

static void fan_set_percent(int fan, int pct, bool fan_triggered)
{
	int new_rpm;

	if (fan_triggered)
		fans[fan].rpm = &fan_rpm_1;
	else
		fans[fan].rpm = &fan_rpm_0;

	new_rpm = fan_percent_to_rpm(fan, pct);

	fan_set_rpm_target(FAN_CH(fan), new_rpm);
}

void board_override_fan_control(int fan, int *tmp)
{
	/*
	* Crota's fan speed is control by three sensors.
	*
	* Sensor SOC control high loading's speed.
	* Sensor ambient control low loading's speed.
	* Sensor charger control the speed when system's temperature
	* is too high.
	*
	* When sensor charger is not triggered, the fan is control
	* and choose the smaller speed between SOC and ambient.
	*
	* When sensor charger is triggered, the fan speed is only
	* control by sensor charger, avoid heat damage to system.
	*/

	int pct;
	int sensor_soc;
	int sensor_ambient;
	int sensor_charger;
	bool fan_triggered;

	sensor_soc = thermal_fan_percent(thermal_params[0].temp_fan_off,
				thermal_params[0].temp_fan_max,
				C_TO_K(tmp[0]));
	sensor_ambient = thermal_fan_percent(thermal_params[3].temp_fan_off,
				thermal_params[3].temp_fan_max,
				C_TO_K(tmp[3]));
	sensor_charger = thermal_fan_percent(thermal_params[2].temp_fan_off,
				thermal_params[2].temp_fan_max,
				C_TO_K(tmp[2]));

	if (sensor_charger){
		fan_triggered = true;
		pct = sensor_charger;
	}
	else{
		fan_triggered = false;
		pct = MIN(sensor_soc, sensor_ambient);
	}

	/* transfer percent to rpm */
	fan_set_percent(fan, pct, fan_triggered);
}
