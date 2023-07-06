/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tach_mock.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#define DT_DRV_COMPAT cros_tach_mock

struct tach_mock_data {
	int32_t rpm_val;
};

static int tach_mock_init(const struct device *dev)
{
	return 0;
}

int tach_mock_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	ARG_UNUSED(chan);
	ARG_UNUSED(dev);

	/* Fetch went a-OK */
	return 0;
}

static int tach_mock_channel_get(const struct device *dev,
				 enum sensor_channel chan,
				 struct sensor_value *val)
{
	ARG_UNUSED(chan);
	struct tach_mock_data *const data = dev->data;

	val->val1 = data->rpm_val;
	val->val2 = 0U;

	return 0;
}

static const struct sensor_driver_api tach_mock_api = {
	.sample_fetch = tach_mock_sample_fetch,
	.channel_get = tach_mock_channel_get,
};

#define MOCK_TACH_INIT(inst)                                               \
	static struct tach_mock_data tach_data_##inst;                     \
                                                                           \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, tach_mock_init, NULL,           \
				     &tach_data_##inst, NULL, POST_KERNEL, \
				     CONFIG_SENSOR_INIT_PRIORITY,          \
				     &tach_mock_api);

DT_INST_FOREACH_STATUS_OKAY(MOCK_TACH_INIT)
