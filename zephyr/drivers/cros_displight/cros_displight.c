/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_displight

#include "common.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_displight.h>

LOG_MODULE_REGISTER(displight, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,displight should be defined.");

static const struct pwm_dt_spec displight_pwm = PWM_DT_SPEC_INST_GET(0);

static int displight_percent;

static void displight_set_duty(int percent)
{
	const struct device *pwm_dev = displight_pwm.dev;
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(pwm_dev)) {
		LOG_ERR("device %s not ready", pwm_dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(displight_pwm.period * percent, 100);

	LOG_DBG("displight PWM %s set percent (%d), pulse %d", pwm_dev->name,
		percent, pulse_ns);

	rv = pwm_set_pulse_dt(&displight_pwm, pulse_ns);
	if (rv) {
		LOG_ERR("pwm_set_pulse_dt failed %s (%d)", pwm_dev->name, rv);
	}
}

int displight_set(int percent)
{
	displight_percent = percent;
	displight_set_duty(percent);
	return EC_SUCCESS;
}

int displight_get(void)
{
	return displight_percent;
}
