/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_kblight_pwm

#include "common.h"
#include "keyboard_backlight.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(kblight, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,kblight-pwm should be defined.");

static const struct pwm_dt_spec kblight_pwm_dt = PWM_DT_SPEC_INST_GET(0);

static bool kblight_enabled;
static int kblight_percent;

static void kblight_pwm_set_duty(int percent)
{
	const struct device *pwm_dev = kblight_pwm_dt.dev;
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(pwm_dev)) {
		LOG_ERR("device %s not ready", pwm_dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(kblight_pwm_dt.period * percent, 100);

	LOG_DBG("kblight PWM %s set percent (%d), pulse %d", pwm_dev->name,
		percent, pulse_ns);

	rv = pwm_set_pulse_dt(&kblight_pwm_dt, pulse_ns);
	if (rv) {
		LOG_ERR("pwm_set_pulse_dt failed %s (%d)", pwm_dev->name, rv);
	}
}

static int kblight_pwm_set(int percent)
{
	kblight_percent = percent;
	kblight_pwm_set_duty(percent);
	return EC_SUCCESS;
}

static int kblight_pwm_enable(int enable)
{
	kblight_enabled = enable;
	if (enable) {
		kblight_pwm_set_duty(kblight_percent);
	} else {
		/* Disable but hold kblight_percent. */
		kblight_pwm_set_duty(0);
	}
	return EC_SUCCESS;
}

static int kblight_pwm_get_enabled(void)
{
	return kblight_enabled;
}

static int kblight_pwm_init(void)
{
	kblight_percent = 0;
	kblight_pwm_enable(0);
	return EC_SUCCESS;
}

const struct kblight_drv kblight_pwm = {
	.init = kblight_pwm_init,
	.set = kblight_pwm_set,
	.enable = kblight_pwm_enable,
	.get_enabled = kblight_pwm_get_enabled,
};
