/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_pwm_mock

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>

#include "pwm_mock.h"

#define CYCLES_PER_SEC 1000000

struct pwm_mock_data {
	uint32_t period_cycles;
	uint32_t pulse_cycles;
};

static int pwm_mock_init(const struct device *dev)
{
	return 0;
}

static int pwm_mock_set_cycles(const struct device *dev, uint32_t channel,
			       uint32_t period_cycles, uint32_t pulse_cycles,
			       pwm_flags_t flags)
{
	struct pwm_mock_data *const data = dev->data;

	data->period_cycles = period_cycles;
	data->pulse_cycles = pulse_cycles;

	return 0;
}

static int pwm_mock_get_cycles_per_sec(const struct device *dev,
				       uint32_t channel, uint64_t *cycles)
{
	*cycles = CYCLES_PER_SEC;

	return 0;
}

int pwm_mock_get_duty(const struct device *dev, uint32_t channel)
{
	struct pwm_mock_data *const data = dev->data;

	if (data->period_cycles == 0) {
		return -EINVAL;
	}

	return data->pulse_cycles * 100 / data->period_cycles;
}

static const struct pwm_driver_api pwm_mock_api = {
	.set_cycles = pwm_mock_set_cycles,
	.get_cycles_per_sec = pwm_mock_get_cycles_per_sec,
};

#define INIT_PWM_MOCK(inst)                                             \
	static struct pwm_mock_data pwm_mock_data##inst;                \
	DEVICE_DT_INST_DEFINE(inst, &pwm_mock_init, NULL,               \
			      &pwm_mock_data##inst, NULL, PRE_KERNEL_1, \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,       \
			      &pwm_mock_api);
DT_INST_FOREACH_STATUS_OKAY(INIT_PWM_MOCK)
