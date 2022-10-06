/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_displight

#include <zephyr/devicetree.h>
#include <drivers/cros_displight.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "common.h"
#include "util.h"

LOG_MODULE_REGISTER(displight, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,displight should be defined.");

#define DISPLIGHT_PWM_NODE DT_INST_PWMS_CTLR(0)
#define DISPLIGHT_PWM_CHANNEL DT_INST_PWMS_CHANNEL(0)
#define DISPLIGHT_PWM_FLAGS DT_INST_PWMS_FLAGS(0)
#define DISPLIGHT_PWM_PERIOD_NS DT_INST_PWMS_PERIOD(0)

static int displight_percent;

static void displight_set_duty(int percent)
{
	const struct device *pwm_dev = DEVICE_DT_GET(DISPLIGHT_PWM_NODE);
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(pwm_dev)) {
		LOG_ERR("PWM device %s not ready", pwm_dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(DISPLIGHT_PWM_PERIOD_NS * percent, 100);

	LOG_DBG("displight PWM %s set percent (%d), pulse %d", pwm_dev->name,
		percent, pulse_ns);

	rv = pwm_set(pwm_dev, DISPLIGHT_PWM_CHANNEL, DISPLIGHT_PWM_PERIOD_NS,
		     pulse_ns, DISPLIGHT_PWM_FLAGS);
	if (rv) {
		LOG_ERR("pwm_set() failed %s (%d)", pwm_dev->name, rv);
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
