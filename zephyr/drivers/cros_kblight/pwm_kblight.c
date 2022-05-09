/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_kblight_pwm

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "common.h"
#include "keyboard_backlight.h"
#include "util.h"

LOG_MODULE_REGISTER(kblight, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,kblight-pwm should be defined.");

#define KBLIGHT_PWM_NODE DT_INST_PWMS_CTLR(0)
#define KBLIGHT_PWM_CHANNEL DT_INST_PWMS_CHANNEL(0)
#define KBLIGHT_PWM_FLAGS DT_INST_PWMS_FLAGS(0)
#define KBLIGHT_PWM_PERIOD_NS (NSEC_PER_SEC/DT_INST_PROP(0, frequency))

static bool kblight_enabled;
static int kblight_percent;

static void kblight_pwm_set_duty(int percent)
{
	const struct device *pwm_dev = DEVICE_DT_GET(KBLIGHT_PWM_NODE);
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(pwm_dev)) {
		LOG_ERR("PWM device %s not ready", pwm_dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(KBLIGHT_PWM_PERIOD_NS * percent, 100);

	LOG_DBG("kblight PWM %s set percent (%d), pulse %d",
		pwm_dev->name, percent, pulse_ns);

	rv = pwm_set(pwm_dev, KBLIGHT_PWM_CHANNEL, KBLIGHT_PWM_PERIOD_NS,
		     pulse_ns, KBLIGHT_PWM_FLAGS);
	if (rv) {
		LOG_ERR("pwm_set() failed %s (%d)", pwm_dev->name, rv);
	}
}

static int kblight_pwm_set(int percent)
{
	kblight_percent = percent;
	kblight_pwm_set_duty(percent);
	return EC_SUCCESS;
}

static int kblight_pwm_get(void)
{
	return kblight_percent;
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
	.get = kblight_pwm_get,
	.enable = kblight_pwm_enable,
	.get_enabled = kblight_pwm_get_enabled,
};
