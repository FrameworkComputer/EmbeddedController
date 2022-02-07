/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <devicetree.h>
#include <drivers/pwm.h>
#include <logging/log.h>

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "pwm.h"
#include "util.h"

#include "pwm/pwm.h"

LOG_MODULE_REGISTER(pwm_shim, LOG_LEVEL_ERR);

/*
 * Initialize the device bindings in pwm_channels.
 * This macro is called from within DT_FOREACH_CHILD
 */
#define INIT_DEV_BINDING(id) {                                              \
		pwm_configs[PWM_CHANNEL(id)].name = DT_NODE_FULL_NAME(id);  \
		pwm_configs[PWM_CHANNEL(id)].dev = DEVICE_DT_GET(           \
			DT_PHANDLE(id, pwms));                              \
		pwm_configs[PWM_CHANNEL(id)].pin = DT_PWMS_CHANNEL(id);     \
		pwm_configs[PWM_CHANNEL(id)].flags = DT_PWMS_FLAGS(id);     \
		pwm_configs[PWM_CHANNEL(id)].freq = DT_PROP(id, frequency); \
	}

struct pwm_config {
	/* Name */
	const char *name;
	/* PWM pin */
	uint32_t pin;
	/* PWM channel flags. See dt-bindings/pwm/pwm.h */
	pwm_flags_t flags;
	/* PWM operating frequency. Configured by the devicetree */
	uint32_t freq;

	/* PWM period in microseconds. Automatically set to 1/frequency */
	uint32_t period_us;
	/* PWM pulse in microseconds. Set by pwm_set_raw_duty */
	uint32_t pulse_us;
	/* Saves whether the PWM channel is currently enabled */
	bool enabled;

	/* Runtime device for PWM */
	const struct device *dev;
};

static struct pwm_config pwm_configs[PWM_CH_COUNT];

static int init_pwms(const struct device *unused)
{
	struct pwm_config *pwm;
	int rv = 0;

	ARG_UNUSED(unused);

	/* Initialize PWM data from the device tree */
	DT_FOREACH_CHILD(DT_PATH(named_pwms), INIT_DEV_BINDING)

	/* Read the PWM operating frequency, set by the chip driver */
	for (size_t i = 0; i < PWM_CH_COUNT; ++i) {
		pwm = &pwm_configs[i];

		if (pwm->dev == NULL) {
			LOG_ERR("Not found (%s)", pwm->name);
			rv = -ENODEV;
			continue;
		}

		/*
		 * TODO - check that devicetree frequency is less than 1/2
		 * max frequency from the chip driver.
		 */
		pwm->period_us = USEC_PER_SEC / pwm->freq;
	}

	return rv;
}
#if CONFIG_PLATFORM_EC_PWM_INIT_PRIORITY <= CONFIG_KERNEL_INIT_PRIORITY_DEVICE
#error "PWM init priority must be > KERNEL_INIT_PRIORITY_DEVICE"
#endif
SYS_INIT(init_pwms, PRE_KERNEL_1, CONFIG_PLATFORM_EC_PWM_INIT_PRIORITY);

static struct pwm_config* pwm_lookup(enum pwm_channel ch)
{
	__ASSERT(ch < ARRAY_SIZE(pwm_configs), "Invalid PWM channel %d", ch);

	return &pwm_configs[ch];
}

void pwm_enable(enum pwm_channel ch, int enabled)
{
	struct pwm_config *pwm;
	uint32_t pulse_us;
	int rv;

	pwm = pwm_lookup(ch);
	pwm->enabled = enabled;

	/*
	 * The Zephyr API doesn't provide explicit enable and disable
	 * commands. However, setting the pulse width to zero disables
	 * the PWM.
	 */
	if (enabled)
		pulse_us = pwm->pulse_us;
	else
		pulse_us = 0;

	rv = pwm_pin_set_usec(pwm->dev, pwm->pin, pwm->period_us, pulse_us,
			      pwm->flags);

	if (rv)
		LOG_ERR("pwm_pin_set_usec() failed %s (%d)", pwm->name, rv);
}

int pwm_get_enabled(enum pwm_channel ch)
{
	struct pwm_config *pwm;

	pwm = pwm_lookup(ch);
	return pwm->enabled;
}

void pwm_set_raw_duty(enum pwm_channel ch, uint16_t duty)
{
	struct pwm_config *pwm;
	int rv;

	pwm = pwm_lookup(ch);

	pwm->pulse_us =
		DIV_ROUND_NEAREST(pwm->period_us * duty, EC_PWM_MAX_DUTY);

	LOG_DBG("PWM %s set raw duty (0x%04x), pulse %d", pwm->name, duty,
		pwm->pulse_us);

	rv = pwm_pin_set_usec(pwm->dev, pwm->pin, pwm->period_us, pwm->pulse_us,
			      pwm->flags);

	if (rv)
		LOG_ERR("pwm_pin_set_usec() failed %s (%d)", pwm->name, rv);
}

uint16_t pwm_get_raw_duty(enum pwm_channel ch)
{
	struct pwm_config *pwm;

	pwm = pwm_lookup(ch);

	return DIV_ROUND_NEAREST(pwm->pulse_us * EC_PWM_MAX_DUTY,
				 pwm->period_us);
}

void pwm_set_duty(enum pwm_channel ch, int percent)
{
	struct pwm_config *pwm;
	int rv;

	pwm = pwm_lookup(ch);

	pwm->pulse_us = DIV_ROUND_NEAREST(pwm->period_us * percent, 100);

	LOG_DBG("PWM %s set percent (%d), pulse %d", pwm->name, percent,
		pwm->pulse_us);

	rv = pwm_pin_set_usec(pwm->dev, pwm->pin, pwm->period_us, pwm->pulse_us,
			      pwm->flags);

	if (rv)
		LOG_ERR("pwm_pin_set_usec() failed %s (%d)", pwm->name, rv);
}

int pwm_get_duty(enum pwm_channel ch)
{
	struct pwm_config *pwm;

	pwm = pwm_lookup(ch);

	return DIV_ROUND_NEAREST(pwm->pulse_us * 100, pwm->period_us);
}
